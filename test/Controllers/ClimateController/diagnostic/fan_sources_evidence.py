"""Strict, latest-response parsers for non-secret fan source observations."""
import math
import re


def parse_sources(text):
    text=re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)
    start=text.rfind("FAN_CONFIG ")
    if start<0:
        raise ValueError("Missing fan configuration")
    text=text[start:]
    number=r"(-?\d+(?:\.\d+)?|nan)"
    config=re.search(r"FAN_CONFIG mode=([0-3]) object_on="+number+r" object_off="+number+r" delta_on="+number+r" delta_off="+number+r"[\r\n]",text)
    if not config:
        raise ValueError("Incomplete fan configuration")
    result={"mode":int(config[1]),"object_on":float(config[2]),"object_off":float(config[3]),
            "delta_on":float(config[4]),"delta_off":float(config[5]),"sources":{},"slots":{}}
    if not all(math.isfinite(result[key]) for key in ("object_on","object_off","delta_on","delta_off")):
        raise ValueError("Nonfinite configuration")
    if not -9<=result["delta_off"]<result["delta_on"]<=9 or not 20<=result["object_off"]<result["object_on"]<=99:
        raise ValueError("Invalid threshold configuration")
    for name in ("object","ambient"):
        match=re.search(r"SOURCE "+name+r" configured=([0-3]) effective=([0-3]) status=([0-2]) valid=([01]) temperature="+number+r" age_ms=(\d+)[\r\n]",text)
        if not match:
            raise ValueError("Incomplete source: "+name)
        value={"configured":int(match[1]),"effective":int(match[2]),"status":int(match[3]),"valid":bool(int(match[4])),
               "temperature":float(match[5]),"age_ms":int(match[6])}
        if value["valid"] and (not math.isfinite(value["temperature"]) or value["status"]==2):
            raise ValueError("Inconsistent valid source")
        if not value["valid"] and value["status"]!=2:
            raise ValueError("Invalid source lacks invalid status")
        if value["valid"] and value["status"]!=(0 if value["configured"]==value["effective"] else 1):
            raise ValueError("Configured/effective source health mismatch")
        if not math.isfinite(value["temperature"]):value["temperature"]=None
        result["sources"][name]=value
    difference=re.search(r"DIFFERENCE valid=([01]) temperature="+number+r"[\r\n]",text)
    if not difference:
        raise ValueError("Missing difference")
    result["difference_valid"]=bool(int(difference[1]))
    result["difference"]=float(difference[2]) if result["difference_valid"] else None
    if result["difference_valid"]!=all(value["valid"] for value in result["sources"].values()):
        raise ValueError("Difference validity does not match inputs")
    if result["difference_valid"]:
        if not all(value["valid"] for value in result["sources"].values()) or not math.isfinite(result["difference"]):
            raise ValueError("Difference lacks valid inputs")
        expected=result["sources"]["object"]["temperature"]-result["sources"]["ambient"]["temperature"]
        if abs(result["difference"]-expected)>.025:
            raise ValueError("Difference sign or value mismatch")
    for slot in range(1,4):
        match=re.search(r"EXT"+str(slot)+r" state=([0-2]) value="+number+r" last_update_ms=(\d+) age_ms=(\d+)[\r\n]",text)
        if not match:
            raise ValueError("Missing slot "+str(slot))
        result["slots"][slot]={"state":int(match[1]),"value":float(match[2]),"last_update_ms":int(match[3]),"age_ms":int(match[4])}
        if int(match[1])!=0 and (not math.isfinite(float(match[2])) or not -50<=float(match[2])<=200):
            raise ValueError("Invalid populated slot temperature")
    return result


def config_values(snapshot):
    return {key:snapshot[key] for key in ("mode","object_on","object_off","delta_on","delta_off")}|{
        "object_source":snapshot["sources"]["object"]["configured"],
        "ambient_source":snapshot["sources"]["ambient"]["configured"]}


def require_empty_slots(snapshot):
    if any(slot["state"]!=0 or slot["last_update_ms"]!=0 for slot in snapshot["slots"].values()):
        raise ValueError("External slots contain foreign or restored data")
