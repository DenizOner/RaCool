import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from Hardware.BuiltinLED.diagnostic import verify_builtin_brightness as module
import unittest

class BrightnessEvidenceTest(unittest.TestCase):
    def test_only_status_fields_are_values(self):
        self.assertIsNone(module.brightness('bled brightness 255\nBrightness saved.'))
        self.assertEqual(0, module.brightness('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=0 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n'))
        self.assertEqual(255, module.brightness('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=20 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\nRACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n'))

    def test_latest_incomplete_invalid_or_unsupported_record_is_rejected(self):
        good = 'RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=20 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n'
        for text in (good.rstrip(), good+good.replace('v=1','v=2'),good+good.replace('brightness=20','brightness=256'),good+'RACOOL_CLI v=1 type=builtin_status brightness=1\n'):
            self.assertIsNone(module.brightness(text))

if __name__ == '__main__':
    unittest.main()
