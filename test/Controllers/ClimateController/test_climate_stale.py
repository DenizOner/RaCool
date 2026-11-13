import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "diagnostic"))
from verify_climate_stale import parse_status

SAMPLE = "RACOOL_CLI v=1 type=climate_status mode=0 on=1 stale=1 debouncing=0 ir_tick=4294967294 object_c=24.00 ambient_c=23.00 cpu_c=27.00 open_c=30.00 close_c=28.00 object_cal_c=0.10 ambient_cal_c=-0.20 enabled=1 manual=0 manual_state=0 strategy=0 object_source=0 ambient_source=0 delta_on=20 delta_off=10\n"



class StaleEvidenceTests(unittest.TestCase):
    def test_full_timestamp_and_state(self):
        status = parse_status(SAMPLE.replace("\n", "\r\r\n"))
        self.assertEqual(4294967294, status["tick"])
        self.assertTrue(status["stale"])
        self.assertTrue(status["on"])
        self.assertFalse(status["debouncing"])

    def test_latest_incomplete_state_cannot_use_old_results(self):
        with self.assertRaises(ValueError):
            parse_status(SAMPLE + "RACOOL_CLI v=1 type=climate_status mode=0\n")
        with self.assertRaises(ValueError):
            parse_status(SAMPLE.replace("stale=1", "stale=unknown"))

    def test_machine_units_are_celsius_and_unavailable_values_are_rejected(self):
        status=parse_status(SAMPLE)
        self.assertEqual(24,status["radiator"])
        self.assertEqual(27,status["cpu"])
        for text in [SAMPLE.replace("v=1","v=2"),SAMPLE.rstrip("\n"),SAMPLE.replace("cpu_c=27.00","cpu_c=nan"),SAMPLE.replace("mode=0","mode=-1")]:
            with self.assertRaises(ValueError):parse_status(text)



if __name__ == "__main__":
    unittest.main()
