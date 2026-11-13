import unittest
from diagnostic.verify_monitor_cli import parse_state, verify

class MonitorVerificationTests(unittest.TestCase):
    def session(self, initial=2, fail_start=False, wrong_ack=False):
        state = {'value': initial, 'fail_once': fail_start}
        def command(text, expected):
            if text == 'perf_status --machine': return 'RACOOL_CLI v=1 type=performance_status state=' + str(state['value'])
            if text == 'perf_start --machine' and state['fail_once']:
                state['fail_once'] = False
                raise RuntimeError('injected start failure')
            if not wrong_ack: state['value'] = 2 if text == 'perf_start --machine' else 3
            return 'ack'
        return command, state
    def test_valid_cycle_restores_running(self):
        command, state = self.session()
        result = verify(command)
        self.assertTrue(result['completed']); self.assertEqual(2, state['value'])
    def test_failure_still_restores(self):
        command, state = self.session(fail_start=True)
        result = verify(command)
        self.assertFalse(result['completed']); self.assertTrue(result['restoration_verified'])
        self.assertEqual(2, state['value'])
    def test_ack_is_not_actuation(self):
        command, state = self.session(wrong_ack=True)
        self.assertFalse(verify(command)['completed'])
    def test_machine_parser_rejects_human_and_foreign_versions(self):
        for value in ('State : 2', 'RACOOL_CLI v=2 type=performance_status state=2', 'RACOOL_CLI v=1 type=performance_status state=2 extra=1', 'RACOOL_CLI v=1 type=performance_status state=0'):
            with self.assertRaises(RuntimeError): parse_state(value)
        self.assertEqual(2, parse_state('log\nRACOOL_CLI v=1 type=performance_status state=2\r\n'))

    def test_unstable_or_uninitialized_state_rejected(self):
        for value in ('', 'State : 0', 'State : 4'):
            with self.assertRaises(RuntimeError): parse_state(value)
        command, state = self.session(initial=1)
        self.assertFalse(verify(command)['completed']); self.assertEqual(1, state['value'])

if __name__ == '__main__': unittest.main()
