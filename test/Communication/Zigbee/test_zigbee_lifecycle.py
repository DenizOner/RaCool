"""Reject SDK lifecycle false positives even when wrapper calls return zero."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import unittest
import json
import tempfile
import io
from unittest.mock import patch
from Communication.Zigbee.diagnostic.verify_zigbee_lifecycle import lifecycle_result, report_values, reject_runtime_errors
from Communication.Zigbee.diagnostic import verify_zigbee_lifecycle as lifecycle


class ZigbeeLifecycleEvidenceTests(unittest.TestCase):
    valid = 'RACOOL_CLI v=1 type=zigbee_lifecycle operation=stop result=0 state=3\nRACOOL_CLI v=1 type=zigbee_lifecycle operation=start result=0 state=2\n'

    def test_complete_owner_acknowledgments(self):
        self.assertEqual(lifecycle_result(self.valid)['start_state'], 2)

    def test_hidden_sdk_errors_are_not_success(self):
        for suffix in ('E (14079) ieee802154: MAC sleep deinit failed\n',
                       'racool> ZB\x1b[31mE (21) Zigbee: failed\x1b[0m\n',
                       'ESP-ROM:esp32c6\n', 'Guru Meditation'):
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                lifecycle_result(self.valid + suffix)

    def test_incomplete_duplicate_failed_and_wrong_state(self):
        for text in ('', self.valid.splitlines()[0], self.valid * 2,
                     self.valid.replace('result=0 state=3', 'result=-3 state=2'),
                     self.valid.replace('state=2', 'state=4')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                lifecycle_result(text)

    def test_reporting_snapshot_requires_all_six(self):
        names = ('fan','ambient','cpu','health','memory','uptime')
        text = '\n'.join('RACOOL_CLI v=1 type=report_interval report=' + name + ' seconds=0 enabled=0' for name in names)
        self.assertEqual(report_values(text), dict.fromkeys(names, 0))
        with self.assertRaises(ValueError): report_values(text.rsplit('\n', 1)[0])

    def test_late_errors_between_commands_are_rejected(self):
        reject_runtime_errors('I (20) Zigbee: joined\n')
        with self.assertRaises(ValueError):
            reject_runtime_errors('I (20) Zigbee: joined\nE (21) ieee802154: init failed\n')
        with self.assertRaises(ValueError):
            reject_runtime_errors('E (21) Zigbee: incomplete')

    def test_observation_allows_only_known_retryable_backpressure(self):
        known = ('E (1) Zigbee: Attribute report request rejected: key=1 sdk_error=1\n'
                 'E (2) Zigbee: Queued attribute failed: key=1 status=137 retry=1\n')
        self.assertEqual(2, len(reject_runtime_errors(known, allow_report_backpressure=True)))
        with self.assertRaises(ValueError): reject_runtime_errors(known)
        for line in (known.replace('sdk_error=1', 'sdk_error=-1'),
                     known.replace('retry=1', 'retry=0'), 'E (3) Zigbee: other\n'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                reject_runtime_errors(line, allow_report_backpressure=True)
        selftest = 'E (4) DiagnosticsManager: [SelfTestTask] Service FAILED: Zigbee result=-1 elapsed_us=6 budget_ms=100\n'
        self.assertEqual(1, len(reject_runtime_errors(selftest, allow_restart_selftest=True)))
        with self.assertRaises(ValueError): reject_runtime_errors(selftest)
        with self.assertRaises(ValueError):
            reject_runtime_errors(selftest.replace('Zigbee', 'OLED'), allow_restart_selftest=True)

    def test_restoration_records_error_but_never_ignores_panic(self):
        text='racool> ZB\x1b[31mE (21) Zigbee: failed\x1b[0m\n'
        self.assertEqual(['E (21) Zigbee: failed'], reject_runtime_errors(text, allow_logged_errors=True))
        for fatal in ('Guru Meditation', 'ESP-ROM:esp32c6', 'assert failed'):
            with self.subTest(fatal=fatal), self.assertRaises(ValueError):
                reject_runtime_errors(fatal, allow_logged_errors=True)

    def test_restoration_error_is_recorded_and_mode_is_restored(self):
        class SerialFake:
            def __init__(self, **kwargs):
                self.dtr = False
                self._rts = False
                self.reset_pulses = 0
                self.mode = False
                self.pending = b''
                self.busy_after = None
                self.menu_reads = 0
                self.inject_restore_error = True
                self.inject_restart_error = False
                self.inject_restart_panic = False
                self.inject_backpressure = False
                self.inject_selftest = False
                self.last_command = ''
                self.restart_count = 0
            @property
            def rts(self): return self._rts
            @rts.setter
            def rts(self, value):
                if value: self.reset_pulses += 1
                self._rts = value
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def reset_input_buffer(self): self.pending = b''
            @property
            def in_waiting(self): return len(self.pending)
            def read(self, count):
                data, self.pending = self.pending[:count], self.pending[count:]
                return data
            def write(self, data):
                command = data.decode().strip()
                self.last_command = command
                if command == 'DEVELOPER ON --machine': self.mode = True
                if command == 'DEVELOPER OFF --machine': self.mode = False
                if command.startswith('DEVELOPER '):
                    prefix = 'racool> X\x1b[31mE (4) Zigbee: late fault\x1b[0m\n' if command == 'DEVELOPER OFF --machine' and self.inject_restore_error else ''
                    response = prefix + f'RACOOL_CLI v=1 type=developer_mode enabled={int(self.mode)}\n'
                elif command == 'menu status --machine':
                    self.menu_reads += 1
                    state = int(self.busy_after is not None and self.menu_reads >= self.busy_after)
                    response = f'RACOOL_CLI v=1 type=menu_status state={state} registry_label_hex=78\n'
                elif command == 'sys status --machine': response = 'RACOOL_CLI v=1 type=system_status state=0\n'
                elif command == 'ZB DIAG --machine': response = 'RACOOL_CLI v=1 type=zigbee_status state=2 connected=1 endpoints=10\n'
                elif command == 'ZB REPORT --machine':
                    response = '\n'.join('RACOOL_CLI v=1 type=report_interval report=' + name + ' seconds=0 enabled=0' for name in ('fan','ambient','cpu','health','memory','uptime')) + '\n'
                elif command == 'ZB RESTART --machine':
                    self.restart_count += 1
                    if self.inject_restart_panic:
                        response = 'Guru Meditation\nESP-ROM:esp32c6\nSYSTEM INITIALIZATION COMPLETE\napp_main: System ready.\nracool>\n'
                    else:
                        response = ('E (7) Zigbee: Attribute report request rejected: key=1 sdk_error=1\n' if self.inject_backpressure else '') + ('E (8) DiagnosticsManager: [SelfTestTask] Service FAILED: Zigbee result=-1 elapsed_us=6 budget_ms=100\n' if self.inject_selftest else '') + ('\x1b[31mE (7) Zigbee: transient report\x1b[0m\n' if self.inject_restart_error else '') + ZigbeeLifecycleEvidenceTests.valid
                else: raise AssertionError(command)
                self.pending += response.encode()
                return len(data)
        fake = SerialFake()
        now = iter(i * .1 for i in range(100000))
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'evidence')
            with patch.object(lifecycle.serial, 'Serial', return_value=fake), \
                 patch.object(lifecycle.time, 'monotonic', side_effect=lambda: next(now)), \
                 patch.object(sys, 'stdout', new_callable=io.StringIO), \
                 patch.object(sys, 'argv', ['verify_zigbee_lifecycle.py', '--port', 'fake', '--output', output]):
                self.assertEqual(1, lifecycle.main())
            evidence = json.loads(Path(output + '.json').read_text(encoding='utf-8'))
        self.assertFalse(fake.mode)
        self.assertFalse(evidence['completed'])
        self.assertEqual(['E (4) Zigbee: late fault'], evidence['restoration_runtime_errors'])
        self.assertEqual(3, len(evidence['cycles']))

        fake = SerialFake()
        fake.busy_after = 3
        fake.inject_restore_error = False
        now = iter(i * .1 for i in range(100000))
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'evidence')
            with patch.object(lifecycle.serial, 'Serial', return_value=fake), \
                 patch.object(lifecycle.time, 'monotonic', side_effect=lambda: next(now)), \
                 patch.object(sys, 'stdout', new_callable=io.StringIO), \
                 patch.object(sys, 'argv', ['verify_zigbee_lifecycle.py', '--port', 'fake', '--output', output]):
                self.assertEqual(1, lifecycle.main())
            evidence = json.loads(Path(output + '.json').read_text(encoding='utf-8'))
        self.assertFalse(fake.mode)
        self.assertEqual(1, len(evidence['cycles']))
        self.assertEqual(0, fake.reset_pulses)
        self.assertIn('foreign menu state', evidence['error'])

        fake = SerialFake()
        fake.inject_restore_error = False
        now = iter(i * .1 for i in range(100000))
        original_reject = lifecycle.reject_runtime_errors
        def fail_before_restart(text, allow_logged_errors=False, allow_report_backpressure=False, allow_restart_selftest=False):
            if not text and not allow_logged_errors and fake.menu_reads == 2 and fake.last_command == 'sys status --machine':
                raise ValueError('late error before write')
            return original_reject(text, allow_logged_errors=allow_logged_errors,
                                   allow_report_backpressure=allow_report_backpressure,
                                   allow_restart_selftest=allow_restart_selftest)
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'evidence')
            with patch.object(lifecycle.serial, 'Serial', return_value=fake), \
                 patch.object(lifecycle.time, 'monotonic', side_effect=lambda: next(now)), \
                 patch.object(lifecycle, 'reject_runtime_errors', side_effect=fail_before_restart), \
                 patch.object(sys, 'stdout', new_callable=io.StringIO), \
                 patch.object(sys, 'argv', ['verify_zigbee_lifecycle.py', '--port', 'fake', '--output', output]):
                self.assertEqual(1, lifecycle.main())
            evidence = json.loads(Path(output + '.json').read_text(encoding='utf-8'))
        self.assertFalse(fake.mode)
        self.assertEqual(0, fake.restart_count)
        self.assertEqual(0, fake.reset_pulses)
        self.assertEqual('late error before write', evidence['error'])

        fake = SerialFake()
        fake.inject_restore_error = False
        fake.inject_restart_error = True
        now = iter(i * .1 for i in range(100000))
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'evidence')
            with patch.object(lifecycle.serial, 'Serial', return_value=fake), \
                 patch.object(lifecycle.time, 'monotonic', side_effect=lambda: next(now)), \
                 patch.object(sys, 'stdout', new_callable=io.StringIO), \
                 patch.object(sys, 'argv', ['verify_zigbee_lifecycle.py', '--port', 'fake', '--output', output]):
                self.assertEqual(1, lifecycle.main())
            evidence = json.loads(Path(output + '.json').read_text(encoding='utf-8'))
        self.assertFalse(fake.mode)
        self.assertEqual(1, fake.restart_count)
        self.assertEqual(0, fake.reset_pulses)
        self.assertIn('Runtime error', evidence['error'])

        fake = SerialFake()
        fake.inject_restore_error = False
        fake.inject_backpressure = True
        now = iter(i * .1 for i in range(100000))
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'evidence')
            with patch.object(lifecycle.serial, 'Serial', return_value=fake), \
                 patch.object(lifecycle.time, 'monotonic', side_effect=lambda: next(now)), \
                 patch.object(sys, 'stdout', new_callable=io.StringIO), \
                 patch.object(sys, 'argv', ['verify_zigbee_lifecycle.py', '--port', 'fake', '--output', output, '--observe-report-backpressure']):
                self.assertEqual(1, lifecycle.main())
            evidence = json.loads(Path(output + '.json').read_text(encoding='utf-8'))
        self.assertTrue(evidence['lifecycle_completed'])
        self.assertFalse(evidence['completed'])
        self.assertEqual(3, len(evidence['cycles']))
        self.assertEqual(3, len(evidence['observed_restart_errors']))
        self.assertFalse(fake.mode)
        self.assertEqual(0, fake.reset_pulses)

        fake = SerialFake()
        fake.inject_restore_error = False
        fake.inject_selftest = True
        now = iter(i * .1 for i in range(100000))
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'evidence')
            with patch.object(lifecycle.serial, 'Serial', return_value=fake), \
                 patch.object(lifecycle.time, 'monotonic', side_effect=lambda: next(now)), \
                 patch.object(sys, 'stdout', new_callable=io.StringIO), \
                 patch.object(sys, 'argv', ['verify_zigbee_lifecycle.py', '--port', 'fake', '--output', output, '--observe-restart-selftest']):
                self.assertEqual(1, lifecycle.main())
            evidence = json.loads(Path(output + '.json').read_text(encoding='utf-8'))
        self.assertTrue(evidence['lifecycle_completed'])
        self.assertFalse(evidence['completed'])
        self.assertEqual(3, len(evidence['observed_restart_errors']))
        self.assertEqual(0, fake.reset_pulses)
        self.assertFalse(fake.mode)

        fake = SerialFake()
        fake.inject_restore_error = False
        fake.inject_restart_panic = True
        now = iter(i * .1 for i in range(100000))
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'evidence')
            with patch.object(lifecycle.serial, 'Serial', return_value=fake), \
                 patch.object(lifecycle.time, 'monotonic', side_effect=lambda: next(now)), \
                 patch.object(sys, 'stdout', new_callable=io.StringIO), \
                 patch.object(sys, 'argv', ['verify_zigbee_lifecycle.py', '--port', 'fake', '--output', output]):
                self.assertEqual(1, lifecycle.main())
            evidence = json.loads(Path(output + '.json').read_text(encoding='utf-8'))
        self.assertFalse(fake.mode)
        self.assertEqual(1, fake.restart_count)
        self.assertEqual(0, fake.reset_pulses)
        self.assertEqual('observed', evidence['recovery_boot'])
        self.assertTrue(evidence['recovery_verified'])


if __name__ == '__main__': unittest.main()
