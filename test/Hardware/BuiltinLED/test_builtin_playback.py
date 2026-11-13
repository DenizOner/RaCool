import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from Hardware.BuiltinLED.diagnostic import verify_builtin_playback as playback
import unittest



class PlaybackStatusTests(unittest.TestCase):
    def test_gate_is_distinct_from_black_idle_animation(self):
        text = 'RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n'
        state = playback.parse_status(text)
        self.assertFalse(state["on"])
        self.assertTrue(state["enabled"])
        self.assertEqual(-1, state["animation"])

    def test_ack_or_old_status_cannot_prove_global_gate(self):
        for text in ("Disabled mode saved.", "Current Animation: 14\nCurrent Priority: 0\nRemaining Duration: 0\nMax Brightness: 255\nIs On: YES"):
            with self.assertRaises(ValueError):
                playback.parse_status(text)


if __name__ == "__main__":
    unittest.main()
