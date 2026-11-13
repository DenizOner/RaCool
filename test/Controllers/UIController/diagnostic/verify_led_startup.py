"""Verify independent LED boot workers after real software boots; restore settings."""
import argparse
import codecs
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import serial

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'test'))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners
from Hardware.RGBLED.diagnostic.rgb_cli_evidence import parse_rgb_status
from Hardware.BuiltinLED.diagnostic.builtin_cli_evidence import parse_builtin_status

CONTROLS = ['status_led_startup_animation_status_indicator', 'thermal_led_startup_animation_thermal_indicator',
            'status_led_mode_status_indicator', 'thermal_led_mode_thermal_indicator',
            'status_led_brightness_status_indicator', 'brightness_thermal_indicator']
MUTABLE_CONTROLS = CONTROLS[:4]


def parse_workers(text):
    """Accept complete actual worker/status rows, never write acknowledgments."""
    internal = parse_builtin_status(text)
    external = parse_rgb_status(text)
    ui = re.findall(r'RACOOL_CLI v=1 type=ui screen=(?:active|suspended) service=\d+ boot=(running|idle)\r*\n', text)
    return (internal['animation'], external['animation'], ui[-1]) if internal and external and ui else None


def parse_driver_error(text):
    state = parse_builtin_status(text)
    return state['driver_error'] if state else None



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {'completed': False, 'restored': False, 'initial': None, 'boots': []}
    log = prefix.with_suffix('.log').open('w', encoding='utf-8')
    def save():
        prefix.with_suffix('.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    def bridge(mode, *selection, expected=None):
        environment = os.environ.copy()
        environment.pop('RACOOL_SERIAL_PORT', None)
        environment.pop('RACOOL_READ_EXPECTED', None)
        if expected is not None:
            environment['RACOOL_READ_EXPECTED'] = json.dumps(expected, ensure_ascii=False)
        run = subprocess.run([args.node, str(ROOT/'test/Communication/Zigbee/diagnostic/live_zigbee.mjs'), mode, *selection],
                             cwd=ROOT, env=environment, capture_output=True, text=True, encoding='utf-8', timeout=105)
        log.write(run.stdout + run.stderr); log.flush()
        if run.returncode:
            raise RuntimeError('Bridge operation failed: '+mode)
        if mode == 'read':
            result = json.loads((ROOT/'test/build/zigbee-live-read.json').read_text(encoding='utf-8'))
            if not result.get('completed'):
                raise RuntimeError('Incomplete bridge read')
            return {row['property']: row['value'] for row in result['initialReads']}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    def receive(deadline, decoder):
        text = decoder.decode(read_available(port, deadline, log.write))
        log.write(text); log.flush()
        return text
    def command(text, accept, timeout=5):
        port.reset_input_buffer()
        log.write('\nCOMMAND: '+text+'\n')
        port.write((text+'\r\n').encode())
        decoder = codecs.getincrementaldecoder('utf-8')('replace')
        response = ''; deadline = time.monotonic()+timeout
        while time.monotonic()<deadline:
            response += receive(deadline, decoder)
            if accept(response):
                return response
        raise RuntimeError('No fresh command result: '+text)
    def reboot(expected=None):
        session = DeveloperSession(lambda text: command(text, has_mode_response))
        reboot_submitted = False
        boot_ready = False
        decoder = codecs.getincrementaldecoder('utf-8')('replace')
        boot = ''
        try:
            session.open()
            menu=command('menu status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$',text)))
            system=command('sys status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$',text)))
            require_idle_owners(menu,system)
            reboot_submitted = True
            command('power restart 500 --machine', lambda text:'RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0' in text)
            deadline = time.monotonic()+40
            while time.monotonic()<deadline:
                boot += receive(deadline, decoder)
                marker=boot.rfind('ESP-ROM:')
                if marker>=0 and re.search(r'rst:[^\n]*\(SW_CPU\)',boot[marker:]) and 'racool>' in boot[marker:] and 'SYSTEM INITIALIZATION COMPLETE' in boot[marker:]:
                    boot_ready=True;break
            else:
                raise RuntimeError('No fresh completed software boot')
            if any(fault in boot for fault in ['Guru Meditation', 'Stack canary', 'Queued attribute failed']):
                raise RuntimeError('Boot fault detected')
            session.after_boot()
            if expected is None:return
            # An elapsed boot window is a failed observation.
            deadline = time.monotonic()+8
            while time.monotonic()<deadline:
                try:
                    combined = command('builtin_led_info --machine', lambda text:parse_driver_error(text) is not None, 1)
                    if parse_driver_error(combined) != 0:
                        raise RuntimeError('Builtin LED driver failure')
                    combined += command('rgb status --machine', lambda text:re.search(r'RACOOL_CLI v=1 type=rgb_status[^\n]*\n', text), 1)
                    combined += command('UI STATUS --machine', lambda text:'type=ui' in text, 1)
                    actual = parse_workers(combined)
                    if actual and actual[2]=='idle':
                        raise RuntimeError('Boot observation window elapsed')
                    if actual and actual[:2]==expected and actual[2]=='running':
                        evidence['boots'].append({'expected': list(expected), 'actual': list(actual), 'builtin_driver_error':0, 'timestamp': time.time()})
                        save(); return
                except RuntimeError as error:
                    if 'window elapsed' in str(error) or 'driver failure' in str(error):
                        raise
                time.sleep(0.1)
            raise RuntimeError('No matching independent workers during boot')
        finally:
            if reboot_submitted and not boot_ready:
                deadline=time.monotonic()+40
                while time.monotonic()<deadline:
                    boot+=receive(deadline,decoder)
                    marker=boot.rfind('ESP-ROM:')
                    if marker>=0 and 'SYSTEM INITIALIZATION COMPLETE' in boot[marker:] and 'racool>' in boot[marker:]:
                        boot_ready=True;break
                if not boot_ready:raise RuntimeError('Cannot safely determine mode ownership after uncertain reboot')
            session.close()
    admitted = False
    owned_choices = {}
    try:
        original = bridge('read', *CONTROLS)
        if set(original)!=set(CONTROLS):
            raise RuntimeError('Incomplete baseline')
        evidence['initial'] = original; save()
        owned_choices = {key:{str(original[key])} for key in MUTABLE_CONTROLS}
        with port:
            initial_cli = command('builtin_led_boot', lambda text:re.search(r'type=builtin_boot animation=\d+\r*\n',text))
            evidence['initial_internal_id'] = int(re.findall(r'type=builtin_boot animation=(\d+)\r*\n',initial_cli)[-1])
        save()
        admitted = True
        common = {CONTROLS[2]:'Otomatik', CONTROLS[3]:'Otomatik'}
        scenarios = [('Mum Işığı','Mum Işığı',17,4,None), ('Gökkuşağı','Mum Işığı',6,4,None),
                     ('Mum Işığı','Mum Işığı',-1,4,CONTROLS[2]), ('Mum Işığı','Mum Işığı',17,0,CONTROLS[3]),
                     ('Kapalı','Mum Işığı',-1,4,None)]
        for internal, external, internal_id, external_id, disabled in scenarios:
            desired = {**original, **common, CONTROLS[0]:internal, CONTROLS[1]:external}
            if disabled:
                desired[disabled] = 'Kapalı'
            for key in MUTABLE_CONTROLS:
                current=bridge('read',key)
                if key not in current or str(current[key]) not in owned_choices[key]:
                    raise RuntimeError('Refusing to overwrite a foreign LED setting: '+key)
                requested=str(desired[key]);owned_choices[key].add(requested)
                if str(current[key])!=requested:
                    bridge('set', key, requested)
                    bridge('read',key,expected={key:desired[key]})
            bridge('read', *CONTROLS, expected=desired)
            with port:
                reboot((internal_id, external_id))
                # Let boot finish before the next checked restart.
                time.sleep(10)
        evidence['completed'] = True
    except Exception as error:
        evidence['error'] = str(error)
        raise
    finally:
        try:
            if admitted:
                if port.is_open:
                    port.close()
                for key in MUTABLE_CONTROLS:
                    current=bridge('read',key)
                    if key not in current or str(current[key]) not in owned_choices[key]:
                        raise RuntimeError('Refusing to overwrite a foreign LED setting: '+key)
                    if str(current[key])!=str(evidence['initial'][key]):
                        bridge('set', key, str(evidence['initial'][key]))
                        bridge('read',key,expected={key:evidence['initial'][key]})
                bridge('read', *CONTROLS, expected=evidence['initial'])
                with port:
                    reboot()
                    for attempt in range(5):
                        try:
                            response = command('builtin_led_boot', lambda text:re.search(r'type=builtin_boot animation=\d+\r*\n',text),1)
                            actual = int(re.findall(r'type=builtin_boot animation=(\d+)\r*\n',response)[-1])
                            if actual != evidence['initial_internal_id']:
                                raise RuntimeError('Persisted boot selection not restored')
                            break
                        except RuntimeError:
                            if attempt==4:
                                raise
                            time.sleep(0.2)
                evidence['restored'] = True
        except Exception as error:
            evidence['restoration_error'] = str(error)
            evidence['completed'] = False
            raise
        finally:
            save(); log.close()
    print('Independent LED startup worker acceptance and restoration completed')


if __name__ == '__main__':
    main()
