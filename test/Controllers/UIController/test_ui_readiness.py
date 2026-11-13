import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import unittest
from Controllers.UIController.diagnostic.ui_readiness import boot_state, wait_for_ui_ready


class ReadinessTests(unittest.TestCase):
    def test_missing_or_truncated_status_is_not_ready(self):
        for text in ('Screen State: ACTIVE', 'Boot Animation: ID', 'Boot Animation: IDLE',
                     'RACOOL_CLI v=1 type=ui screen=active service=2 boot=idle',
                     'RACOOL_CLI v=2 type=ui screen=active service=2 boot=idle\n'):
            self.assertIsNone(boot_state(text))

    def test_busy_then_ready_only_uses_readonly_commands(self):
        states = iter(('RUNNING', 'RUNNING', 'IDLE'))
        calls = []
        def command(text, accept):
            calls.append(text)
            response = 'RACOOL_CLI v=1 type=ui screen=active service=2 boot=' + next(states).lower() + '\n'
            self.assertTrue(accept(response))
            return response
        wait_for_ui_ready(command, pause=lambda _: None)
        self.assertEqual(['UI STATUS --machine'] * 3, calls)

    def test_idle_before_service_start_is_not_ready(self):
        with self.assertRaises(RuntimeError):
            wait_for_ui_ready(lambda *_: 'RACOOL_CLI v=1 type=ui screen=active service=1 boot=idle\n',
                              attempts=1, pause=lambda _: None)

    def test_busy_is_bounded(self):
        calls = []
        def command(text, accept):
            calls.append(text)
            return 'RACOOL_CLI v=1 type=ui screen=active service=2 boot=running\n'
        with self.assertRaises(RuntimeError):
            wait_for_ui_ready(command, attempts=3, pause=lambda _: None)
        self.assertEqual(3, len(calls))

    def test_old_ready_record_cannot_override_latest_busy_or_stopped(self):
        ready='RACOOL_CLI v=1 type=ui screen=active service=2 boot=idle\n'
        for last in ('service=2 boot=running', 'service=1 boot=idle'):
            response=ready+'RACOOL_CLI v=1 type=ui screen=active '+last+'\n'
            with self.subTest(last=last),self.assertRaises(RuntimeError):
                wait_for_ui_ready(lambda *_: response,attempts=1,pause=lambda _:None)

if __name__ == '__main__': unittest.main()
