import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "diagnostic"))
from verify_climate_cpu import parse_override


class CpuOverrideEvidenceTests(unittest.TestCase):
    def test_active_and_inactive_are_distinct(self):
        self.assertFalse(parse_override("RACOOL_CLI v=1 type=climate_cpu override=0 cpu_c=27 warning_c=70 critical_c=80 emergency_c=90 polling_ms=1000\r\r\n"))
        self.assertTrue(parse_override("RACOOL_CLI v=1 type=climate_cpu override=1 cpu_c=27 warning_c=70 critical_c=80 emergency_c=90 polling_ms=1000\n"))

    def test_last_incomplete_result_cannot_use_old_status(self):
        with self.assertRaises(ValueError):
            parse_override("RACOOL_CLI v=1 type=climate_cpu override=0 cpu_c=27 warning_c=70 critical_c=80 emergency_c=90 polling_ms=1000\nRACOOL_CLI v=1 type=climate_cpu\n")
        with self.assertRaises(ValueError):
            parse_override("RACOOL_CLI v=1 type=climate_cpu override=1 cpu_c=27 warning_c=70 critical_c=80 emergency_c=90 polling_ms=1000")


if __name__ == "__main__":
    unittest.main()
