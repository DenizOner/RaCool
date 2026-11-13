import unittest,importlib.util,io,json,sys,tempfile
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
spec=importlib.util.spec_from_file_location('health',Path(__file__).parent/'diagnostic/capture_health_change.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
class HealthChangeTests(unittest.TestCase):
    def test_serial_capture_detects_fatal_markers_across_chunks(self):
        tail=mod.check_serial_fatal('', 'normal output\nGuru Med')
        with self.assertRaises(RuntimeError):mod.check_serial_fatal(tail, 'itation Error\n')
        for text in ('ESP-ROM:esp32c6', 'assert failed'):
            with self.subTest(text=text),self.assertRaises(RuntimeError):mod.check_serial_fatal('',text)
        self.assertTrue(mod.check_serial_fatal('', 'E (1) Diagnostic: expected test fault\n'))
    def run_capture(self,fail_hold=False,foreign=False):
        state=[2,foreign];commands=[]
        def command(text,expected):
            commands.append(text)
            if text=='health_report hold':
                state[:]=[3,True]
                if fail_hold:raise RuntimeError('Uncertain delivery')
            if text=='health_report restore':state[:]=[2,False]
            if text.startswith('health_report'):return f'HEALTH_REPORT state={state[0]} owned={int(state[1])}'
            if text not in ('DIAG RUN --machine','DIAG STATUS --machine'):raise RuntimeError('Unknown CLI command')
            score=88 if state[0]==3 else 100
            return f'RACOOL_CLI v=1 type=diagnostic_run result=accepted\nRACOOL_CLI v=1 type=diagnostic_status known=1 score={score} running=0 checked=9 healthy={8 if score==88 else 9}\n'
        return mod.capture(command,lambda:None,lambda:None),commands
    def test_real_change_and_restore(self):
        result,commands=self.run_capture();self.assertTrue(result['completed']);self.assertEqual(result['changed_score'],88);self.assertIn('health_report restore',commands)
    def test_uncertain_hold_is_read_back_and_restored_without_resend(self):
        result,commands=self.run_capture(True);self.assertFalse(result['completed']);self.assertTrue(result['restoration_verified']);self.assertEqual(commands.count('health_report hold'),1)
    def test_foreign_capture_is_preserved(self):
        result,commands=self.run_capture(foreign=True);self.assertFalse(result['completed']);self.assertNotIn('health_report hold',commands);self.assertNotIn('health_report restore',commands)
    def test_failed_observation_still_restores_service(self):
        state=[2,False];commands=[]
        def command(text,expected):
            commands.append(text)
            if text=='health_report hold':state[:]=[3,True]
            if text=='health_report restore':state[:]=[2,False]
            if text.startswith('health_report'):return f'HEALTH_REPORT state={state[0]} owned={int(state[1])}'
            score=88 if state[0]==3 else 100
            return f'RACOOL_CLI v=1 type=diagnostic_run result=accepted\nRACOOL_CLI v=1 type=diagnostic_status known=1 score={score} running=0 checked=9 healthy={8 if score==88 else 9}\n'
        def fail_hold():raise RuntimeError('Observation failed')
        result=mod.capture(command,fail_hold,lambda:None)
        self.assertFalse(result['completed']);self.assertTrue(result['restoration_verified'])
        self.assertEqual(1,commands.count('health_report hold'));self.assertEqual(1,commands.count('health_report restore'))
    def test_developer_mode_restore_failure_is_saved_as_failure(self):
        class Port:
            def __enter__(self):return self
            def __exit__(self,*args):pass
        class Session:
            def __init__(self,exchange):pass
            def open(self):pass
            def close(self):raise RuntimeError('mode readback failed')
        with tempfile.TemporaryDirectory() as directory:
            output=str(Path(directory)/'health')
            with patch('serial.Serial',return_value=Port()), \
                 patch('Communication.DebugConsole.diagnostic.developer_session.DeveloperSession',Session), \
                 patch.object(mod,'capture',return_value={'completed':True,'restoration_verified':True}), \
                 patch.object(sys,'argv',['capture_health_change.py','--port','fake','--output',output]), \
                 patch.object(sys,'stdout',new_callable=io.StringIO):
                self.assertEqual(1,mod.main())
            result=json.loads(Path(output+'.json').read_text(encoding='utf-8'))
        self.assertFalse(result['completed'])
        self.assertEqual('mode readback failed',result['developer_mode_restore_error'])
    def test_incomplete_status_is_rejected(self):
        for text in ('HEALTH_REPORT state=2','HEALTH_REPORT state=0 owned=0',''):
            with self.assertRaises(RuntimeError):mod.parse_status(text)
    def test_acknowledgment_alone_does_not_replace_changed_health(self):
        state=[2,False];stale=[True];polls=[0]
        def command(text,expected):
            if text=='health_report hold':state[:]=[3,True]
            if text=='health_report restore':state[:]=[2,False]
            if text.startswith('health_report'):return f'HEALTH_REPORT state={state[0]} owned={int(state[1])}'
            if text=='DIAG RUN --machine':return 'RACOOL_CLI v=1 type=diagnostic_run result=accepted\n'
            if text!='DIAG STATUS --machine':raise RuntimeError('Unknown CLI command')
            if state[0]==3:
                polls[0]+=1
                if stale[0]:stale[0]=False;return 'RACOOL_CLI v=1 type=diagnostic_status known=1 score=100 running=0 checked=9 healthy=9\n'
            score=88 if state[0]==3 else 100
            return f'RACOOL_CLI v=1 type=diagnostic_status known=1 score={score} running=0 checked=9 healthy={8 if score==88 else 9}\n'
        result=mod.capture(command,lambda:None,lambda:None)
        self.assertTrue(result['completed']);self.assertEqual(polls[0],2)
if __name__=='__main__':unittest.main()
