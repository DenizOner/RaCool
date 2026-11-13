import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from Hardware.RotaryEncoder.diagnostic.verify_encoder_persistence import controls


class EncoderPersistenceEvidenceTest(unittest.TestCase):
    def test_commands_and_partial_output_are_not_status(self):
        self.assertIsNone(controls("ENCCONFIG DETENTS 2\nSteps per detent set to 2"))
        self.assertIsNone(controls("Steps Per Detent: 2\nDirection Inverted: YES"))

    def test_complete_status_and_bounds(self):
        def status(steps, threshold):
            return f"RACOOL_CLI v=1 type=encoder_status state=2 steps={steps} inverted=1 acceleration=0 threshold_ms={threshold} rotation_debounce_ms=5 button_debounce_ms=50 position=0 rotations=0 clicks=0 dropped_rotations=0 dropped_buttons=0 queue_overflows=0\n"
        self.assertEqual({"steps": 2, "inverted": True, "acceleration": False, "threshold": 50}, controls(status(2, 50)))
        for steps, threshold in ((0, 50), (256, 50), (2, 256)):
            self.assertIsNone(controls(status(steps, threshold)))
        valid=status(2,50)
        for bad in [valid.rstrip('\n'),valid.replace('v=1','v=2'),valid.replace('inverted=1','inverted=2')]:self.assertIsNone(controls(valid+bad))


if __name__ == "__main__":
    unittest.main()
