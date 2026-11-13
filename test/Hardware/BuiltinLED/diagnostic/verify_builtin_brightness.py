"""Verify persistent internal LED brightness through the selected serial console, restoring the original value."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners
from Hardware.BuiltinLED.diagnostic.builtin_cli_evidence import parse_builtin_status, parse_builtin_command


def brightness(text):
    state = parse_builtin_status(text)
    return state['brightness'] if state else None



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {"completed": False, "checks": [], "started": time.time()}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(text, accept, timeout=8):
            port.reset_input_buffer()
            port.write((text + '\r\n').encode())
            log.write('\nCOMMAND: ' + text + '\n')
            received = ''
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                received += part
                log.write(part)
                log.flush()
                if accept(received):
                    return received
            raise RuntimeError('Missing CLI result: ' + text)

        def read_value(expected=None):
            for attempt in range(4):
                response = command('bled status --machine', lambda text: brightness(text) is not None)
                actual = brightness(response)
                if expected is None or actual == expected:
                    return actual
                time.sleep(0.2)
            raise RuntimeError(f'Brightness readback {actual}, expected {expected}')

        def apply(value, spelling='bled brightness'):
            command(f'{spelling} {value} --machine', lambda text: parse_builtin_command(text, 'brightness') == 'accepted')
            actual = read_value(value)
            evidence['checks'].append({'value': value, 'actual': actual, 'command': spelling, 'timestamp': time.time()})

        original = None
        session = DeveloperSession(lambda text: command(text, has_mode_response))
        try:
            session.open()
            original = read_value()
            evidence['original'] = original
            prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
            for value in (0, 37, 255):
                apply(value)
            apply(20, 'builtin_led_brightness')
            for invalid in ('-1', '256', '2.5', '20x'):
                command('bled brightness ' + invalid + ' --machine', lambda text: parse_builtin_command(text, 'brightness') == 'rejected')
                read_value(20)
            evidence['invalid_inputs_rejected'] = 4
            apply(0)
            menu=command('menu status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$',text)))
            system=command('sys status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$',text)))
            require_idle_owners(menu,system)
            command('power restart 500 --machine', lambda text: 'RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0' in text)
            boot = ''
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                boot += part
                log.write(part)
                log.flush()
                marker = boot.rfind('ESP-ROM:')
                if marker >= 0 and 'racool>' in boot[marker:] and 'SYSTEM INITIALIZATION COMPLETE' in boot[marker:]:
                    break
            else:
                raise RuntimeError('Missing fresh ROM boot after restart')
            session.after_boot()
            for attempt in range(4):
                try:
                    evidence['after_restart'] = read_value(0)
                    break
                except RuntimeError:
                    if attempt == 3:
                        raise
            evidence['checks_passed'] = True
        finally:
            try:
                if original is not None:
                    apply(original)
                    evidence['restored'] = original
                evidence['completed'] = evidence.get('checks_passed', False)
            finally:
                try:
                    session.close()
                except Exception as error:
                    evidence['completed'] = False
                    evidence['developer_mode_restore_error'] = str(error)
                    raise
                finally:
                    prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('Internal brightness CLI passed; original value restored.')


if __name__ == '__main__':
    main()
