"""Exercise the real menu locale transaction; restore locale, monitor and sleep."""
import argparse
import codecs
import json
import re
import sys
import time
from pathlib import Path
import serial

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'test'))
from Core.Localization.diagnostic.verify_language import parse_locale,parse_screen,parse_config,parse_set,parse_catalogs
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response


def language_state(header):
    source=re.sub(r'/\*[\s\S]*?\*/|//[^\n]*','',header)
    block=re.search(r'typedef\s+enum\s*\{([\s\S]*?)\}\s*UIState\s*;',source)
    if not block:raise ValueError('Missing UIState enumeration')
    value=-1
    for entry in block[1].split(','):
        match=re.fullmatch(r'\s*(UI_STATE_\w+)\s*(?:=\s*(\d+)\s*)?',entry)
        if not match:raise ValueError('Unsupported UI state expression')
        value=int(match[2]) if match[2] else value+1
        if match[1]=='UI_STATE_SET_LANGUAGE':return value
    raise ValueError('Missing language menu state')


from Controllers.MenuManager.diagnostic.menu_cli_evidence import menu_state as parse_menu


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--output',required=True)
    args=parser.parse_args();prefix=Path(args.output)
    state=language_state((ROOT/'include/Controllers/MenuManager/MenuManagerTypes.h').read_text(encoding='utf-8'))
    result={'completed':False,'restored':False,'original_locale':None,'original_screen':None,'checks':[],'state':state}
    def save():prefix.with_suffix('.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    port=serial.Serial(port=None,baudrate=115200,timeout=0.1);port.dtr=port.rts=False;port.port=args.port
    admitted=False
    with port,prefix.with_suffix('.log').open('w',encoding='utf-8') as log:
        def command(text,accept,timeout=8):
            port.reset_input_buffer();log.write('\nCOMMAND: '+text+'\n');port.write((text+'\r\n').encode())
            decoder=codecs.getincrementaldecoder('utf-8')('replace');response='';deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                chunk=decoder.decode(read_available(port,deadline,log.write))
                response+=chunk;log.write(chunk);log.flush()
                if accept(response):return response
            raise RuntimeError('No fresh command result: '+text)
        def locale():return parse_locale(command('LANGUAGE --machine',lambda text:parse_locale(text) is not None))
        def menu():return parse_menu(command('menu status --machine',lambda text:parse_menu(text) is not None))
        def screen():return parse_screen(command('UI STATUS --machine',lambda text:parse_screen(text) is not None))
        def wait_locale(expected):
            deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                if locale()==expected:return
                time.sleep(0.15)
            raise RuntimeError('Menu did not apply requested locale')
        session=DeveloperSession(lambda text:command(text,has_mode_response))
        try:
            session.open()
            baseline=parse_config(command('LANGUAGE CONFIG --machine',lambda text:parse_config(text) is not None))
            result['configuration_baseline']=baseline;save()
            if not baseline['restorable'] or baseline['saved']!=baseline['active']:raise RuntimeError('Refusing unavailable/fallback locale baseline')
            original=baseline['saved'];result['original_locale']=original
            if menu()!=0:raise RuntimeError('Refusing foreign menu session')
            result['original_screen']=screen()
            config=command('UI CONFIG --machine',lambda text:'type=ui_config' in text and text.endswith('\n'))
            timeout=re.search(r'^RACOOL_CLI v=1 type=ui_config screen_timeout_s=(\d+) display_mode=[12]\r*$',config,re.M)
            if result['original_screen']=='SUSPENDED' and (not timeout or int(timeout[1])>120):
                raise RuntimeError('Cannot guarantee bounded restoration of sleeping panel')
            catalogs=parse_catalogs(command('LANGUAGE LIST --machine',lambda text:parse_catalogs(text) is not None))
            if not {'tr','en',original}.issubset(catalogs):raise RuntimeError('Required firmware catalogs missing')
            save();admitted=True
            command('UI WAKE --machine',lambda text:'RACOOL_CLI v=1 type=ui_command command=WAKE result=queued error=0' in text)
            time.sleep(0.6)
            if menu()!=0:raise RuntimeError('Wake did not return to monitor')
            command('menu nav '+str(state)+' --machine',lambda text:'RACOOL_CLI v=1 type=menu_navigation result=queued state=' in text)
            deadline=time.monotonic()+8
            while menu()!=state:
                if time.monotonic()>deadline:raise RuntimeError('Menu navigation was not applied')
                time.sleep(0.15)
            count=len(catalogs)
            observed={original}
            for _ in range(count):
                before=locale()
                command('menu click --machine',lambda text:'RACOOL_CLI v=1 type=menu_input result=accepted input=button' in text);time.sleep(0.4)
                command('menu enc 1 --machine',lambda text:'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in text)
                time.sleep(0.4)
                if locale()!=before:raise RuntimeError('Unsaved language draft changed the active locale')
                command('menu click --machine',lambda text:'RACOOL_CLI v=1 type=menu_input result=accepted input=button' in text)
                deadline=time.monotonic()+8
                after=locale()
                while after==before:
                    if time.monotonic()>deadline:raise RuntimeError('Menu encoder did not change locale')
                    time.sleep(0.15);after=locale()
                if menu()!=state:raise RuntimeError('Locale change escaped language editor')
                if screen()!='ACTIVE':raise RuntimeError('Language selection did not keep explicitly awakened panel active')
                observed.add(after);result['checks'].append({'from':before,'to':after,'worker_state':state})
            if not {'tr','en'}.issubset(observed):raise RuntimeError('Both product locales were not exercised')
            result['completed']=True
        except Exception as error:result['error']=str(error)
        finally:
            if admitted:
                failures=[]
                try:
                    original=result['original_locale']
                    outcome=parse_set(command('LANGUAGE SET '+original+' --machine',lambda text:parse_set(text) is not None))
                    if outcome['result']!='applied' or outcome['locale']!=original:raise RuntimeError('Locale restoration write rejected')
                    wait_locale(original)
                    restored=parse_config(command('LANGUAGE CONFIG --machine',lambda text:parse_config(text) is not None))
                    if restored!=result['configuration_baseline']:raise RuntimeError('Saved locale baseline not restored')
                except Exception as error:failures.append(str(error))
                try:
                    command('menu reset --machine',lambda text:'RACOOL_CLI v=1 type=menu_navigation result=queued state=' in text)
                    deadline=time.monotonic()+8
                    while menu()!=0:
                        if time.monotonic()>deadline:raise RuntimeError('Monitor restoration failed')
                        time.sleep(0.15)
                    if result['original_screen']=='SUSPENDED':
                        deadline=time.monotonic()+150
                        while screen()!='SUSPENDED':
                            if time.monotonic()>deadline:raise RuntimeError('Sleep restoration failed')
                            time.sleep(1)
                except Exception as error:failures.append(str(error))
                result['restore_errors']=failures;result['restored']=not failures
            try:
                session.close()
            except Exception as error:
                result.setdefault('restore_errors',[]).append(str(error));result['restored']=False
            save()
    if not result['completed'] or not result['restored']:raise SystemExit(1)
    print('Real OLED menu locale transaction and restoration accepted')


if __name__=='__main__':main()
