import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("telemetry_menu", Path(__file__).parent / "diagnostic/verify_telemetry_menu.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)

class TelemetryMenuEvidenceTests(unittest.TestCase):
    def test_admission_is_not_worker_or_configuration_evidence(self):
        self.assertIsNone(helper.menu_state("Navigation request queued for state 33"))
        self.assertIsNone(helper.menu_state("Encoder request accepted"))
        self.assertIsNone(helper.interval("Telemetry interval request accepted"))

    def test_complete_latest_menu_status_with_usb_line_endings(self):
        text = "RACOOL_CLI v=1 type=menu_status state=33 registry_label_hex=54656c656d65747269\r\r\nRACOOL_CLI v=1 type=menu_status state=0 registry_label_hex=497a6c656d65\r\r\n"
        self.assertEqual(0,helper.menu_state(text))
        self.assertIsNone(helper.menu_state("RACOOL_CLI v=1 type=menu_status state=7 registry_label_hex=6"))

    def test_local_telemetry_bounds_and_latest_value(self):
        self.assertEqual(1000,helper.interval("RACOOL_CLI v=1 type=config_field key=system.telemetry_interval_ms value=1000"))
        self.assertEqual(600000,helper.interval("RACOOL_CLI v=1 type=config_field key=system.telemetry_interval_ms value=1000\r\r\nRACOOL_CLI v=1 type=config_field key=system.telemetry_interval_ms value=600000"))
        self.assertEqual(0,helper.interval("RACOOL_CLI v=1 type=config_field key=system.telemetry_interval_ms value=0"))
        for value in [999,600001,999999999999999999999]:
            self.assertIsNone(helper.interval(f"RACOOL_CLI v=1 type=config_field key=system.telemetry_interval_ms value={value}"))

    def test_missing_or_malformed_configuration_is_rejected(self):
        for text in ["Telemetry Int : 5000 ms", "RACOOL_CLI v=1 type=config_field key=system.telemetry_interval_ms value=-1000", "RACOOL_CLI v=1 type=config_field key=system.telemetry_interval_ms value=bad", ""]:
            self.assertIsNone(helper.interval(text))

if __name__ == '__main__': unittest.main()
