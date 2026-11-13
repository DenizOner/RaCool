import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import importlib.util
import os
import unittest
from unittest.mock import patch


class HardwareDiagnosticsTest(unittest.TestCase):
    def load_helper(self):
        spec = importlib.util.spec_from_file_location('hardware_diagnostics',
            Path(__file__).parent / 'test_hardware_revisions.py')
        module = importlib.util.module_from_spec(spec)
        original_stdout = sys.stdout
        spec.loader.exec_module(module)
        self.assertIs(original_stdout, sys.stdout)
        return module

    def test_import_is_inert_and_missing_port_rejects_before_execution(self):
        helper = self.load_helper()
        with patch.dict(os.environ, {}, clear=True), patch.object(helper.subprocess, 'run') as run:
            with self.assertRaises(SystemExit) as error:
                helper.run_test([])
            self.assertEqual(2, error.exception.code)
            run.assert_not_called()

    def test_selected_port_and_only_read_only_commands_reach_capture(self):
        helper = self.load_helper()
        with patch.dict(os.environ, {'RACOOL_SERIAL_PORT': 'LOCAL_TEST_PORT'}), \
                patch.object(helper.subprocess, 'run') as run:
            run.return_value.returncode = 0
            self.assertEqual(0, helper.run_test(['--output', 'capture.log']))
            command = run.call_args.args[0]
            self.assertEqual('LOCAL_TEST_PORT', command[command.index('--port') + 1])
            commands = [command[index + 1] for index, value in enumerate(command) if value == '--command']
            self.assertTrue(Path(command[1]).is_file())
            self.assertIn('--developer', command)
            self.assertEqual(['power status', 'ZB INFO', 'CC STATUS', 'menu status --machine'], commands)
            self.assertNotIn('--hardware-reset', command)

    def test_explicit_port_overrides_environment_and_failure_is_not_success(self):
        helper = self.load_helper()
        with patch.dict(os.environ, {'RACOOL_SERIAL_PORT': 'OLD_TEST_PORT'}), \
                patch.object(helper.subprocess, 'run') as run:
            run.return_value.returncode = 7
            self.assertEqual(7, helper.run_test(['--port', 'NEW_TEST_PORT']))
            command = run.call_args.args[0]
            self.assertEqual('NEW_TEST_PORT', command[command.index('--port') + 1])


if __name__ == '__main__':
    unittest.main()
