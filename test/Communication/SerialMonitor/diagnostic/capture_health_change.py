"""Temporarily stop an idle SerialMonitor and retain serial evidence alongside JSON."""
import argparse,json,re,time,sys
from pathlib import Path

def parse_status(text):
    m=re.search(r'HEALTH_REPORT state=(\d+) owned=([01])',text)
    if not m or int(m[1]) not in (2,3): raise RuntimeError('Missing stable diagnostic status')
    return int(m[1]),bool(int(m[2]))

def check_serial_fatal(previous,chunk):
    text=previous+chunk
    if re.search(r'Guru Meditation|assert failed|ESP-ROM:',text):
        raise RuntimeError('Unexpected panic or reboot in serial capture')
    return text[-32:]

def capture(command,hold,ready):
    result={'completed':False,'restoration_verified':False}
    attempted=False
    def status(): return parse_status(command('health_report status',r'HEALTH_REPORT state=\d+ owned=[01]'))
    def check_score(text,score):
        match=re.search(r'^RACOOL_CLI v=1 type=diagnostic_status known=1 score=(\d+) running=0 checked=(\d+) healthy=(\d+)\r*\n',text,re.M)
        if not match or int(match[1])!=score:raise RuntimeError('Unexpected committed health')
        if int(match[2])!=9 or int(match[3])!=(9 if score==100 else 8):raise RuntimeError('Unexpected service count')
    def scan(score):
        command('DIAG RUN --machine',r'RACOOL_CLI v=1 type=diagnostic_run result=accepted\r*\n')
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            text=command('DIAG STATUS --machine',r'RACOOL_CLI v=1 type=diagnostic_status known=[01] score=-?\d+ running=[01] checked=\d+ healthy=\d+\r*\n')
            try: check_score(text,score);return
            except RuntimeError: time.sleep(0.2)
        raise RuntimeError('Health did not change after accepted self-test')
    try:
        initial=status()
        if initial!=(2,False): raise RuntimeError('Refusing foreign or stopped diagnostic service')
        check_score(command('DIAG STATUS --machine',r'RACOOL_CLI v=1 type=diagnostic_status known=[01] score=-?\d+ running=[01] checked=\d+ healthy=\d+\r*\n'),100)
        attempted=True
        command('health_report hold',r'HEALTH_REPORT state=3 owned=1')
        if status()!=(3,True): raise RuntimeError('Hold was not applied')
        scan(88)
        result.update(initial_score=100,changed_score=88)
        ready();hold()
        if status()!=(3,True): raise RuntimeError('Diagnostic ownership changed')
        result['checks_passed']=True
    except Exception as error: result['error']=str(error)
    finally:
        if attempted:
            try:
                current=status()
                if current==(3,True): command('health_report restore',r'HEALTH_REPORT state=2 owned=0')
                if status()!=(2,False): raise RuntimeError('Service restoration failed')
                scan(100);result['restoration_verified']=True
            except Exception as error: result['restore_error']=str(error)
        result['completed']=result.get('checks_passed',False) and result['restoration_verified']
    return result

def main():
    import serial
    sys.path.insert(0,str(Path(__file__).resolve().parents[3]))
    from Infrastructure.diagnostic.serial_reconnect import read_available
    from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',required=True);parser.add_argument('--output',required=True)
    args=parser.parse_args();port=serial.Serial(port=None,baudrate=115200,timeout=0.1);port.dtr=port.rts=False;port.port=args.port
    with port, Path(args.output+'.serial.log').open('w',encoding='utf-8') as log:
        tail=''
        def receive(deadline):
            nonlocal tail
            chunk=read_available(port,deadline,log.write).decode('utf-8',errors='replace')
            log.write(chunk);log.flush()
            tail=check_serial_fatal(tail,chunk)
            return chunk
        def command(text,expected):
            deadline=time.monotonic()+12
            while port.in_waiting and time.monotonic()<deadline:receive(deadline)
            if port.in_waiting:raise RuntimeError('Serial input did not quiesce before command: '+text)
            log.write('\nCOMMAND: '+text+'\n');log.flush()
            port.write((text+'\r\n').encode());response=''
            while time.monotonic()<deadline:
                response+=receive(deadline)
                if re.search(expected,response): return response
            raise RuntimeError('Missing health diagnostic response: '+text)
        def hold():
            deadline=time.monotonic()+50
            while time.monotonic()<deadline: receive(min(deadline,time.monotonic()+1))
        session=DeveloperSession(lambda text:command(text,r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        result={'completed':False,'restoration_verified':False}
        try:
            session.open()
            result=capture(command,hold,lambda:print('HEALTH_CHANGE_READY',flush=True))
        except Exception as error:
            result['error']=str(error)
        finally:
            try:session.close()
            except Exception as error:
                result['completed']=False
                result['developer_mode_restore_error']=str(error)
            Path(args.output+'.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result),flush=True)
    return 0 if result['completed'] else 1
if __name__=='__main__':raise SystemExit(main())
