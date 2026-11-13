import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import importlib.util
import unittest

spec = importlib.util.spec_from_file_location("led_menu", Path(__file__).resolve().parents[2] / "Controllers/MenuManager/diagnostic/verify_led_menu.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)

class LEDMenuEvidenceTests(unittest.TestCase):
    def test_ack_is_not_worker_or_menu_evidence(self):
        self.assertIsNone(helper.brightness("Brightness saved."))
        self.assertIsNone(helper.menu_state("Navigated to state 46"))
        self.assertIsNone(helper.menu_state("RACOOL_CLI v=1 type=menu_status state=7 registry_label_hex=6"))
        self.assertIsNone(helper.brightness('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=256 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n'))
    def test_complete_status_is_read(self):
        self.assertEqual(46, helper.menu_state("RACOOL_CLI v=1 type=menu_status state=46 registry_label_hex=446168696c69205061726c616b"))
        self.assertEqual(0, helper.brightness('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=0 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n'))

if __name__ == '__main__': unittest.main()
