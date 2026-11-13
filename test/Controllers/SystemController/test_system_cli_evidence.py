import unittest
from diagnostic.system_cli_evidence import parse_state, parse_result

class SystemCliEvidenceTests(unittest.TestCase):
    def test_status_with_logs_and_crlf(self):
        self.assertEqual(3, parse_state('log\nRACOOL_CLI v=1 type=system_status state=3\r\n'))
    def test_status_rejects_human_version_bounds_duplicate(self):
        line='RACOOL_CLI v=1 type=system_status state=3\n'
        for value in ('Current System State: 3', line.replace('v=1','v=2'),line.replace('state=3','state=8'),line.replace('state=3','state=-1'),line+line):
            with self.assertRaises(RuntimeError): parse_state(value)
    def test_applied_and_rejected(self):
        self.assertEqual('applied', parse_result('RACOOL_CLI v=1 type=system_state result=applied state=6')['result'])
        self.assertEqual('invalid', parse_result('RACOOL_CLI v=1 type=system_state result=rejected state=-1 reason=invalid')['reason'])
        self.assertEqual(2, parse_result('RACOOL_CLI v=1 type=system_state result=rejected state=2 reason=unapplied')['state'])
    def test_invalid_action_contracts_fail(self):
        for value in ('RACOOL_CLI v=1 type=system_state result=applied state=-1', 'RACOOL_CLI v=1 type=system_state result=applied state=2 reason=invalid', 'RACOOL_CLI v=1 type=system_state result=rejected state=2', 'RACOOL_CLI v=1 type=system_state result=rejected state=-1 reason=anything'):
            with self.assertRaises(RuntimeError): parse_result(value)

if __name__=='__main__': unittest.main()
