"""Exercise live-profile verification and restoration without connected hardware."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))


from Management.PowerManager.diagnostic import verify_power_profiles as helper
import json
import tempfile
import unittest
from unittest.mock import patch


class FakePort:
    def __init__(self, fail_selftest=False, lose_profile=False, unknown=False, sensor_error=False):
        self.mode = -1 if unknown else 1
        self.fail_selftest = fail_selftest
        self.lose_profile = lose_profile
        self.commands = []
        self.chunks = []
        self.sensor_reads = 0
        self.sensor_error = sensor_error
        self.developer = False
    def __enter__(self): return self
    def __exit__(self, *args): return False
    @property
    def in_waiting(self): return len(self.chunks[0]) if self.chunks else 0
    def reset_input_buffer(self): self.chunks.clear()
    def read(self, count): return self.chunks.pop(0) if self.chunks else b''
    def write(self, data):
        text = data.decode().strip()
        self.commands.append(text)
        if text.startswith('DEVELOPER '):
            action = text.split()[1]
            if action == 'ON': self.developer = True
            elif action == 'OFF': self.developer = False
            response = f'RACOOL_CLI v=1 type=developer_mode enabled={int(self.developer)}\n'
        elif text == 'power status --machine':
            response = f'RACOOL_CLI v=1 type=power_status mode={self.mode} locks=0 sleeps=0 wakeups=0 restarts=0 uptime_s=5\n'
        elif text.startswith('power mode '):
            baseline = json.loads(self.evidence_path.read_text(encoding='utf-8'))
            assert baseline['original'] == 1, 'Initial profile must be saved before mutable writes'
            self.mode = {'perf': 0, 'balanced': 1, 'lowpower': 2}[text.split()[2]]
            response = f'RACOOL_CLI v=1 type=power_mode result=applied mode={self.mode} error=0\n'
        elif text == 'power selftest --machine':
            if self.fail_selftest and self.mode == 2:
                self.fail_selftest = False
                response = 'RACOOL_CLI v=1 type=power_selftest result=failed error=-1\n'
            else: response = 'RACOOL_CLI v=1 type=power_selftest result=passed error=0\n'
        elif text == 'ir_temp selftest --machine': response = 'RACOOL_CLI v=1 type=ir_sensor_test result=passed error=0\n'
        elif text == 'ir_temp stats --machine':
            self.sensor_reads += 10
            errors = 1 if self.sensor_error and self.sensor_reads >= 20 else 0
            response = f'RACOOL_CLI v=1 type=ir_sensor_stats reads={self.sensor_reads} pec_errors=0 i2c_errors={errors} failure_kind=0 failure_error=0 failure_time_us=0 object_min_c=20 object_max_c=30 ambient_min_c=20 ambient_max_c=30\n'
        elif text == 'watchdog status --machine':
            response = 'RACOOL_CLI v=1 type=watchdog_status active=1 timeout_ms=5000 registered=3 warnings=0 timeouts=0 last_timeout_ms=0\n'
        elif text == 'ZB INFO': response = 'Rol : Router\n'
        elif text == 'ZB DIAG --machine': response = 'RACOOL_CLI v=1 type=zigbee_status state=2 connected=1 endpoints=10\n'
        elif text == 'menu status --machine': response = 'RACOOL_CLI v=1 type=menu_status state=0 registry_label_hex=74657374\n'
        elif text == 'sys status --machine': response = 'RACOOL_CLI v=1 type=system_status state=0\n'
        elif text == 'power restart 500 --machine':
            if self.lose_profile: self.mode = 1
            self.chunks = [b'RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0\n', b'ESP-ROM: fresh\nSW_CPU\nSYSTEM INITIALIZATION COMPLETE\nracool> ']
            return
        else: raise AssertionError(text)
        self.chunks = [response.encode()]

class VerifyPowerProfilesTests(unittest.TestCase):
    def run_helper(self, port, expected_error=False):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'evidence'
            port.evidence_path = output.with_suffix('.json')
            ticks = iter(i / 10 for i in range(10000))
            with patch.object(sys, 'argv', ['verify_power_profiles.py', '--port', 'FAKE', '--output', str(output)]), \
                 patch.object(helper.serial, 'Serial', return_value=port), \
                 patch.object(helper.time, 'sleep'), \
                 patch.object(helper.time, 'monotonic', side_effect=lambda: next(ticks)):
                if expected_error:
                    with self.assertRaises(RuntimeError): helper.main()
                else: helper.main()
            return json.loads(output.with_suffix('.json').read_text())
    def test_success_requires_restart_and_restores_original(self):
        port = FakePort()
        result = self.run_helper(port)
        self.assertTrue(result['completed'])
        self.assertEqual(2, result['restart_profile'])
        self.assertEqual(1, result['restored'])
        self.assertEqual(1, port.mode)
    def test_async_rejoin_retries_only_read_only_query(self):
        class DelayedRouter(FakePort):
            waiting = True
            def write(self, data):
                if data.strip() == b'ZB INFO' and 'power restart 500 --machine' in self.commands and self.waiting:
                    self.waiting = False
                    self.commands.append('ZB INFO')
                    self.chunks = [b'Zigbee not connected yet\n']
                else:
                    super().write(data)
        port = DelayedRouter()
        result = self.run_helper(port)
        self.assertTrue(result['completed'])
        self.assertEqual(5, port.commands.count('ZB INFO'))
        self.assertEqual(1, port.commands.count('power restart 500 --machine'))
        self.assertEqual(1, port.mode)
    def test_usb_disconnect_after_restart_reconnects_without_resending(self):
        class ReconnectingPort(FakePort):
            reconnects = 0
            disconnected = False
            def read(self, count):
                if self.chunks and self.chunks[0].startswith(b'ESP-ROM:') and not self.disconnected:
                    self.disconnected = True
                    raise OSError('USB re-enumeration')
                return super().read(count)
            def close(self): pass
            def open(self): self.reconnects += 1
        port = ReconnectingPort()
        result = self.run_helper(port)
        self.assertTrue(result['completed'])
        self.assertEqual(1, port.reconnects)
        self.assertEqual(1, port.commands.count('power restart 500 --machine'))
        self.assertEqual(1, port.mode)
    def test_selftest_failure_restores_original_and_fails(self):
        port = FakePort(fail_selftest=True)
        result = self.run_helper(port, True)
        self.assertFalse(result['completed'])
        self.assertEqual(1, result['restored'])
        self.assertEqual(1, port.mode)
    def test_lost_persistence_is_not_accepted(self):
        port = FakePort(lose_profile=True)
        result = self.run_helper(port, True)
        self.assertFalse(result['completed'])
        self.assertEqual(1, result['restored'])
    def test_unknown_original_prevents_mutation(self):
        port = FakePort(unknown=True)
        result = self.run_helper(port, True)
        self.assertFalse(result['completed'])
        self.assertEqual(['power status --machine'], [cmd for cmd in port.commands if not cmd.startswith('DEVELOPER ')])
    def test_sensor_error_growth_fails_and_restores(self):
        port = FakePort(sensor_error=True)
        result = self.run_helper(port, True)
        self.assertFalse(result['completed'])
        self.assertEqual(1, result['restored'])

if __name__ == '__main__': unittest.main()
