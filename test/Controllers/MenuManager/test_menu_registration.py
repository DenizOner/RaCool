import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import importlib.util
import unittest

root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('menu_registration', root / 'test/Controllers/MenuManager/diagnostic/verify_menu_registration.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)

class RegistrationEvidenceTests(unittest.TestCase):
    def test_command_ack_does_not_prove_registry(self):
        self.assertIsNone(helper.parse_status('Navigated to state 7'))
        self.assertIsNone(helper.parse_status("RACOOL_CLI v=1 type=menu_status state=7 registry_label_hex=6"))
        self.assertEqual((7, 'Temp Low'), helper.parse_status('RACOOL_CLI v=1 type=menu_status state=7 registry_label_hex=54656d70204c6f77'))
    def test_groups_resolve_from_current_source_without_duplicate_states(self):
        entries = helper.registered_entries(root)
        self.assertTrue(entries)
        self.assertEqual(len(entries), len({entry['state'] for entry in entries}))
        self.assertTrue(all(entry['label'] and entry['source'] for entry in entries))
    def test_visibility_requires_one_matching_machine_response(self):
        line = 'RACOOL_CLI v=1 type=menu_visibility state=50 visible=0\n'
        self.assertFalse(helper.parse_visibility(line, 50))
        with self.assertRaises(RuntimeError): helper.parse_visibility(line, 49)
        with self.assertRaises(RuntimeError): helper.parse_visibility(line + line, 50)

if __name__ == '__main__': unittest.main()
