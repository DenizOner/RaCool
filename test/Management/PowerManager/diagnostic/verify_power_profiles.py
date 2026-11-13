"""Verify persisted CPU profiles over the selected serial console, restoring the original profile."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, has_mode_response
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners
from Hardware.InfraredTemperatureSensor.diagnostic.ir_cli_evidence import parse_stats, require_selftest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {'completed': False, 'checks': []}
    names = {0: 'perf', 1: 'balanced', 2: 'lowpower'}
    original = None
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def receive(deadline):
            part = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
            log.write(part)
            log.flush()
            return part
        def command(text, marker, timeout=8):
            port.reset_input_buffer()
            port.write((text + '\r\n').encode())
            log.write('\nCOMMAND: ' + text + '\n')
            received = ''
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                received += receive(deadline)
                if re.search(r'(?:^|[\r\n])(?:ERROR:|FAILED:)', received) or re.search(r'^RACOOL_CLI v=1 type=power_\w+ result=(?:failed|rejected)\b',received,re.M):
                    raise RuntimeError('CLI rejected operation: ' + text)
                if (has_mode_response(received) if text.startswith('DEVELOPER ') else (marker(received) if callable(marker) else marker in received)):
                    return received
            raise RuntimeError('Missing response: ' + text)
        def read_mode():
            response = command('power status --machine', 'type=power_status')
            mode = re.search(r'^RACOOL_CLI v=1 type=power_status mode=(-?\d+) locks=\d+ sleeps=\d+ wakeups=\d+ restarts=\d+ uptime_s=\d+\r*$', response, re.M)
            if not mode or int(mode[1]) not in names:
                raise RuntimeError('No applied profile in status')
            return int(mode[1])
        def wait_for_router():
            # Application startup completes before asynchronous Zigbee rejoin.
            # Retry only this read-only query; never replay profile writes.
            for attempt in range(4):
                try:
                    info=command('ZB INFO', lambda text:bool(re.search(r'(?m)^\s*(?:Role|Rol)\s*:\s*(?:Router|Yönlendirici)\s*\r*$',text)))
                    command('ZB DIAG --machine', lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=zigbee_status state=2 connected=1 endpoints=\d+\r*$',text)))
                    return info
                except RuntimeError:
                    if attempt == 3:
                        raise
        def apply(value):
            command('power mode ' + names[value] + ' --machine', 'type=power_mode result=applied mode='+str(value)+' error=0')
            actual = read_mode()
            if actual != value: raise RuntimeError('Profile mismatch')
            command('power selftest --machine', 'type=power_selftest result=passed error=0')
            evidence['checks'].append({'profile': value, 'timestamp': time.time()})
        def sensor_stats():
            response = command('ir_temp stats --machine', 'type=ir_sensor_stats')
            try:values=parse_stats(response)
            except ValueError as error:raise RuntimeError(str(error)) from error
            return {'Total Reads':values['reads'],'I2C Errors':values['i2c_errors'],'PEC Errors':values['pec_errors']}

        def sensor_selftest():
            response=command('ir_temp selftest --machine','type=ir_sensor_test')
            try:require_selftest(response)
            except ValueError as error:raise RuntimeError(str(error)) from error
        session = DeveloperSession(lambda text: command(text, 'type=developer_mode'))
        try:
            session.open()
            original = read_mode()
            evidence['original'] = original
            prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
            for value in (0, 1, 2):
                before = sensor_stats()
                apply(value)
                sensor_selftest()
                wait_for_router()
                time.sleep(1)
                after = sensor_stats()
                if after['Total Reads'] <= before['Total Reads']:
                    raise RuntimeError('Sensor worker stopped advancing')
                if any(after[name] != before[name] for name in ('I2C Errors', 'PEC Errors')):
                    raise RuntimeError('Sensor errors increased during profile transition')
                evidence['checks'][-1]['sensor_before'] = before
                evidence['checks'][-1]['sensor_after'] = after
            menu=command('menu status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$',text)))
            system=command('sys status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$',text)))
            require_idle_owners(menu,system)
            command('power restart 500 --machine', 'RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0')
            boot = ''
            deadline = time.monotonic() + 35
            while time.monotonic() < deadline:
                boot += receive(deadline)
                marker = boot.rfind('ESP-ROM:')
                if marker >= 0 and 'SYSTEM INITIALIZATION COMPLETE' in boot[marker:] and 'racool>' in boot[marker:]: break
            else: raise RuntimeError('No completed fresh boot')
            if 'SW_CPU' not in boot: raise RuntimeError('Missing software reset reason')
            session.after_boot()
            if read_mode() != 2: raise RuntimeError('Profile did not persist')
            evidence['restart_profile'] = 2
            command('power selftest --machine', 'type=power_selftest result=passed error=0')
            sensor_selftest()
            wait_for_router()
            watchdog = command('watchdog status --machine', 'type=watchdog_status')
            match = re.search(r'^RACOOL_CLI v=1 type=watchdog_status active=([01]) timeout_ms=(\d+) registered=(\d+) warnings=(\d+) timeouts=(\d+) last_timeout_ms=(\d+)\r*$', watchdog, re.M)
            if not match or tuple(map(int, (match[1],match[3],match[4],match[5],match[6]))) != (1,3,0,0,0):
                raise RuntimeError('Watchdog machine acceptance failed')
            evidence['checks_passed'] = True
        finally:
            try:
                if original is not None:
                    current = read_mode()
                    if current != original:
                        apply(original)
                    else:
                        command('power selftest --machine', 'type=power_selftest result=passed error=0')
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
    print('Power profiles accepted; original profile restored.')

if __name__ == '__main__':
    main()
