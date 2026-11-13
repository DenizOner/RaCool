"""Observe bounded flash capture without clearing pre-existing log files."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, parse_mode


def capture_status(text):
    start=text.rfind('RACOOL_CLI v=1 type=flash_capture ')
    match=re.match(r'RACOOL_CLI v=1 type=flash_capture active=([01]) admitted=(\d+) rejected=(\d+) remaining=(\d+)\r*\n',text[start:]) if start>=0 else None
    return tuple(map(int,match.groups())) if match else None



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    result = {'completed': False, 'capture_stopped': False}
    owned = False
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False; port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(value, accept):
            port.reset_input_buffer(); port.write((value+'\r\n').encode())
            log.write('\nCOMMAND: '+value+'\n')
            text=''; deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                chunk=port.read(port.in_waiting or 1).decode('utf-8', errors='replace')
                text+=chunk;log.write(chunk);log.flush()
                if accept(text): return text
            raise RuntimeError('Missing fresh result: '+value)

        def state():
            return capture_status(command('LOGS capture-status --machine', lambda t: capture_status(t) is not None))

        def exchange_mode(value):
            def accepted(text):
                try: parse_mode(text); return True
                except RuntimeError: return False
            return command(value, accepted)

        session=DeveloperSession(exchange_mode)
        try:
            session.open()
            if state()[0]: raise RuntimeError('Foreign capture is active')
            command('LOGS capture 12 1 --machine', lambda t: 'RACOOL_CLI v=1 type=logs_command result=completed' in t)
            owned=True
            command('sys test', lambda t: 'System controller self-test PASSED.' in t or 'Sistem denetleyicisi öz sınaması BAŞARILI.' in t)
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                current=state()
                if not current[0]: break
                time.sleep(0.25)
            if current[0] or current[1:4]!=(1,0,0):
                raise RuntimeError('One-record capture budget not consumed successfully')
            command('LOGS flush --machine', lambda t: 'RACOOL_CLI v=1 type=logs_command result=completed' in t)
            result['one_record']=current
            command('LOGS capture 1 256 --machine', lambda t: 'RACOOL_CLI v=1 type=logs_command result=completed' in t)
            time.sleep(1.5)
            current=state()
            if current[0] or current[1]+current[2]+current[3]!=256:
                raise RuntimeError('Timed capture did not expire with conserved budget')
            result['timed_capture']=current
            command('LOGS flush --machine', lambda t: 'RACOOL_CLI v=1 type=logs_command result=completed' in t)
            result['completed']=True
        except Exception as error:
            result['error']=str(error)
        finally:
            if owned:
                try:
                    command('LOGS capture-stop --machine', lambda t: 'RACOOL_CLI v=1 type=logs_command result=completed' in t)
                    result['capture_stopped']=state()[0]==0
                except Exception as error:
                    result['cleanup_error']=str(error)
            try:
                session.close()
            except Exception as error:
                result['developer_mode_restore_error']=str(error)
    prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    raise SystemExit(0 if result['completed'] and result['capture_stopped'] and 'developer_mode_restore_error' not in result else 1)


if __name__=='__main__': main()
