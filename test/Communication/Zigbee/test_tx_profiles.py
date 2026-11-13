import importlib.util
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Communication/Zigbee/diagnostic"))
from verify_tx_profiles import parse_profile, POWERS


class TxEvidenceTests(unittest.TestCase):
    def test_presets_match_sdk_power(self):
        for profile, power in {"Eco": 0, "Normal": 10, "Boost": 20}.items():
            self.assertEqual(parse_profile(f"RACOOL_CLI v=1 type=zigbee_tx profile={tuple(POWERS).index(profile)} dbm={power}\n"),
                             {"profile": profile, "dbm": power})

    def test_incomplete_wrong_and_old_responses_fail(self):
        for text in ("", "RACOOL_CLI v=1 type=zigbee_tx profile=0 dbm=10\n",
                     "RACOOL_CLI v=1 type=zigbee_tx profile=2 dbm=20",
                     "RACOOL_CLI v=1 type=zigbee_tx profile=1 dbm=10\nRACOOL_CLI v=1 type=zigbee_tx profile=?\n"):
            with self.assertRaises(ValueError):
                parse_profile(text)


if __name__ == "__main__":
    unittest.main()
