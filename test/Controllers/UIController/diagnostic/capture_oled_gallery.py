"""Capture reachable TR/EN menu views, temporarily changing locale/navigation without confirming actions."""
import argparse
import sys,re,json,time,codecs
from pathlib import Path
import serial
sys.path.insert(0,str(Path(__file__).resolve().parents[3]))
from Core.Localization.diagnostic.verify_language import parse_config,parse_set,parse_screen
from Controllers.MenuManager.diagnostic.menu_cli_evidence import menu_state
from Hardware.OLED.diagnostic.frame_evidence import parse_frame,write_png
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--output',required=True)
    args=parser.parse_args()
    root=Path(args.output);root.mkdir(parents=True,exist_ok=True)
    text=Path('include/Controllers/MenuManager/MenuManagerTypes.h').read_text(encoding='utf-8').split('typedef enum {',1)[1].split('} UIState',1)[0]
    names=re.findall(r'\b(UI_STATE_\w+)\b',re.sub(r'//[^\n]*','',text));ids=dict((name,index) for index,name in enumerate(names))
    nav=Path('src/Controllers/MenuManager/MenuNavigator.c').read_text(encoding='utf-8')
    reachable=set(re.findall(r'UI_STATE_\w+',nav.split('static const MenuCategoryDef categories')[0]));reachable.update(['UI_STATE_MONITOR','UI_STATE_MENU_ROOT'])
    category=set(re.findall(r'\{\s*(UI_STATE_\w+)\s*,\s*(?:\w+Children|children_\w+)',nav));special={'UI_STATE_BACK','UI_STATE_EXIT'}
    records=[];baseline=None;skipped=[]
    port=serial.Serial(port=None,baudrate=115200,timeout=.1);port.dtr=port.rts=False;port.port=args.port
    with port,(root/'serial.log').open('w',encoding='utf-8') as log:
     def command(text,accept,timeout=12):
      port.reset_input_buffer();port.write((text+'\r\n').encode());log.write('\nCOMMAND: '+text+'\n');response='';decoder=codecs.getincrementaldecoder('utf-8')('replace');deadline=time.monotonic()+timeout
      while time.monotonic()<deadline:
       chunk=decoder.decode(port.read(port.in_waiting or 1));response+=chunk;log.write(chunk);log.flush()
       if accept(response):return response
      raise RuntimeError('Missing command evidence: '+text)
     def screen():return parse_screen(command('UI STATUS --machine',lambda t:parse_screen(t) is not None))
     def reset():command('menu reset --machine',lambda t:'type=menu_navigation result=queued state=0' in t);time.sleep(.3)
     def capture(label):
      response=command('oled frame --machine',lambda t:re.search(r'^RACOOL_CLI v=1 type=oled_frame_end seq=\d+ fnv=[0-9a-f]{8}\r*\n',t,re.M))
      frame=parse_frame(response);prefix=root/label
      prefix.with_suffix('.log').write_text(response,encoding='utf-8');prefix.with_suffix('.bin').write_bytes(frame['pixels']);write_png(frame,prefix.with_suffix('.png'));write_png(frame,prefix.with_name(prefix.name+'-8x').with_suffix('.png'),8)
      records.append({'label':label,**{k:v for k,v in frame.items() if k!='pixels'}})
      (root/'frames.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
     session=DeveloperSession(lambda text:command(text,has_mode_response))
     try:
      session.open()
      original_menu=menu_state(command('menu status --machine',lambda t:menu_state(t) is not None))
      if original_menu!=0:raise RuntimeError('Foreign menu session; no navigation issued')
      baseline={'language':parse_config(command('LANGUAGE CONFIG --machine',lambda t:parse_config(t) is not None)),'screen':screen()}
      (root/'baseline.json').write_text(json.dumps(baseline,indent=2),encoding='utf-8')
      command('UI WAKE --machine',lambda t:'type=ui_command command=WAKE result=queued error=0' in t);time.sleep(.5)
      for locale in ['tr','en']:
       actual=parse_set(command('LANGUAGE SET '+locale+' --machine',lambda t:parse_set(t) is not None))
       if actual['result']!='applied':raise RuntimeError('Locale not applied')
       reset();time.sleep(.5);capture(locale+'-monitor')
       for name in names:
        if name not in reachable or name=='UI_STATE_MONITOR' or name in special:continue
        state=ids[name]
        response=command(f'menu visible {state} --machine',lambda t:re.search(rf'type=menu_visibility state={state} visible=[01]',t))
        if re.search(rf'type=menu_visibility state={state} visible=0',response):
         skipped.append({'locale':locale,'state':state,'name':name,'reason':'runtime_visibility_policy'})
         (root/'skipped.json').write_text(json.dumps(skipped,indent=2),encoding='utf-8')
         continue
        reset()
        state=ids[name];command(f'menu nav {state} --machine',lambda t:f'type=menu_navigation result=queued state={state}' in t);time.sleep(.4)
        current=menu_state(command('menu status --machine',lambda t:menu_state(t) is not None))
        if current!=state:raise RuntimeError('Navigation did not apply')
        capture(locale+'-'+name.lower()+'-list')
        if name not in category and name!='UI_STATE_MENU_ROOT':
         # First click only enters the editor/NO confirmation; no encoder or confirm command.
         command('menu click --machine',lambda t:'type=menu_input result=accepted input=button' in t);time.sleep(.5)
         capture(locale+'-'+name.lower()+'-editor')
     finally:
      try:
       if baseline:
        reset();actual=parse_set(command('LANGUAGE SET '+baseline['language']['saved']+' --machine',lambda t:parse_set(t) is not None))
        if actual['result']!='applied':raise RuntimeError('Locale restore failed')
        restored_language=parse_config(command('LANGUAGE CONFIG --machine',lambda t:parse_config(t) is not None))
        if restored_language['saved']!=baseline['language']['saved'] or restored_language['active']!=baseline['language']['active']:raise RuntimeError('Locale restoration readback mismatch')
        if menu_state(command('menu status --machine',lambda t:menu_state(t) is not None))!=0:raise RuntimeError('Menu restoration readback mismatch')
        if baseline['screen']=='SUSPENDED':
         deadline=time.monotonic()+150
         while screen()!='SUSPENDED':
          if time.monotonic()>deadline:raise RuntimeError('Screen restore timeout')
          time.sleep(1)
        (root/'restoration.json').write_text(json.dumps({'restored':True,'baseline':baseline}),encoding='utf-8')
      finally:
       session.close()
    print('Captured frames:',len(records))

if __name__=="__main__":main()
