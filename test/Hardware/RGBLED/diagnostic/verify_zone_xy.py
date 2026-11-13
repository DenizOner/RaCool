"""Verify five persisted XY pairs through CLI and the real menu worker."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[3]))
from Controllers.MenuManager.diagnostic.menu_cli_evidence import menu_state
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners

ZONES=('cold','warm','hot','danger','freeze')

def menu_states(root):
    text=(root/'include/Controllers/MenuManager/MenuManagerTypes.h').read_text(encoding='utf-8')
    text=re.sub(r'//[^\n]*|/\*.*?\*/','',text,flags=re.S)
    body=text.split('typedef enum {',1)[1].split('} UIState;',1)[0]
    names={};value=0
    for item in body.split(','):
        if not item.strip():continue
        match=re.fullmatch(r'\s*(UI_STATE_\w+)\s*(?:=\s*(0x[0-9a-fA-F]+|[0-9]+))?\s*',item)
        if not match:raise ValueError('Unsupported menu enum expression')
        if match[2] is not None:value=int(match[2],0)
        names[match[1]]=value;value+=1
    return {zone:names['UI_STATE_SET_RGB_'+zone.upper()] for zone in ZONES}

def blue_xy(root):
    text=(root/'include/Hardware/RGBLED/RGBLEDConfig.h').read_text(encoding='utf-8')
    values=[]
    for axis in ('X','Y'):
        matches=re.findall(r'^#define\s+RGBLED_PRESET_BLUE_'+axis+r'\s+(\d+)u\s*$',text,re.M)
        if len(matches)!=1:raise ValueError('Missing or ambiguous blue preset')
        values.append(int(matches[0]))
    return tuple(values)

def parse_xy(text,zone):
    start=text.rfind('RACOOL_CLI v=1 type=zone_xy zone='+zone+' ')
    match=re.match(r'RACOOL_CLI v=1 type=zone_xy zone='+re.escape(zone)+r' x=(\d+) y=(\d+)\r*\n',text[start:]) if start>=0 else None
    if not match:return None
    x,y=map(int,match.groups())
    return (x,y) if 0<=x<=65279 and 1<=y<=65279 else None

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--output',required=True)
    parser.add_argument('--first-state',type=int,help='Deprecated legacy XY state; current preset states are discovered from source')
    args=parser.parse_args();prefix=Path(args.output)
    result={'completed':False,'restored':False,'physical_output_verified':False,'checks':[]}
    root=Path(__file__).resolve().parents[4];states=menu_states(root);preset=blue_xy(root)
    original={};owned={};menu_owned=False;port=serial.Serial(port=None,baudrate=115200,timeout=0.1)
    port.dtr=port.rts=False;port.port=args.port
    with port,prefix.with_suffix('.log').open('w',encoding='utf-8') as log:
        def command(text,accept,timeout=8):
            port.reset_input_buffer();port.write((text+'\r\n').encode())
            log.write('\nCOMMAND: '+text+'\n');received='';deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                chunk=port.read(port.in_waiting or 1).decode('utf-8',errors='replace')
                received+=chunk;log.write(chunk);log.flush()
                if accept(received):return received
            raise RuntimeError('Missing fresh command result: '+text)
        def read(zone,expected=None):
            for attempt in range(8):
                actual=parse_xy(command('SYSTEM CONFIG XY '+zone+' --machine',lambda text:parse_xy(text,zone) is not None),zone)
                if expected is None or actual==expected:return actual
                time.sleep(0.2)
            raise RuntimeError('XY readback mismatch: '+zone)
        def apply(zone,value):
            command(f'SYSTEM CONFIG XY {zone} {value[0]} {value[1]} --machine',lambda text:'RACOOL_CLI v=1 type=system_command result=completed' in text)
            return read(zone,value)
        def menu(expected=None):
            text=command('menu status --machine',lambda text:menu_state(text) is not None)
            actual=menu_state(text)
            if actual is None or (expected is not None and actual!=expected):raise RuntimeError('Unexpected menu state')
            return actual
        session=DeveloperSession(lambda text:command(text,has_mode_response))
        try:
            session.open()
            if menu()!=0:raise RuntimeError('Refusing foreign menu session')
            for zone in ZONES:original[zone]=read(zone)
            for zone,state in states.items():
                visible=command(f'menu visible {state} --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_visibility state=\d+ visible=[01]\r*$',text)))
                if not re.search(rf'(?m)^RACOOL_CLI v=1 type=menu_visibility state={state} visible=1\r*$',visible):
                    raise RuntimeError('Preset editor is not visible: '+zone)
            result['original']=original
            status=command('UI STATUS --machine',lambda t: re.search(r'RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle',t) is not None)
            result['original_screen']=re.findall(r'RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle',status)[-1]
            prefix.with_suffix('.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
            command('UI WAKE --machine',lambda text:'RACOOL_CLI v=1 type=ui_command command=WAKE result=queued error=0' in text)
            menu_owned=True
            time.sleep(0.5);menu(0)
            for index,zone in enumerate(ZONES):
                value=(12000+1000*index,21000+1000*index)
                owned[zone]={original[zone],value,preset}
                apply(zone,value)
                state=states[zone]
                command(f'menu nav {state} --machine',lambda text:'RACOOL_CLI v=1 type=menu_navigation result=queued state=' in text)
                time.sleep(0.3);menu(state)
                command('menu click --machine',lambda text:'RACOOL_CLI v=1 type=menu_input result=accepted input=button' in text);time.sleep(0.4)
                command('menu enc 1 --machine',lambda text:'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in text);time.sleep(0.4)
                read(zone,value)
                command('menu click --machine',lambda text:'RACOOL_CLI v=1 type=menu_input result=accepted input=button' in text);time.sleep(0.4)
                read(zone,preset)
                result['checks'].append({'zone':zone,'custom_before_save':value,'value':preset})
            command('menu reset --machine',lambda text:'RACOOL_CLI v=1 type=menu_navigation result=queued' in text);time.sleep(0.3);menu(0)
            menu_text=command('menu status --machine',lambda text:menu_state(text) is not None)
            system=command('sys status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$',text)))
            require_idle_owners(menu_text,system)
            command('power restart 500 --machine',lambda text:'RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0' in text)
            boot='';deadline=time.monotonic()+40
            while time.monotonic()<deadline:
                try:chunk=port.read(port.in_waiting or 1).decode('utf-8',errors='replace')
                except serial.SerialException:
                    port.close();time.sleep(0.3)
                    try:port.open()
                    except serial.SerialException:continue
                    continue
                boot+=chunk;log.write(chunk);log.flush()
                marker=boot.rfind('ESP-ROM:')
                if marker>=0 and 'racool>' in boot[marker:] and 'SYSTEM INITIALIZATION COMPLETE' in boot[marker:]:break
            else:raise RuntimeError('Missing fresh boot')
            time.sleep(3)
            session.after_boot()
            for check in result['checks']:read(check['zone'],tuple(check['value']))
            result['fresh_boot_persistence']=True;result['completed']=True
        except Exception as error:result['error']=str(error)
        finally:
            failures=[]
            for zone,allowed in owned.items():
                try:
                    current=read(zone)
                    if current not in allowed:raise RuntimeError('Refusing foreign XY change: '+zone)
                    if current!=original[zone]:apply(zone,original[zone])
                except Exception as error:failures.append(str(error))
            if menu_owned:
                try:command('menu reset --machine',lambda text:'RACOOL_CLI v=1 type=menu_navigation result=queued' in text);time.sleep(0.3);menu(0)
                except Exception as error:failures.append(str(error))
            if result.get('original_screen')=='suspended':
                try:
                    deadline=time.monotonic()+150
                    while True:
                        status=command('UI STATUS --machine',lambda t: re.search(r'RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle',t) is not None)
                        if 'type=ui screen=suspended service=2 boot=idle' in status: break
                        if time.monotonic()>deadline: raise RuntimeError('Screen sleep restoration failed')
                        time.sleep(1)
                except Exception as error:failures.append(str(error))
            try:session.close()
            except Exception as error:failures.append(str(error))
            result['restored']=len(original)==5 and not failures
            if failures:result['restoration_errors']=failures
            prefix.with_suffix('.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['completed'] and result['restored'] else 1

if __name__=='__main__':raise SystemExit(main())
