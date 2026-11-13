import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from Management.DiagnosticsManager.diagnostic.verify_readonly_health import validate
import unittest


def stats(reads, errors=0):
    return f'COMMAND: ir_temp stats --machine\nRACOOL_CLI v=1 type=ir_sensor_stats reads={reads} pec_errors={errors} i2c_errors=0 failure_kind=0 failure_error=0 failure_time_us=0 object_min_c=20 object_max_c=30 ambient_min_c=20 ambient_max_c=30\n'


def sensor(reads=12, temperature='25.5', errors=0):
    return stats(10) + f'COMMAND: ir_temp status --machine\nRACOOL_CLI v=1 type=ir_sensor_status object_c={temperature} ambient_c=24.0 measured_time_us=1000000 age_ms=50 polling_ms=100\n' + stats(reads, errors)


def diagnostic(uptime):
    return f'COMMAND: DIAG METRICS --machine\nRACOOL_CLI v=1 type=diagnostic_metrics free_heap=80000 min_free_heap=60000 largest_block=50000 uptime_s={uptime} tasks=12 score=75 known=1\n'


class HealthEvidenceTests(unittest.TestCase):
    def test_fresh_sensor_readback(self):
        self.assertEqual([12, 0, 0], validate(sensor(), 'infrared')['after'])

    def test_stale_measurements_and_errors_fail(self):
        for evidence in [sensor(reads=10), sensor(errors=1)]:
            with self.assertRaises(ValueError):
                validate(evidence, 'infrared')

    def test_nonfinite_and_error_temperatures_fail(self):
        for value in ['nan', 'inf', '-273.15']:
            with self.assertRaises(ValueError):
                validate(sensor(temperature=value), 'infrared')

    def test_missing_or_old_command_evidence_fails(self):
        for evidence in [sensor().replace('COMMAND: ir_temp status', 'COMMAND: ir_info'), stats(10)]:
            with self.assertRaises(ValueError):
                validate(evidence, 'infrared')

    def test_diagnostic_metrics_and_advancing_uptime(self):
        result = validate(diagnostic(10) + diagnostic(12), 'diagnostics')
        self.assertEqual(75, result['snapshots'][1]['Health Score'])
        self.assertIn('not async self-test', result['scope'])

    def test_partial_or_stale_diagnostics_fail(self):
        for evidence in [diagnostic(10) + diagnostic(10), (diagnostic(10) + diagnostic(12)).replace('tasks=', 'missing=')]:
            with self.assertRaises(ValueError):
                validate(evidence, 'diagnostics')

    def test_unrelated_uptime_log_cannot_become_diagnostic_metric(self):
        noise = 'I (123456) PerformanceMonitor: System Uptime: 87911 ms\n'
        result = validate(diagnostic(85) + noise + diagnostic(88), 'diagnostics')
        self.assertEqual(88, result['snapshots'][1]['Uptime'])
        with self.assertRaises(ValueError):
            validate(diagnostic(85) + noise + diagnostic(88).replace('uptime_s=88', ''), 'diagnostics')
    def test_unknown_unsupported_and_incomplete_metrics_fail(self):
        for bad in [diagnostic(12).replace('known=1','known=0'),diagnostic(12).replace('v=1','v=2'),diagnostic(12).rstrip('\n')]:
            with self.assertRaises(ValueError):validate(diagnostic(10)+bad,'diagnostics')


if __name__ == '__main__':
    unittest.main()
