"""Verify that retired session commands cannot issue product credentials."""
import argparse
import json
import time
import re
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, has_mode_response


def rejected_session(text):
    return bool(re.search(r'^RACOOL_CLI v=1 type=security_command result=rejected reason=retired\r*$',text,re.M)) and 'Session token generated:' not in text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--output',required=True)
    args=parser.parse_args();prefix=Path(args.output)
    result={'completed':False,'rejected':[]}
    port=serial.Serial(port=None,baudrate=115200,timeout=0.1)
    port.dtr=port.rts=False;port.port=args.port
    with port,prefix.with_suffix('.log').open('w',encoding='utf-8') as log:
        def exchange_mode(value):
            port.reset_input_buffer(); port.write((value+'\r\n').encode()); response=''; deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                chunk=port.read(port.in_waiting or 1).decode('utf-8',errors='replace')
                response+=chunk;log.write(chunk);log.flush()
                if has_mode_response(response): return response
            raise RuntimeError('Developer Mode response timed out')
        session=DeveloperSession(exchange_mode)
        try:
            session.open()
            for subcommand in ('session-token','invalidate-session'):
                port.reset_input_buffer();port.write(('sec '+subcommand+' --machine\r\n').encode())
                text='';deadline=time.monotonic()+8
                while time.monotonic()<deadline:
                    chunk=port.read(port.in_waiting or 1).decode('utf-8',errors='replace')
                    text+=chunk;log.write(chunk);log.flush()
                    if rejected_session(text):break
                else:raise RuntimeError('Retired command was not explicitly rejected')
                result['rejected'].append(subcommand)
            result['completed']=True
        except Exception as error:result['error']=str(error)
        finally:
            try: session.close()
            except Exception as error: result['completed']=False;result['developer_mode_restore_error']=str(error)
    prefix.with_suffix('.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    raise SystemExit(0 if result['completed'] else 1)


if __name__=='__main__':main()
