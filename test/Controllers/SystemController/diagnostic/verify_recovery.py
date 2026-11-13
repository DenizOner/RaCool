"""Verify cancellation and one elapsed recovery restart on production firmware."""
import argparse
import json
import re
import sys
import time
from pathlib import Path
import serial
from system_cli_evidence import parse_state, parse_result

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {'completed': False, 'restored': False}
    original = None
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def collect(seconds, accept=None):
            deadline = time.monotonic() + seconds
            text = ''
            while time.monotonic() < deadline:
                chunk = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                log.write(chunk); log.flush(); text += chunk
                if accept and accept(text): return text
            if accept: raise RuntimeError('Missing fresh console evidence')
            return text

        def command(value, pattern):
            port.reset_input_buffer()
            log.write('\nCOMMAND: ' + value + '\n')
            port.write((value + '\r\n').encode())
            return collect(8, lambda t: re.search(pattern, t))

        def state():
            text = command('sys status --machine', r'RACOOL_CLI v=1 type=system_status state=\s*\d+')
            return parse_state(text)

        def apply_state(selected):
            response = command(f'sys setstate {selected} --machine', r'RACOOL_CLI v=1 type=system_state result=(applied|rejected) state=-?\d+')
            result = parse_result(response)
            expected = 6 if selected == 7 else selected
            if result['result'] != 'applied' or result['state'] != expected:
                raise RuntimeError('System state request was not applied')

        session = DeveloperSession(lambda text: command(text, r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            menu = command('menu status --machine', r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$')
            system = command('sys status --machine', r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$')
            require_idle_owners(menu, system)
            original = state()
            if original >= 6:
                original = None
                raise RuntimeError('Foreign recovery/error episode is active')
            evidence['original_state'] = original
            prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
            apply_state(7)
            collect(2)
            apply_state(original)
            cancelled = collect(11)
            if 'ESP-ROM:' in cancelled or 'Automatic recovery restart requested' in cancelled or state() != original:
                raise RuntimeError('Cancelled recovery restarted or changed state')
            evidence['cancellation_verified'] = True
            started = time.monotonic()
            apply_state(7)
            boot = collect(40, lambda t: 'ESP-ROM:' in t and 'racool>' in t[t.rfind('ESP-ROM:'):] and 'SYSTEM INITIALIZATION COMPLETE' in t[t.rfind('ESP-ROM:'):])
            if 'Automatic recovery restart requested' not in boot or 'SW_CPU' not in boot:
                raise RuntimeError('Missing checked restart and software boot evidence')
            session.after_boot()
            evidence['boot_observed_after_s'] = round(time.monotonic() - started, 3)
            evidence['fresh_software_boot'] = True
            quiet = collect(12)
            if 'ESP-ROM:' in quiet: raise RuntimeError('Unexpected reboot loop')
            evidence['completed'] = True
        except Exception as error:
            evidence['error'] = str(error)
        finally:
            if original is not None:
                try:
                    apply_state(original)
                    evidence['restored'] = state() == original
                except Exception as error:
                    evidence['restoration_error'] = str(error)
            try:
                session.close()
            except Exception as error:
                evidence['developer_mode_restore_error'] = str(error)
                evidence['completed'] = False
    prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    raise SystemExit(0 if evidence['completed'] and evidence['restored'] else 1)


if __name__ == '__main__':
    main()
