"""Exercise persistent Developer Mode on the real console and restore OFF.

The caller must save the device configuration baseline before firmware changes.
This helper refuses an already enabled mode and sends each mutation once.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import parse_mode
from Infrastructure.diagnostic.serial_reconnect import read_available


def verify_user_help(text):
    clean = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', text)
    if not re.search(r'(?m)^\s*DEVELOPER\s+:', clean):
        raise ValueError('Developer Mode entry is missing from user help')
    if re.search(r'(?m)^\s*(?:LOGS|DEBUG|DIAG|MEM)\s+:', clean):
        raise ValueError('Developer root leaked into user help')


def verify_tools(text):
    if 'Developer Tools' not in text or not re.search(r'(?m)^\s*MEM\s+:', text):
        raise ValueError('Diagnostics category does not expose the memory tool')
    if 'Read-only:' not in text or 'Controls (may change state):' not in text:
        raise ValueError('Tool operation classes are missing')


def parse_config_snapshot(text):
    fields = re.findall(r'(?m)^RACOOL_CLI v=1 type=config_field key=([^\s]+) value=([^\s]+)\r*$', text)
    totals = re.findall(r'(?m)^RACOOL_CLI v=1 type=config_snapshot fields=(\d+)\r*$', text)
    if len(totals) != 1 or len(fields) != int(totals[0]) or len(dict(fields)) != len(fields):
        raise ValueError('Configuration snapshot is missing, incomplete or ambiguous')
    return dict(fields)


def require_idle_owners(menu_text, system_text):
    menus = re.findall(r'(?m)^RACOOL_CLI v=1 type=menu_status state=(\d+) registry_label_hex=[0-9a-f]+\r*$', menu_text)
    states = re.findall(r'(?m)^RACOOL_CLI v=1 type=system_status state=(-?\d+)\r*$', system_text)
    if len(menus) != 1 or int(menus[0]) != 0:
        raise RuntimeError('Refusing unavailable or foreign menu state')
    if len(states) != 1 or not 0 <= int(states[0]) < 6:
        raise RuntimeError('Refusing unavailable or active recovery state')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--baseline', required=True, help='Fresh config.show --machine capture from before deployment')
    args = parser.parse_args()
    prefix = Path(args.output)
    result = {'completed': False, 'checks': []}
    baseline = parse_config_snapshot(Path(args.baseline).read_text(encoding='utf-8'))
    if baseline.get('restart_required') != '0':
        raise RuntimeError('Refusing restart with pending owner configuration')
    if baseline.get('system.display_mode') != '2' or baseline.get('system.menu_auto_exit_enabled') != '0':
        raise RuntimeError('Physical observation display settings are not active')
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    admitted = False
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def receive(deadline):
            chunk = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
            log.write(chunk); log.flush()
            return chunk

        def command(value, marker, timeout=10):
            port.reset_input_buffer()
            log.write('\nCOMMAND: ' + value + '\n'); log.flush()
            port.write((value + '\r\n').encode())
            response = ''; deadline = time.monotonic() + timeout; matched = None
            while time.monotonic() < deadline:
                response += receive(deadline)
                if re.search(marker, response) and matched is None: matched = time.monotonic()
                if matched is not None and time.monotonic() - matched >= 0.3: return response
            raise RuntimeError('Missing fresh response: ' + value)

        def mode():
            return parse_mode(command('DEVELOPER STATUS --machine', r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))

        def set_mode(enabled):
            value = 'ON' if enabled else 'OFF'
            text = command('DEVELOPER ' + value + ' --machine', r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$')
            if parse_mode(text) != enabled or mode() != enabled: raise RuntimeError('Mode readback mismatch')

        def reboot():
            command('power restart 500 --machine', r'type=power_restart result=queued delay_ms=500 error=0', timeout=5)
            boot = ''; deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                boot += receive(deadline)
                start = boot.rfind('ESP-ROM:')
                if (start >= 0 and 'SW_CPU' in boot[start:] and 'racool>' in boot[start:]
                        and 'SYSTEM INITIALIZATION COMPLETE' in boot[start:]): return
            raise RuntimeError('Fresh software boot and completed system initialization were not observed')

        try:
            port.reset_input_buffer()
            if mode(): raise RuntimeError('Refusing an already active Developer Mode session')
            admitted = True
            verify_user_help(command('HELP', r'DEVELOPER\s+:'))
            denied = command('DIAG METRICS --machine', r'Developer Tools are unavailable')
            if 'type=diagnostic_metrics' in denied: raise RuntimeError('Disabled developer command executed')
            product = command('LANGUAGE --machine', r'(?m)^RACOOL_CLI v=1 type=language locale=\S+\r*$')
            result['checks'].append('off_help_gate_product')
            set_mode(True)
            verify_tools(command('DEVELOPER TOOLS diagnostics', r'Controls \(may change state\):'))
            metrics = command('DIAG METRICS --machine', r'(?m)^RACOOL_CLI v=1 type=diagnostic_metrics [^\r\n]+\r*$')
            if 'type=diagnostic_metrics' not in metrics: raise RuntimeError('Metrics did not run')
            if command('LANGUAGE --machine', r'(?m)^RACOOL_CLI v=1 type=language locale=\S+\r*$').split('type=language locale=')[-1].split()[0] != product.split('type=language locale=')[-1].split()[0]:
                raise RuntimeError('Product locale changed during mode transition')
            result['checks'].append('on_tools_metrics_product')
            current = parse_config_snapshot(command('config.show --machine', r'(?m)^RACOOL_CLI v=1 type=config_snapshot fields=\d+\r*$'))
            expected = dict(baseline, **{'system.developer_mode_enabled': '1'})
            if current != expected: raise RuntimeError('Owner configuration changed before restart')
            menu = command('menu status --machine', r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$')
            state = command('sys status --machine', r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$')
            require_idle_owners(menu, state)
            reboot()
            if not mode(): raise RuntimeError('Developer Mode preference was not retained after restart')
            result['checks'].append('fresh_boot_preference_on')
            current = parse_config_snapshot(command('config.show --machine', r'(?m)^RACOOL_CLI v=1 type=config_snapshot fields=\d+\r*$'))
            if current != expected:
                result['configuration_differences'] = sorted(key for key in set(current) | set(expected) if current.get(key) != expected.get(key))
                raise RuntimeError('Owner configuration differs from baseline')
            result['checks'].append('configuration_baseline_preserved')
            menu = command('menu status --machine', r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$')
            state = command('sys status --machine', r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$')
            require_idle_owners(menu, state)
            set_mode(False)
            reboot()
            if mode(): raise RuntimeError('Final mode is not OFF')
            result['checks'].append('fresh_boot_preference_off')
            result['completed'] = True
        except Exception as error:
            result['error'] = str(error)
        finally:
            if admitted:
                try:
                    if mode(): set_mode(False)
                    result['mode_restored_off'] = not mode()
                except Exception as error:
                    result['completed'] = False
                    result['mode_restore_error'] = str(error)
            prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['completed'] and result.get('mode_restored_off') else 1


if __name__ == '__main__':
    raise SystemExit(main())
