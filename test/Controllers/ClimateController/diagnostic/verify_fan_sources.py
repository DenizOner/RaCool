"""Controlled fan-source/restart/freshness acceptance; restore saved state in finally."""
import argparse
import json
import re
import sys
import time
from pathlib import Path
import serial
sys.path.insert(0,str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners
from fan_sources_evidence import parse_sources,config_values,require_empty_slots
from verify_climate_stale import parse_status

def background_errors(text):
    """Return asynchronous error lines without discarding inter-command input."""
    clean=re.sub(r"\x1b\[[0-9;]*[A-Za-z]","",text)
    if "Guru Meditation" in clean or "ESP-ROM:" in clean or "assert failed" in clean:
        raise RuntimeError("Unexpected panic or reboot in serial output")
    # Console echo can interleave immediately before the log level.
    return re.findall(r'E \(\d+\) [^\r\n]*',clean)

def check_before_command(pending, restoring, evidence, value):
    errors=background_errors(pending)
    if errors:
        evidence.setdefault('asynchronous_errors',[]).extend(errors)
        evidence['completed']=False
        if not restoring:raise RuntimeError('Runtime error observed before command: '+value)

def collect_restart(receive, timeout=40, clock=None):
    """Observe one submitted restart without sending or retrying any command."""
    clock=clock or time.monotonic
    deadline=clock()+timeout; text=''; ready=None
    while clock()<deadline:
        text+=receive(deadline)
        clean=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',text)
        if clean.count('ESP-ROM:')>1 or 'Guru Meditation' in clean or 'assert failed' in clean:
            raise RuntimeError('Reboot loop or fatal runtime fault')
        markers=('ESP-ROM:','SW_CPU','SYSTEM INITIALIZATION COMPLETE','app_main: System ready.','racool>')
        if all(marker in clean for marker in markers) and ready is None:ready=clock()
        if ready is not None and clock()-ready>=2:
            return {'acknowledged':bool(re.search(r'RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0',clean)),
                    'errors':re.findall(r'E \(\d+\) [^\r\n]*',clean)}
    raise RuntimeError('No fresh completed software boot')

def close_developer_session(session, device_uncertain, evidence, save):
    if device_uncertain:
        evidence['completed']=False
        evidence['developer_mode_restore_error']='Restart outcome uncertain; refusing further mutations'
        save()
    else:
        session.close()

def result_exit_code(evidence):
    return 0 if evidence.get('completed') is True else 1

def verify_owned_restore_state(snapshot, original, commands):
    """Refuse cleanup if observed settings cannot come from this run's requests."""
    modes={'OBJECT_SENSITIVE':0,'ON':1,'OFF':2,'AMBIENT_DELTA':3}
    allowed={'mode':{original['mode']},'object_source':{original['object_source']},
             'ambient_source':{original['ambient_source']},
             'delta':{(original['delta_on'],original['delta_off'])}}
    slots={1:set(),2:set(),3:set()}
    for item in commands:
        parts=item['text'].split()
        if len(parts)<3 or parts[0]!='CLIMATE':continue
        try:
            if parts[1]=='MODE' and parts[2] in modes:allowed['mode'].add(modes[parts[2]])
            elif parts[1]=='SOURCE' and len(parts)>=4 and parts[2] in ('OBJECT','AMBIENT'):
                allowed[parts[2].lower()+'_source'].add(int(parts[3]))
            elif parts[1]=='DELTA' and len(parts)>=4:
                pair=(float(parts[2]),float(parts[3]))
                if pair[0]>pair[1]:allowed['delta'].add(pair)
            elif parts[1]=='EXTERNAL' and len(parts)>=4 and parts[3]!='CLEAR':
                slots[int(parts[2])].add(float(parts[3]))
        except (ValueError,KeyError):
            raise RuntimeError('Invalid owned command journal')
    current=config_values(snapshot)
    if (current['object_on']!=original['object_on'] or current['object_off']!=original['object_off'] or
        any(current[key] not in allowed[key] for key in ('mode','object_source','ambient_source')) or
        (current['delta_on'],current['delta_off']) not in allowed['delta']):
        raise RuntimeError('Refusing to overwrite foreign fan configuration')
    for slot,data in snapshot['slots'].items():
        if data['state'] and data['value'] not in slots[slot]:
            raise RuntimeError('Refusing to overwrite foreign measurement slot')
    return current


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",required=True)
    parser.add_argument("--output",required=True)
    parser.add_argument("--stale-window",action="store_true")
    args=parser.parse_args()
    prefix=Path(args.output);prefix.parent.mkdir(parents=True,exist_ok=True)
    evidence={"completed":False,"started":time.time(),"observations":[],"commands":[]}
    original=None;changed=False;restoring=False;device_uncertain=False
    port=serial.Serial(port=None,baudrate=115200,timeout=.1);port.dtr=port.rts=False;port.port=args.port
    with port,prefix.with_suffix(".log").open("w",encoding="utf-8") as log:
        def save():
            prefix.with_suffix(".json").write_text(json.dumps(evidence,indent=2,allow_nan=False),encoding="utf-8")
        def receive(deadline):
            text=read_available(port,deadline,log.write).decode("utf-8",errors="replace")
            log.write(text);log.flush();return text
        def command(value,marker,timeout=9):
            pending=""
            drain_deadline=time.monotonic()+timeout
            while port.in_waiting and time.monotonic()<drain_deadline:
                pending+=receive(drain_deadline)
            if port.in_waiting:raise RuntimeError('Serial input did not quiesce before command: '+value)
            check_before_command(pending,restoring,evidence,value)
            sequence=len(evidence["commands"])+1
            evidence["commands"].append({"sequence":sequence,"text":value,"submitted":time.time()})
            log.write(f"\nCOMMAND {sequence}: {value}\n");port.write((value+"\r\n").encode())
            # Pending bytes are already logged and checked; a response must be
            # matched only against bytes received after this command is sent.
            text='';deadline=time.monotonic()+timeout;matched=None
            while time.monotonic()<deadline:
                text+=receive(deadline)
                clean=re.sub(r"\x1b\[[0-9;]*[A-Za-z]","",text)
                if "Guru Meditation" in clean or "ESP-ROM:" in clean or "assert failed" in clean:
                    raise RuntimeError("Runtime error during "+value)
                start=clean.find("Executing: "+value)
                if start>=0 and re.search(marker,clean[start:]) and matched is None:matched=time.monotonic()
                if matched is not None and time.monotonic()-matched>=.15:
                    errors=background_errors(clean)
                    if errors:
                        evidence.setdefault('asynchronous_errors',[]).extend(errors)
                        evidence['completed']=False
                        if not restoring:raise RuntimeError('Runtime error observed after checked command: '+value)
                    return clean[start:]
            errors=background_errors(text)
            if errors:
                evidence.setdefault('asynchronous_errors',[]).extend(errors)
                evidence['completed']=False
                if not restoring:raise RuntimeError('Runtime error observed during command: '+value)
            raise RuntimeError("Missing checked command result: "+value)
        def sources():
            actual=parse_sources(command("CLIMATE SOURCES",r"EXT3 state=.*[\r\n]"))
            # Invalid internal sentinel is finite; invalid difference is represented by None.
            evidence["observations"].append({"timestamp":time.time(),"sources":actual});return actual
        def status():
            return parse_status(command("CLIMATE STATUS --machine",r"RACOOL_CLI v=1 type=climate_status[^\r\n]*[\r\n]"))
        def ready_status():
            deadline=time.monotonic()+25
            while time.monotonic()<deadline:
                actual=status()
                if -40<=actual["cpu"]<40 and not actual["stale"] and not actual["debouncing"]:return actual
                time.sleep(.25)
            raise RuntimeError("No fresh sensor/CPU state after boot")
        def mode(value):
            command("CLIMATE MODE "+value+" --machine",r"RACOOL_CLI v=1 type=climate_command command=MODE result=completed error=0[\r\n]")
        def source(name,value):
            command(f"CLIMATE SOURCE {name} {value} --machine",r"RACOOL_CLI v=1 type=climate_command command=SOURCE result=completed error=0[\r\n]")
        def delta(on,off):
            command(f"CLIMATE DELTA {on} {off} --machine",r"RACOOL_CLI v=1 type=climate_command command=DELTA result=completed error=0[\r\n]")
        def external(slot,value):
            command(f"CLIMATE EXTERNAL {slot} {value} --machine",r"RACOOL_CLI v=1 type=climate_command command=EXTERNAL result=completed error=0[\r\n]")
        def wait_output(on,timeout=12):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                actual=status()
                if actual["on"]==on and not actual["debouncing"]:
                    hardware=command("relay_info --machine",r"RACOOL_CLI v=1 type=relay_status state=[012] mode=[01] pin=\d+ level=[01][\r\n]")
                    gpio=int(re.search(r"^RACOOL_CLI v=1 type=relay_status state=[012] mode=[01] pin=\d+ level=([01])\r*$",hardware,re.M)[1])
                    config=Path("include/Hardware/Relay/RelayConfig.h").read_text(encoding="utf-8")
                    polarity=re.search(r"^#define\s+RELAY_ACTIVE_HIGH\s+(true|false)\s*$",config,re.M)
                    if not polarity or gpio!=int(on==(polarity[1]=="true")):raise RuntimeError("Worker/GPIO mismatch")
                    return actual
                time.sleep(.25)
            raise RuntimeError("No expected worker output")
        def restart():
            nonlocal device_uncertain
            menu=command('menu status --machine',r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$')
            system=command('sys status --machine',r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$')
            require_idle_owners(menu,system)
            value='power restart 500 --machine';pending='';drain_deadline=time.monotonic()+9
            while port.in_waiting and time.monotonic()<drain_deadline:pending+=receive(drain_deadline)
            if port.in_waiting:raise RuntimeError('Serial input did not quiesce before command: '+value)
            check_before_command(pending,restoring,evidence,value)
            sequence=len(evidence['commands'])+1
            evidence['commands'].append({'sequence':sequence,'text':value,'submitted':time.time()})
            log.write(f'\nCOMMAND {sequence}: {value}\n');log.flush()
            device_uncertain=True
            write_error=None
            try:port.write((value+'\r\n').encode())
            except (serial.SerialException,OSError) as error:write_error=str(error)
            boot=collect_restart(receive)
            device_uncertain=False
            if boot['errors']:
                evidence.setdefault('asynchronous_errors',[]).extend(boot['errors'])
                evidence['completed']=False
            session.after_boot()
            evidence.setdefault("boots",[]).append({"timestamp":time.time(),"new_rom_boot":True,"software_reason":True})
            if write_error or not boot['acknowledged']:
                raise RuntimeError('Restart acknowledgement missing or serial write ambiguous after fresh boot')
            if boot['errors'] and not restoring:raise RuntimeError('Runtime error observed during restart')
            for attempt in range(4):
                try:return sources()
                except RuntimeError as error:
                    if attempt==3 or not str(error).startswith('Missing checked command result: CLIMATE SOURCES'):raise
        session=DeveloperSession(lambda text:command(text,r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            initial=sources();require_empty_slots(initial);original=config_values(initial)
            initial_status=ready_status()
            if initial_status["cpu"]>=40 or initial_status["stale"] or initial_status["enabled"]!="ENABLED":
                raise RuntimeError("Refusing unsafe/nonfresh initial device")
            evidence["original"]=original;save() # Preserve baseline before every mutable action.
            changed=True
            source("OBJECT",0);source("AMBIENT",0);mode("OBJECT_SENSITIVE")
            evidence["internal_pair"]=sources()
            external(1,20);source("AMBIENT",1);mode("AMBIENT_DELTA");delta(2,1)
            mixed=sources()
            if mixed["sources"]["object"]["effective"]!=0 or mixed["sources"]["ambient"]["effective"]!=1:raise RuntimeError("Mixed source resolution failed")
            evidence["mixed_pair"]=mixed
            # Establish OFF before entering the dead band; an already-ON fan
            # correctly remains ON at 1.9 C after the mixed-source scenario.
            mode("OFF");wait_output(False)
            external(2,21.9);source("OBJECT",2);mode("AMBIENT_DELTA");wait_output(False)
            for value,expected in ((22,True),(23,True),(21.1,True),(21,False)):
                external(2,value);wait_output(expected)
                actual=sources()
                if actual["sources"]["object"]["effective"]!=2 or actual["sources"]["ambient"]["effective"]!=1:raise RuntimeError("Independent external pair failed")
            for on,off,start,end in ((0,-1,20,19),(-1,-2,19,18)):
                delta(on,off);external(2,start);wait_output(True);external(2,end);wait_output(False)
            before=config_values(sources())
            command("CLIMATE DELTA 1 1 --machine",r"RACOOL_CLI v=1 type=climate_command command=DELTA result=rejected error=-?\d+")
            command("CLIMATE DELTA -2 -1 --machine",r"RACOOL_CLI v=1 type=climate_command command=DELTA result=rejected error=-?\d+")
            if config_values(sources())!=before:raise RuntimeError("Invalid pair changed live settings")
            delta(2,1);external(2,23);wait_output(True)
            command("CLIMATE SAVE --machine",r"RACOOL_CLI v=1 type=climate_command command=SAVE result=completed error=0")
            expected=config_values(sources());after=restart();require_empty_slots(after)
            if config_values(after)!=expected:raise RuntimeError("Saved source/mode/threshold configuration lost on restart")
            evidence["restart_persistence"]={"expected":expected,"actual":config_values(after),"slots_empty":True}
            if args.stale_window:
                external(1,20);external(2,23);wait_output(True)
                baseline=sources();updates={slot:baseline["slots"][slot]["last_update_ms"] for slot in (1,2)}
                deadline=time.monotonic()+325
                while time.monotonic()<deadline:
                    actual=sources()
                    if any(actual["slots"][slot]["last_update_ms"]!=updates[slot] for slot in (1,2)):raise RuntimeError("Foreign heartbeat changed test slots")
                    if all(actual["slots"][slot]["state"]==2 for slot in (1,2)):
                        for name in ("object","ambient"):
                            if actual["sources"][name]["effective"]!=0 or actual["sources"][name]["status"]!=1:raise RuntimeError("No internal stale fallback")
                        if any(actual["slots"][slot]["age_ms"]<300000 for slot in (1,2)):raise RuntimeError("Premature stale boundary")
                        evidence["stale_fallback"]=actual;break
                    save();time.sleep(15)
                else:raise RuntimeError("No real 5 minute stale fallback")
                external(1,20);external(2,23);wait_output(True)
                recovered=sources()
                if recovered["sources"]["object"]["effective"]!=2 or recovered["sources"]["ambient"]["effective"]!=1:raise RuntimeError("External recovery did not resume selected sources")
                evidence["external_recovery"]=recovered
            evidence["completed"]=True
        except Exception as error:
            evidence["error"]=str(error)
            evidence["completed"]=False
            raise
        finally:
            try:
                if changed and original:
                    restoring=True
                    errors=[]
                    try:
                        if device_uncertain:raise RuntimeError('Restart outcome uncertain; refusing further mutations')
                        snapshot=sources()
                        current=verify_owned_restore_state(snapshot,original,evidence['commands'])
                        if current['mode']!=2:mode("OFF")
                        if current['object_source']!=original['object_source']:source("OBJECT",original["object_source"])
                        if current['ambient_source']!=original['ambient_source']:source("AMBIENT",original["ambient_source"])
                        if (current['delta_on'],current['delta_off'])!=(original['delta_on'],original['delta_off']):delta(original["delta_on"],original["delta_off"])
                        for slot in (1,2,3):
                            if snapshot['slots'][slot]['state']:external(slot,"CLEAR")
                        if original['mode']!=2:mode({0:"OBJECT_SENSITIVE",1:"ON",2:"OFF",3:"AMBIENT_DELTA"}[original["mode"]])
                        command("CLIMATE SAVE --machine",r"RACOOL_CLI v=1 type=climate_command command=SAVE result=completed error=0")
                        restored=restart();require_empty_slots(restored)
                        if config_values(restored)!=original:raise RuntimeError("Original configuration not restored")
                        evidence["restored"]={"configuration":config_values(restored),"slots_empty":True,"new_boot":True}
                        restored_status=ready_status()
                        evidence["restored"]["worker_status"]=restored_status
                        if original["mode"] in (1,2):wait_output(original["mode"]==1)
                    except Exception as error:
                        errors.append(str(error));evidence["completed"]=False
                    evidence["restore_errors"]=errors;save()
                    if errors:raise RuntimeError("Restoration failed: "+"; ".join(errors))
                else:save()
            finally:
                try:
                    close_developer_session(session,device_uncertain,evidence,save)
                except Exception as error:
                    evidence["completed"]=False;evidence["developer_mode_restore_error"]=str(error);save();raise
    print(json.dumps({"completed":evidence["completed"],"stale_fallback":bool(evidence.get("stale_fallback")),"restored":bool(evidence.get("restored"))}))
    return result_exit_code(evidence)


if __name__=="__main__":raise SystemExit(main())
