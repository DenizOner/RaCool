"""Verify system CLI validation and restore the original non-recovery state."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession
from system_cli_evidence import parse_state, parse_result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    result = {"completed": False, "checks": []}
    original = None
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(value, expected):
            port.reset_input_buffer()
            log.write("COMMAND: " + value + "\n")
            port.write((value + "\r\n").encode())
            data = ""
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                chunk = port.read(port.in_waiting or 1).decode('utf-8', errors='replace')
                data += chunk
                log.write(chunk); log.flush()
                if re.search(expected, data): return data
            raise RuntimeError("Missing response for " + value)

        def state():
            data = command('sys status --machine', r'RACOOL_CLI v=1 type=system_status state=\s*\d+')
            return parse_state(data)

        def apply_state(selected):
            response = command(f'sys setstate {selected} --machine', r'RACOOL_CLI v=1 type=system_state result=(applied|rejected) state=-?\d+')
            result = parse_result(response)
            expected = 6 if selected == 7 else selected
            if result['result'] != 'applied' or result['state'] != expected:
                raise RuntimeError('System state request was not applied')

        session = DeveloperSession(lambda text: command(text, r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            current = state()
            if current >= 6:
                raise RuntimeError("Refusing to modify an active recovery/error state")
            original = current
            result['original_state'] = original
            for invalid in ('abc', '1x', '-1', '99999999999999999999999', '8'):
                response = command('sys setstate ' + invalid + ' --machine', 'RACOOL_CLI v=1 type=system_state result=rejected state=-1 reason=invalid')
                if parse_result(response) != {'result':'rejected','state':-1,'reason':'invalid'}: raise RuntimeError('Invalid input was not explicitly rejected')
                if state() != original: raise RuntimeError("Rejected input changed state")
                result['checks'].append({'rejected': invalid})
            selected = 1 if original != 1 else 0
            apply_state(selected)
            if state() != selected: raise RuntimeError("Valid selection not applied")
            result['checks'].append({'applied_state': selected})
            result['completed'] = True
        except Exception as exc:
            result['error'] = str(exc)
        finally:
            if original is not None:
                try:
                    apply_state(original)
                    result['restoration_verified'] = state() == original
                except Exception as exc:
                    result['restore_error'] = str(exc)
            try:
                session.close()
            except Exception as exc:
                result['developer_mode_restore_error'] = str(exc)
                result['completed'] = False
            result['completed'] = result['completed'] and result.get('restoration_verified', False)
            prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
