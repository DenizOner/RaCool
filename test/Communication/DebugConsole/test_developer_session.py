import unittest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, parse_mode
from Communication.DebugConsole.diagnostic.verify_developer_mode import parse_config_snapshot, require_idle_owners, verify_tools, verify_user_help
from Management.ConfigurationManager.diagnostic.verify_system_config import read_field


class DeveloperSessionTests(unittest.TestCase):
    def test_rejects_missing_and_duplicate_mode_response(self):
        with self.assertRaises(RuntimeError):
            parse_mode("no response")
        with self.assertRaises(RuntimeError):
            parse_mode("RACOOL_CLI v=1 type=developer_mode enabled=0\n" * 2)

    def test_restores_only_mode_it_enabled(self):
        state = [False]
        commands = []

        def exchange(command):
            commands.append(command)
            if " ON " in command:
                state[0] = True
            elif " OFF " in command:
                state[0] = False
            return f"RACOOL_CLI v=1 type=developer_mode enabled={int(state[0])}\n"

        session = DeveloperSession(exchange)
        session.open()
        self.assertTrue(state[0])
        session.close()
        self.assertFalse(state[0])
        self.assertEqual(1, commands.count("DEVELOPER ON --machine"))
        self.assertEqual(1, commands.count("DEVELOPER OFF --machine"))
        state[0] = True
        commands.clear()
        session.open()
        session.close()
        self.assertTrue(state[0])
        self.assertFalse(any(" OFF " in item for item in commands))

    def test_reboot_preserves_preference_and_restores_original(self):
        state = [True]

        def exchange(command):
            if " ON " in command:
                state[0] = True
            elif " OFF " in command:
                state[0] = False
            return f"RACOOL_CLI v=1 type=developer_mode enabled={int(state[0])}\n"

        session = DeveloperSession(exchange)
        session.open()
        session.after_boot()
        self.assertTrue(state[0])
        session.close()
        self.assertTrue(state[0])

    def test_detects_foreign_change_without_overwriting_it(self):
        state = [True]
        commands = []
        def exchange(command):
            commands.append(command)
            return f"RACOOL_CLI v=1 type=developer_mode enabled={int(state[0])}\n"
        session = DeveloperSession(exchange)
        session.open()
        state[0] = False
        with self.assertRaisesRegex(RuntimeError, 'outside this session'):
            session.close()
        self.assertEqual(['DEVELOPER STATUS --machine'] * 2, commands)

    def test_reboot_keeps_temporary_preference_until_owner_restores_it(self):
        state = [False]
        def exchange(command):
            if ' ON ' in command: state[0] = True
            if ' OFF ' in command: state[0] = False
            return f"RACOOL_CLI v=1 type=developer_mode enabled={int(state[0])}\n"
        session = DeveloperSession(exchange)
        session.open()
        session.after_boot()
        session.close()
        self.assertFalse(state[0])

    def test_live_help_evidence_requires_visibility_and_operation_classes(self):
        verify_user_help('  DEVELOPER : Developer Mode\n  LANGUAGE : Language\n')
        with self.assertRaises(ValueError):
            verify_user_help('  DEVELOPER : Developer Mode\n  LOGS : logs\n')
        verify_tools('Developer Tools\n Read-only:\n  MEM : memory\n Controls (may change state):\n')
        with self.assertRaises(ValueError):
            verify_tools('Developer Tools\n MEM : memory\n')

    def test_configuration_baseline_requires_one_complete_unique_snapshot(self):
        one = 'RACOOL_CLI v=1 type=config_field key=system.display_mode value=2\nRACOOL_CLI v=1 type=config_snapshot fields=1\n'
        self.assertEqual({'system.display_mode': '2'}, parse_config_snapshot(one))
        with self.assertRaises(ValueError): parse_config_snapshot(one + one)
        with self.assertRaises(ValueError): parse_config_snapshot(one.replace('fields=1', 'fields=2'))

    def test_brightness_readback_uses_machine_field(self):
        self.assertEqual(127, read_field('RACOOL_CLI v=1 type=config_field key=hardware.ext_led_brightness value=127\n', 'brightness'))
        self.assertIsNone(read_field('RGB Brightness: 127\n', 'brightness'))

    def test_reboot_preflight_refuses_foreign_menu_and_recovery(self):
        menu = 'RACOOL_CLI v=1 type=menu_status state=0 registry_label_hex=74657374\n'
        state = 'RACOOL_CLI v=1 type=system_status state=3\n'
        require_idle_owners(menu, state)
        with self.assertRaises(RuntimeError): require_idle_owners(menu.replace('state=0', 'state=1'), state)
        with self.assertRaises(RuntimeError): require_idle_owners(menu, state.replace('state=3', 'state=6'))
        with self.assertRaises(RuntimeError): require_idle_owners(menu + menu, state)


if __name__ == "__main__":
    unittest.main()
