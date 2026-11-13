import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from Core.SystemInitializer.diagnostic.startup_diagnostics_evidence import validate


def evidence():
    return "\n".join(["ESP-ROM:esp32c6", "SUCCESS: InfraredTemperatureSensor started",
        "SUCCESS: InternalTemperatureSensor started", "System Initialization COMPLETE",
        "app_main: System ready.", "Device joined Zigbee network successfully",
        "Asynchronous self-test started", "Self-test finished. Health Score: 100"])


class StartupEvidenceTests(unittest.TestCase):
    def test_complete_ordering(self):
        self.assertEqual(100, validate(evidence())["initial_score"])

    def test_early_scan_rejected(self):
        text = evidence().replace("Device joined Zigbee network successfully\nAsynchronous self-test started",
            "Asynchronous self-test started\nDevice joined Zigbee network successfully")
        with self.assertRaises(ValueError): validate(text)

    def test_early_bootstrap_rejected(self):
        text = evidence().replace("SUCCESS: InternalTemperatureSensor started\nSystem Initialization COMPLETE",
            "System Initialization COMPLETE\nSUCCESS: InternalTemperatureSensor started")
        with self.assertRaises(ValueError): validate(text)

    def test_failures_and_incomplete_output_rejected(self):
        for text in [evidence().replace("100", "88"), evidence() + "\nService FAILED: Zigbee",
                     evidence().replace("ESP-ROM:", ""), evidence() + "\nESP-ROM:esp32c6",
                     evidence().replace("Self-test finished. Health Score: 100", "")]:
            with self.subTest(text=text):
                with self.assertRaises(ValueError): validate(text)


if __name__ == "__main__": unittest.main()
