import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('capture_cpu', Path(__file__).parent / 'diagnostic/capture_calibration_change.py')
cpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpu)


class CalibrationChangeTests(unittest.TestCase):
    def run_capture(self, temperature=25, warning=80, fail_set=False, fail_hold=False):
        offset = cpu.f32(0.123456789)
        original = offset
        writes = []
        ready = []
        checkpoints = []

        def command(text, expected):
            nonlocal offset
            if text == 'CLIMATE CPU --machine':
                return f'RACOOL_CLI v=1 type=climate_cpu override=0 cpu_c={temperature} warning_c={warning} critical_c=90 emergency_c=100 polling_ms=5000\n'
            if text == 'int_temp status --machine':
                return f'RACOOL_CLI v=1 type=cpu_sensor_status valid=1 temperature_c={temperature} offset_c={offset:.9g} level=0 trend=0 state=2 sample_time_ms=1000 age_ms=50\n'
            if text.startswith('int_temp offset '):
                offset = cpu.f32(float(text.split()[2]))
                writes.append(offset)
                if fail_set and len(writes) == 1:
                    raise RuntimeError('Uncertain delivery')
                return 'RACOOL_CLI v=1 type=cpu_sensor_command command=offset result=applied error=0\n'
            return 'RACOOL_CLI v=1 type=cpu_sensor_read result=read temperature_c=25\n'

        def hold():
            if fail_hold:
                raise RuntimeError('Observation failed')

        def checkpoint(snapshot):
            self.assertEqual([],writes)
            self.assertEqual(original,snapshot['original_offset'])
            checkpoints.append(snapshot)
        result = cpu.capture(command, hold, lambda: ready.append(True),checkpoint)
        if writes:self.assertEqual(1,len(checkpoints))
        return result, offset, original, writes, ready

    def test_exact_float_restoration(self):
        result, offset, original, writes, ready = self.run_capture()
        self.assertTrue(result['completed'])
        self.assertEqual(offset, original)
        self.assertEqual(len(writes), 2)
        self.assertEqual(ready, [True])

    def test_thermal_margin_refuses_before_mutation(self):
        result, _, _, writes, ready = self.run_capture(temperature=69)
        self.assertFalse(result['completed'])
        self.assertEqual(writes, [])
        self.assertEqual(ready, [])

    def test_uncertain_delivery_is_read_before_restoration(self):
        result, offset, original, writes, ready = self.run_capture(fail_set=True)
        self.assertFalse(result['completed'])
        self.assertTrue(result['restoration_verified'])
        self.assertEqual(offset, original)
        self.assertEqual(len(writes), 2)
        self.assertEqual(ready, [])

    def test_observation_failure_restores(self):
        result, offset, original, _, _ = self.run_capture(fail_hold=True)
        self.assertFalse(result['completed'])
        self.assertTrue(result['restoration_verified'])
        self.assertEqual(offset, original)

    def test_missing_nonfinite_and_partial_status_rejected(self):
        for text in ['', 'Offset : nan C', 'Offset : 1 C\nLast Temp : 25 C']:
            with self.assertRaises(RuntimeError):
                cpu.parse_status(text)
        with self.assertRaises(RuntimeError):
            cpu.parse_warning('CPU Warning : nan C')

    def test_latest_record_cannot_hide_incomplete_or_invalid_cpu_sample(self):
        valid='RACOOL_CLI v=1 type=cpu_sensor_status valid=1 temperature_c=25 offset_c=0.1 level=0 trend=0 state=2 sample_time_ms=1000 age_ms=50\n'
        self.assertEqual('NORMAL',cpu.parse_status(valid)['level'])
        for invalid in [valid.rstrip('\n'),valid.replace('v=1','v=2'),valid.replace('valid=1','valid=0'),valid.replace('age_ms=50','age_ms=-1'),valid.replace('temperature_c=25','temperature_c=nan'),valid.replace('offset_c=0.1','offset_c=10'),valid.replace('state=2','state=7')]:
            with self.assertRaises(RuntimeError):cpu.parse_status(valid+invalid)


if __name__ == '__main__':
    unittest.main()
