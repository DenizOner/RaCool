import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from Management.SecurityManager.diagnostic.verify_security_surface import rejected_session


class RetiredSessionEvidenceTest(unittest.TestCase):
    def test_submission_and_issuance_are_not_rejection(self):
        self.assertFalse(rejected_session('sec session-token'))
        self.assertFalse(rejected_session('RACOOL_CLI v=1 type=security_command result=rejected reason=retired\nSession token generated: SYNTHETIC'))
        self.assertTrue(rejected_session('RACOOL_CLI v=1 type=security_command result=rejected reason=retired'))


if __name__=='__main__':unittest.main()
