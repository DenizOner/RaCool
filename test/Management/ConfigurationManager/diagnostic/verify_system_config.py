"""Verify a selected CLI field, restoring the snapshot in finally."""
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
from Controllers.ClimateController.diagnostic.climate_cli_evidence import parse_status, record


def read_field(text, field):
    if field == 'radiator-calibration':
        return parse_status(text)['object_cal']
    pattern = r"(?m)^RACOOL_CLI v=1 type=config_field key=hardware\.ext_led_brightness value=(\d+)\r*$"
    values = re.findall(pattern, text)
    return float(values[-1]) if values else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--field', choices=('brightness', 'radiator-calibration'), default='brightness')
    args = parser.parse_args()
    prefix = Path(args.output)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    evidence = {'completed': False, 'checks': [], 'started': time.time(), 'field': args.field, 'persistent_write': args.field == 'brightness'}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(text, accept):
            port.reset_input_buffer()
            port.write((text + '\r\n').encode())
            log.write('\nCOMMAND: ' + text + '\n')
            received = ''
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                received += part
                log.write(part)
                log.flush()
                if accept(received):
                    return received
            raise RuntimeError('Missing CLI result: ' + text)

        def read_value():
            response = command('config.show --machine', lambda text: bool(re.search(r'(?m)^RACOOL_CLI v=1 type=config_snapshot fields=\d+\r*$', text))) if args.field == 'brightness' else command('CLIMATE STATUS --machine', lambda text: 'type=climate_status' in text and text.endswith('\n'))
            result = read_field(response, args.field)
            if result is None or not (-10 <= result <= 255):
                raise RuntimeError('Missing field snapshot')
            return result

        def apply(value):
            text = f'SYSTEM CONFIG SET brightness {int(value)}' if args.field == 'brightness' else f'CLIMATE CAL OBJ {value} --machine'
            if args.field == 'brightness':
                response = command(text, lambda text: any(marker in text for marker in ('brightness saved to flash.', 'brightness flash belleğine kaydedildi.', 'Configuration update failed', 'Ayar güncellenemedi')))
                rejected = 'Configuration update failed' in response or 'Ayar güncellenemedi' in response
            else:
                response = command(text, lambda text: 'type=climate_command' in text and text.endswith('\n'))
                result = record(response, 'climate_command')
                rejected = result.get('command') != 'CAL' or result.get('result') != 'completed' or result.get('error') != '0'
            if rejected:
                raise RuntimeError('Persistent field update failed')
            actual = read_value()
            if abs(actual - value) > 0.005:
                raise RuntimeError(f'Readback {actual}, expected {value}')
            evidence['checks'].append({'expected': value, 'actual': actual, 'timestamp': time.time()})

        original = None
        session = DeveloperSession(lambda text: command(text, has_mode_response))
        try:
            session.open()
            original = read_value()
            evidence['original'] = original
            prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
            value = (127 if original != 127 else 128) if args.field == 'brightness' else (0.2 if original != 0.2 else 0.3)
            apply(value)
            evidence['checks_passed'] = True
        finally:
            try:
                if original is not None:
                    apply(original)
                    evidence['restored'] = original
                evidence['completed'] = evidence.get('checks_passed', False) and evidence.get('restored') == original
            finally:
                try:
                    session.close()
                except Exception as error:
                    evidence['completed'] = False
                    evidence['developer_mode_restore_error'] = str(error)
                    raise
                finally:
                    prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('CLI field readback passed; original value restored.')


if __name__ == '__main__':
    main()
