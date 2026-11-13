import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from Communication.DebugConsole.diagnostic.verify_flash_capture import capture_status


class CaptureStatusTest(unittest.TestCase):
    def test_only_complete_status_counts(self):
        self.assertIsNone(capture_status('LOGS capture 1 256\nFlash capture started (0x0).'))
        self.assertIsNone(capture_status('RACOOL_CLI v=1 type=flash_capture active=1 admitted=0'))
        self.assertEqual((0,1,0,0),capture_status('RACOOL_CLI v=1 type=flash_capture active=0 admitted=1 rejected=0 remaining=0\n'))


if __name__=='__main__': unittest.main()
