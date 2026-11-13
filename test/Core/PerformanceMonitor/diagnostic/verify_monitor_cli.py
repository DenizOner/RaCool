"""Verify CLI monitor lifecycle and restore the worker state in finally."""
import argparse
import json
import re
import time
from pathlib import Path
import sys
import serial
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession


def parse_state(text):
    match = re.search(r'^RACOOL_CLI v=1 type=performance_status state=(\d+)\r?$', text, re.M)
    if not match or int(match[1]) not in (1, 2, 3):
        raise RuntimeError('Missing stable PerformanceMonitor lifecycle state')
    return int(match[1])


def verify(command):
    result = {'completed': False, 'checks': [], 'restoration_verified': False}
    original = None
    def state():
        return parse_state(command('perf_status --machine', r'RACOOL_CLI v=1 type=performance_status state=\d+'))
    def apply(running):
        command('perf_start --machine' if running else 'perf_stop --machine',
                r'RACOOL_CLI v=1 type=performance_start result=applied' if running else r'RACOOL_CLI v=1 type=performance_stop result=applied')
        actual = state()
        expected = 2 if running else 3
        if actual != expected:
            raise RuntimeError('CLI acknowledged an unapplied monitor state')
        return actual
    try:
        original = state()
        result['original_state'] = original
        # Preserve an initialized but never started service instead of changing
        # its lifecycle history just for verification.
        if original == 1:
            raise RuntimeError('Refusing to alter a never-started monitor')
        for running in (False, True):
            result['checks'].append({'applied_state': apply(running), 'timestamp': time.time()})
        result['checks_passed'] = True
    except Exception as error:
        result['error'] = str(error)
    finally:
        if original in (2, 3):
            try:
                if state() != original:
                    apply(original == 2)
                result['restoration_verified'] = state() == original
            except Exception as error:
                result['restore_error'] = str(error)
        result['completed'] = result.get('checks_passed', False) and result['restoration_verified']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(text, expected):
            port.reset_input_buffer()
            log.write('\nCOMMAND: ' + text + '\n')
            port.write((text + '\r\n').encode())
            response = ''
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                log.write(part); log.flush(); response += part
                if re.search(expected, response): return response
            raise RuntimeError('Missing response: ' + text)
        session = DeveloperSession(lambda text: command(text, r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            result = verify(command)
        finally:
            session.close()
    prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
