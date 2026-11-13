"""Verify visible menu registry labels and report policy-hidden entries."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Controllers.UIController.diagnostic.ui_readiness import wait_for_ui_ready
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, has_mode_response


def registered_entries(root):
    text = (root / 'include/Controllers/MenuManager/MenuManagerTypes.h').read_text(encoding='utf-8')
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    body = text.split('typedef enum {', 1)[1].split('} UIState;', 1)[0]
    names = {}; value = 0
    for item in body.split(','):
        if not item.strip(): continue
        match = re.fullmatch(r'\s*(UI_STATE_\w+)\s*(?:=\s*(0x[0-9a-fA-F]+|[0-9]+))?\s*', item)
        if not match: raise ValueError('Unsupported UI state enum expression')
        if match[2] is not None: value = int(match[2], 16 if match[2].startswith('0x') else 10)
        if match[1] in names: raise ValueError('Duplicate UI state')
        names[match[1]] = value
        value += 1
    result = []
    for source, table in (
        ('src/Hardware/RGBLED/RGBLEDMenuPlugin.c', 'menuEntries'),
        ('src/Controllers/ClimateController/ClimateController.c', 'menuEntries'),
        ('src/Controllers/SystemController/SystemController.c', 'systemMenuEntries'),
    ):
        text = (root / source).read_text(encoding='utf-8')
        match = re.search(r'MenuPluginRegistration\s+' + table + r'\[\]\s*=\s*\{(.*?)\};', text, re.S)
        if not match: raise ValueError('Missing registration group: ' + source)
        entries = re.findall(r'\{\s*(UI_STATE_\w+)\s*,\s*"([^"\n]+)"', match.group(1))
        if not entries: raise ValueError('Empty registration group: ' + source)
        result.extend({'state': names[state], 'label': label, 'source': source} for state, label in entries)
    if len({entry['state'] for entry in result}) != len(result): raise ValueError('Overlapping registration groups')
    return result


from Controllers.MenuManager.diagnostic.menu_cli_evidence import parse_status

def parse_visibility(text, expected):
    matches = re.findall(r'(?m)^RACOOL_CLI v=1 type=menu_visibility state=(\d+) visible=([01])\r*$', text)
    if len(matches) != 1 or int(matches[0][0]) != expected:
        raise RuntimeError('Missing or ambiguous menu visibility response')
    return matches[0][1] == '1'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    expected = registered_entries(Path(__file__).resolve().parents[4])
    prefix = Path(args.output)
    result = {'completed': False, 'checks': [], 'hidden_entries': [], 'physical_output_verified': False}
    entered = False
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(text, accept):
            port.reset_input_buffer(); port.write((text + '\r\n').encode())
            log.write('\nCOMMAND: ' + text + '\n')
            received = ''; deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                part = port.read(port.in_waiting or 1).decode('utf-8', errors='replace')
                log.write(part); log.flush(); received += part
                if accept(received): return received
            raise RuntimeError('Missing fresh response: ' + text)
        def status():
            return parse_status(command('menu status --machine', lambda t: parse_status(t) is not None))
        session = DeveloperSession(lambda text: command(text, has_mode_response))
        try:
            session.open()
            wait_for_ui_ready(command)
            if status()[0] != 0: raise RuntimeError('Refusing to interrupt an active menu session')
            entered = True
            for entry in expected:
                response = command(f"menu visible {entry['state']} --machine", lambda t: bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_visibility state=\d+ visible=[01]\r*$', t)))
                if not parse_visibility(response, entry['state']):
                    result['hidden_entries'].append(entry)
                    continue
                command(f"menu nav {entry['state']} --machine", lambda t: 'RACOOL_CLI v=1 type=menu_navigation result=queued state=' in t)
                time.sleep(0.15)
                actual = status()
                passed = actual == (entry['state'], entry['label'])
                result['checks'].append({**entry, 'actual': actual, 'passed': passed, 'timestamp': time.time()})
                if not passed: raise RuntimeError(f"Unexpected registry label: {actual}, expected {entry}")
            if not result['checks']:
                raise RuntimeError('No visible menu registrations were verified')
            result['completed'] = True
        except Exception as exc:
            result['error'] = str(exc)
        finally:
            if entered:
                try:
                    command('menu reset --machine', lambda t: 'RACOOL_CLI v=1 type=menu_navigation result=queued state=0' in t)
                    time.sleep(0.2)
                    if status()[0] != 0: raise RuntimeError('Monitoring menu was not restored')
                    result['restoration_verified'] = True
                except Exception as exc:
                    result['completed'] = False; result['restore_error'] = str(exc)
            try:
                session.close()
            except Exception as exc:
                result['completed'] = False; result['developer_mode_restore_error'] = str(exc)
            prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'completed': result['completed'], 'checks': len(result['checks']), 'hidden_entries': len(result['hidden_entries']), 'restoration_verified': result.get('restoration_verified'), 'error': result.get('error')}))
    return 0 if result['completed'] and result.get('restoration_verified') else 1


if __name__ == '__main__': raise SystemExit(main())
