"""Strict current climate machine records; human labels are never evidence."""
import math
import re


def record(text, kind):
    candidates=[line for line in re.findall(r"[^\n]*(?:\n|$)",text) if line.startswith("RACOOL_CLI ") and "type="+kind in line]
    if not candidates or not candidates[-1].endswith("\n"):
        raise ValueError("Missing complete climate machine record")
    line=candidates[-1].rstrip("\r\n")
    tokens=line.split(" ")
    if tokens[:3] != ["RACOOL_CLI","v=1","type="+kind]:
        raise ValueError("Unsupported climate machine record")
    fields={}
    for token in tokens[3:]:
        parts=token.split("=",1)
        if len(parts)!=2 or parts[0] in fields or not parts[1]:
            raise ValueError("Invalid climate field")
        fields[parts[0]]=parts[1]
    return fields


def parse_status(text):
    f=record(text,"climate_status")
    expected={"mode","on","stale","debouncing","ir_tick","object_c","ambient_c","cpu_c","open_c","close_c","object_cal_c","ambient_cal_c","enabled","manual","manual_state","strategy","object_source","ambient_source","delta_on","delta_off"}
    if set(f)!=expected:raise ValueError("Incomplete climate status")
    modes={0:"OBJECT_SENSITIVE",1:"MANUAL ON",2:"MANUAL OFF",3:"AMBIENT_DELTA"}
    if not re.fullmatch(r"[0-3]",f["mode"]):raise ValueError("Unconfirmed fan mode")
    for key in ("on","stale","debouncing","enabled","manual","manual_state"):
        if f[key] not in ("0","1"):raise ValueError("Invalid climate boolean")
    if not re.fullmatch(r"\d+",f["ir_tick"]) or not 0<=int(f["ir_tick"])<=0xffffffff:raise ValueError("Invalid IR tick")
    manual_mode=f["mode"] in ("1","2")
    if (f["manual"]=="1")!=manual_mode or (manual_mode and (f["manual_state"]=="1")!=(f["mode"]=="1")):
        raise ValueError("Inconsistent fan mode snapshot")
    if f["strategy"] not in ("0","1") or f["object_source"] not in ("0","1","2","3") or f["ambient_source"] not in ("0","1","2","3"):
        raise ValueError("Invalid climate source configuration")
    if not manual_mode and (f["strategy"]=="1") != (f["mode"]=="3"):
        raise ValueError("Inconsistent automatic fan strategy")
    for key in ("delta_on","delta_off"):
        if not re.fullmatch(r"-?\d+",f[key]) or not -90<=int(f[key])<=90:raise ValueError("Invalid delta threshold")
    if int(f["delta_on"])<=int(f["delta_off"]):raise ValueError("Invalid delta pair")
    numbers={}
    for key in expected-set(("mode","on","stale","debouncing","ir_tick","enabled","manual","manual_state","strategy","object_source","ambient_source","delta_on","delta_off")):
        value=float(f[key])
        if not math.isfinite(value):raise ValueError("Unavailable climate measurement")
        numbers[key]=value
    return {"mode":modes[int(f["mode"])],"on":f["on"]=="1","stale":f["stale"]=="1","debouncing":f["debouncing"]=="1","tick":int(f["ir_tick"]),
            "radiator":numbers["object_c"],"ambient":numbers["ambient_c"],"cpu":numbers["cpu_c"],"open":numbers["open_c"],"close":numbers["close_c"],
            "object_cal":numbers["object_cal_c"],"ambient_cal":numbers["ambient_cal_c"],"manual":f["manual"]=="1","manual_state":f["manual_state"]=="1",
            "enabled":"ENABLED" if f["enabled"]=="1" else "DISABLED","strategy":int(f["strategy"]),"object_source":int(f["object_source"]),"ambient_source":int(f["ambient_source"]),"delta_on":int(f["delta_on"]),"delta_off":int(f["delta_off"])}


def parse_override(text):
    f=record(text,"climate_cpu")
    if set(f)!={"override","cpu_c","warning_c","critical_c","emergency_c","polling_ms"} or f["override"] not in ("0","1"):
        raise ValueError("Incomplete CPU protection record")
    for key in ("cpu_c","warning_c","critical_c","emergency_c"):
        if not math.isfinite(float(f[key])):raise ValueError("Unavailable CPU protection measurement")
    if not re.fullmatch(r"\d+",f["polling_ms"]):raise ValueError("Invalid CPU poll interval")
    return f["override"]=="1"
