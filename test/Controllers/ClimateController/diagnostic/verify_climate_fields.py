"""Exercise climate RAM controls with snapshot, readback and finally restoration."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available


from Controllers.ClimateController.diagnostic.climate_cli_evidence import parse_status


def parse_settings(text):
    try:status=parse_status(text)
    except ValueError as error:raise RuntimeError(str(error)) from error
    result={"open":status["open"],"close":status["close"],"object":status["object_cal"],"ambient":status["ambient_cal"],
            "manual":"ACTIVE" if status["manual"] else "OBJECT_SENSITIVE","state":"RUNNING" if status["manual_state"] else "STOPPED","mode":status["mode"],"object_source":status["object_source"],"ambient_source":status["ambient_source"],"delta_on":status["delta_on"],"delta_off":status["delta_off"]}
    if not 20<=result["close"]<result["open"]<=99:raise RuntimeError("Unsafe initial setpoint pair")
    if not status["manual"] and status["manual_state"]:raise RuntimeError("Cannot restore dormant manual state; refusing writes")
    if status["manual"] and status["strategy"]:raise RuntimeError("Cannot restore dormant manual strategy; refusing writes")
    return result


def expected_defaults():
    source = (Path(__file__).resolve().parents[4] / 'include/Management/ConfigurationManager/SystemConfigs.h').read_text(encoding='utf-8')
    result = {}
    names = {'open': 'RELAY_OPEN_TEMP', 'close': 'RELAY_CLOSE_TEMP', 'object': 'IR_OBJECT_CAL', 'ambient': 'IR_AMBIENT_CAL'}
    for key, name in names.items():
        match = re.search(r'^#define DEFAULT_' + name + r'\s+(-?\d+(?:\.\d+)?)f?\s', source, re.M)
        if not match: raise RuntimeError('Cannot resolve default: ' + name)
        result[key] = float(match[1])
    for key, name in [('manual', 'FAN_MANUAL_MODE'), ('state', 'FAN_MANUAL_STATE')]:
        match = re.search(r'^#define DEFAULT_' + name + r'\s+(true|false)\s', source, re.M)
        if not match: raise RuntimeError('Cannot resolve default: ' + name)
        result[key] = ('ACTIVE' if match[1] == 'true' else 'OBJECT_SENSITIVE') if key == 'manual' else ('RUNNING' if match[1] == 'true' else 'STOPPED')
    result.update(mode="OBJECT_SENSITIVE",object_source=0,ambient_source=0)
    for key,name in [("delta_on","FAN_DELTA_ON_DECIC"),("delta_off","FAN_DELTA_OFF_DECIC")]:
        match=re.search(r'^#define DEFAULT_'+name+r'\s+(-?\d+)\s',source,re.M)
        if not match:raise RuntimeError("Cannot resolve source/delta default")
        result[key]=int(match[1])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    evidence = {'completed': False, 'checks': [], 'started': time.time(), 'persistent_writes': False}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(text, accept):
            port.reset_input_buffer()
            port.write((text + '\r\n').encode())
            log.write('\nCOMMAND: ' + text + '\n')
            response = ''
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                response += part
                log.write(part); log.flush()
                if accept(response): return response
            raise RuntimeError('Missing CLI result: ' + text)
        def snapshot():
            return parse_settings(command('CLIMATE STATUS --machine', lambda text: 'type=climate_status' in text and 'manual_state=' in text and text.endswith('\n')))
        def apply(text, expected):
            response = command(text+' --machine', lambda text: 'type=climate_command' in text and text.endswith('\n'))
            if not re.search(r'^RACOOL_CLI v=1 type=climate_command command=[A-Z]+ result=completed error=0\r*$',response,re.M): raise RuntimeError('CLI update rejected')
            actual = snapshot()
            for key, value in expected.items():
                if actual[key] != value: raise RuntimeError(f'{key}: {actual[key]}, expected {value}')
            evidence['checks'].append({'command': text, 'actual': actual, 'timestamp': time.time()})
        original = None
        try:
            defaults = expected_defaults()
            original = snapshot(); evidence['original'] = original
            prefix.with_suffix('.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
            for mode, manual, state in [('ON', 'ACTIVE', 'RUNNING'), ('OFF', 'ACTIVE', 'STOPPED'), ('OBJECT_SENSITIVE', 'OBJECT_SENSITIVE', 'STOPPED')]:
                apply('CLIMATE MODE ' + mode, {'manual': manual, 'state': state})
            high = original['open'] + 0.1 if original['open'] < 98.9 else original['open'] - 0.1
            if high <= original['close']: high = original['open']
            apply(f'CLIMATE SET HIGH {high:.2f}', {'open': round(high, 2)})
            apply('CLIMATE RESET', defaults)
            evidence['after_reset'] = snapshot()
            evidence['checks_passed'] = True
        finally:
            try:
                if original is not None:
                    current = snapshot()
                    order = [('HIGH', 'open'), ('LOW', 'close')] if original['open'] > current['close'] else [('LOW', 'close'), ('HIGH', 'open')]
                    for label, key in order: apply(f'CLIMATE SET {label} {original[key]:.2f}', {key: original[key]})
                    for label, key in [('OBJ', 'object'), ('AMB', 'ambient')]: apply(f'CLIMATE CAL {label} {original[key]:.2f}', {key: original[key]})
                    for kind,key in [('OBJECT','object_source'),('AMBIENT','ambient_source')]:apply(f'CLIMATE SOURCE {kind} {original[key]}',{key:original[key]})
                    apply(f"CLIMATE DELTA {original['delta_on']/10:.1f} {original['delta_off']/10:.1f}",{'delta_on':original['delta_on'],'delta_off':original['delta_off']})
                    mode={'OBJECT_SENSITIVE':'OBJECT_SENSITIVE','AMBIENT_DELTA':'AMBIENT_DELTA','MANUAL ON':'ON','MANUAL OFF':'OFF'}[original['mode']]
                    apply('CLIMATE MODE ' + mode, {'manual': original['manual'], 'state': original['state']})
                    evidence['restored'] = snapshot()
                    if evidence['restored'] != original: raise RuntimeError('Incomplete restoration')
                evidence['completed'] = evidence.get('checks_passed', False)
            finally:
                prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('Climate field readback passed; original settings restored.')


if __name__ == '__main__': main()
