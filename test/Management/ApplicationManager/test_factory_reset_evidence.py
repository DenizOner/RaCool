"""Reject incomplete reset/boot artifacts without touching hardware."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from Management.ApplicationManager.diagnostic.factory_reset_evidence import validate


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.reset = "COMMAND: app reset_factory\n"
        for phase in range(2, 8):
            if phase == 7:
                self.reset += "[ZB-SIGNAL] Signal: 0x0003 (ZDO Leave), Status: ESP_OK (0x0)\n"
            self.reset += f"[FACTORY_RESET] id=1 phase={phase} error=0\n"
        self.reset += "ESP-ROM:esp32c6\nrst:0xc (SW_CPU)\nfactory_new=Yes\nWaiting for user pairing command\n"
        self.after = "All 7 persisted and runtime configuration blocks match defaults.\n"
        self.after += "Current Mode : BALANCED (DFS 80-160MHz) (1)\nPASSED: PowerManager self-test OK\n"
        self.after += "Zigbee TX profile: Normal; SDK power: 10 dBm\n"

    def check(self, reset=None, after=None):
        return validate(self.reset if reset is None else reset, self.after if after is None else after,
                        cpu_profile=1, tx_profile="Normal", tx_dbm=10)

    def test_complete_boot_and_optional_final_log(self):
        self.assertTrue(self.check()["completed"])
        self.assertTrue(self.check(self.reset.replace("ESP-ROM:", "[FACTORY_RESET] id=1 phase=8 error=0\nESP-ROM:"))["completed"])

    def test_missing_or_duplicate_command_or_boot(self):
        for reset in (self.reset.replace("COMMAND:", "UNKNOWN:"), "COMMAND: app reset_factory\n" + self.reset,
                      self.reset.replace("ESP-ROM:", "MISSED-ROM:"), self.reset + "ESP-ROM:loop\n"):
            with self.subTest(reset=reset), self.assertRaises(ValueError): self.check(reset)

    def test_failed_missing_or_mixed_phase(self):
        for reset in (self.reset.replace("phase=7 error=0", "phase=9 error=263"),
                      self.reset.replace("phase=4 error=0", "phase=3 error=0"),
                      self.reset.replace("id=1 phase=6", "id=2 phase=6")):
            with self.subTest(reset=reset), self.assertRaises(ValueError): self.check(reset)

    def test_leave_and_software_factory_boot_are_required(self):
        for before, after in (("Status: ESP_OK", "Status: ESP_FAIL"), ("(SW_CPU)", "(POWERON)"),
                              ("factory_new=Yes", "factory_new=No"), ("Waiting for user pairing command", "Auto joining")):
            with self.subTest(before=before), self.assertRaises(ValueError): self.check(self.reset.replace(before, after))

    def test_post_boot_defaults_and_actual_profiles_are_required(self):
        for after in ("", self.after.replace("(1)", "(2)"), self.after.replace("10 dBm", "20 dBm"),
                      self.after.replace("PASSED:", "FAILED:")):
            with self.subTest(after=after), self.assertRaises(ValueError): self.check(after=after)

    def test_cached_pre_boot_readback_or_automatic_pairing_cannot_pass(self):
        with self.assertRaises(ValueError): self.check(self.after + self.reset, after="")
        with self.assertRaises(ValueError): self.check(self.reset + "User initiated Zigbee Network Steering\n")


if __name__ == "__main__":
    unittest.main()
