import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from Hardware.InfraredTemperatureSensor.diagnostic.ir_cli_evidence import parse_status,parse_stats,require_poll,require_selftest
import unittest

STATUS='RACOOL_CLI v=1 type=ir_sensor_status object_c=25.9 ambient_c=24.5 measured_time_us=1000000 age_ms=50 polling_ms=100\n'
STATS='RACOOL_CLI v=1 type=ir_sensor_stats reads=100 pec_errors=2 i2c_errors=1 failure_kind=3 failure_error=263 failure_time_us=1000000 object_min_c=20 object_max_c=30 ambient_min_c=20 ambient_max_c=30\n'

class IREvidenceTests(unittest.TestCase):
    def test_sample_and_failure_history_are_preserved(self):
        self.assertEqual(25.9,parse_status(STATUS)['object_c'])
        self.assertEqual(100,parse_status(STATUS)['polling_ms'])
        self.assertEqual(3,parse_stats(STATS)['failure_kind'])
        self.assertEqual(2,parse_stats(STATS)['pec_errors'])
    def test_latest_invalid_or_partial_record_is_rejected(self):
        for bad in [STATUS.rstrip('\n'),STATUS.replace('v=1','v=2'),STATUS.replace('age_ms=50','age_ms=-1'),STATUS.replace('object_c=25.9','object_c=nan'),STATUS.replace('ambient_c=24.5','ambient_c=-273.15'),STATUS.replace('polling_ms=100','missing=100')]:
            with self.assertRaises(ValueError):parse_status(STATUS+bad)
        with self.assertRaises(ValueError):parse_stats(STATS+STATS.replace('failure_kind=3','failure_kind=4'))
    def test_checked_commands_require_real_success(self):
        command='RACOOL_CLI v=1 type=ir_sensor_command command=poll result=applied error=0 interval_ms=0\n'
        require_poll(command,0)
        with self.assertRaises(ValueError):require_poll(command,100)
        with self.assertRaises(ValueError):require_poll(command.replace('result=applied','result=rejected'),0)
        require_selftest('RACOOL_CLI v=1 type=ir_sensor_test result=passed error=0\n')
        with self.assertRaises(ValueError):require_selftest('RACOOL_CLI v=1 type=ir_sensor_test result=failed error=-1\n')

if __name__=='__main__':unittest.main()
