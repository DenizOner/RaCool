import unittest
from diagnostic.menu_cli_evidence import parse_status, menu_state

class MenuCliEvidenceTests(unittest.TestCase):
    def record(self,state,label):return f'RACOOL_CLI v=1 type=menu_status state={state} registry_label_hex={label.encode("utf-8").hex()}'
    def test_utf8_and_latest_complete_record(self):
        text=self.record(0,'İzleme')+'\r\r\n'+self.record(64,'Язык')+'\r\n'
        self.assertEqual((64,'Язык'),parse_status(text));self.assertEqual(64,menu_state(text))
    def test_admission_is_not_worker_state(self):
        self.assertIsNone(menu_state('RACOOL_CLI v=1 type=menu_navigation result=queued state=64'))
    def test_rejects_human_foreign_version_truncated_encoding(self):
        valid=self.record(2,'Menu')
        for value in ('Current UI State : 2 (Menu)',valid.replace('v=1','v=2'),valid.replace('4d656e75','6'),valid.replace('4d656e75','ff'),valid.replace('state=2','state=65536'),valid.replace('4d656e75','')):
            with self.subTest(value=value):self.assertIsNone(parse_status(value))
    def test_registry_label_capacity_is_enforced(self):
        self.assertIsNotNone(parse_status(self.record(2,'x'*31)))
        self.assertIsNone(parse_status(self.record(2,'x'*32)))

if __name__=='__main__':unittest.main()
