"""Verify UART ownership through USB CLI; no external wiring/throughput claim."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response


def status(text):
    rows = re.findall(r'(?m)^RACOOL_CLI [^\r\n]*type=serial_monitor[^\r\n]*(?:\r*\n|$)', text)
    if not rows or not rows[-1].endswith('\n'): return None
    fields = {}
    for token in rows[-1].strip().split()[1:]:
        if token.count('=') != 1: return None
        key, value = token.split('=')
        if key in fields: return None
        fields[key] = value
    if fields.get('v') != '1' or fields.get('type') != 'serial_monitor': return None
    limits = dict(state=7, owned=1, active=1, closing=1, pending_tx=4294967295,
                  queued_bytes=4294967295, tx_bytes=4294967295, rx_bytes=4294967295,
                  rejected=4294967295, dropped=4294967295, tx_failures=4294967295, rx_failures=4294967295)
    for key, maximum in limits.items():
        if not re.fullmatch(r'[0-9]+', fields.get(key,'')) or len(fields[key]) > 10 or int(fields[key]) > maximum: return None
    return tuple(int(fields[key]) for key in ('state','owned','active','closing','pending_tx'))

def idle_for_test(current):
    return current == (2,0,0,0,0)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {'completed': False, 'restored': False, 'external_wiring_verified': False}
    original = None
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(value, accept):
            port.reset_input_buffer(); port.write((value + '\r\n').encode())
            log.write('\nCOMMAND: ' + value + '\n')
            text = ''; deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                chunk = port.read(port.in_waiting or 1).decode('utf-8', errors='replace')
                log.write(chunk); log.flush(); text += chunk
                if accept(text): return text
            raise RuntimeError('Missing result: ' + value)

        def read():
            return status(command('SER_PERF status --machine', lambda t: status(t) is not None))

        def apply(active):
            verb = 'start' if active else 'stop'
            command('SER_PERF uart-' + verb + ' --machine', lambda t: re.search(r'RACOOL_CLI v=1 type=serial_command command=uart-'+verb+r' result=completed\r*\n',t) is not None)
            actual = read()
            if actual[1:4] != (int(active), int(active), 0):
                raise RuntimeError('UART ownership readback mismatch')
            return actual

        session=DeveloperSession(lambda value:command(value,has_mode_response))
        try:
            session.open()
            current = read()
            if not idle_for_test(current):
                raise RuntimeError('Refusing non-started, closing or busy UART')
            original = bool(current[2]); evidence['original'] = current
            prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
            evidence['started'] = apply(True)
            evidence['stopped'] = apply(False)
            evidence['completed'] = True
        except Exception as error:
            evidence['error'] = str(error)
        finally:
            if original is not None:
                try:
                    evidence['restored_state'] = apply(original)
                    evidence['restored'] = True
                except Exception as error:
                    evidence['restoration_error'] = str(error)
            try:
                session.close()
            except Exception as error:
                evidence['completed'] = False
                evidence['developer_mode_restore_error'] = str(error)
    prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    raise SystemExit(0 if evidence['completed'] and evidence['restored'] else 1)


if __name__ == '__main__':
    main()
