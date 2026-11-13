"""Strict versioned CPU sensor evidence, independent of human language."""
import math
import re


def record(text, kind):
    candidates=[line for line in re.findall(r"[^\n]*(?:\n|$)",text) if line.startswith('RACOOL_CLI ') and 'type='+kind in line]
    if not candidates or not candidates[-1].endswith('\n'):
        raise RuntimeError('Missing complete CPU machine record')
    tokens=candidates[-1].rstrip('\r\n').split(' ')
    if tokens[:3]!=['RACOOL_CLI','v=1','type='+kind]:
        raise RuntimeError('Unsupported CPU machine record')
    fields={}
    for token in tokens[3:]:
        pair=token.split('=',1)
        if len(pair)!=2 or not pair[1] or pair[0] in fields:
            raise RuntimeError('Invalid CPU record field')
        fields[pair[0]]=pair[1]
    return fields


def parse_status(text):
    fields=record(text,'cpu_sensor_status')
    if set(fields)!={'valid','temperature_c','offset_c','level','trend','state','sample_time_ms','age_ms'} or fields['valid']!='1':
        raise RuntimeError('Missing valid CPU sample')
    for key in ('level','trend','state','sample_time_ms'):
        if not re.fullmatch(r'\d+',fields[key]):raise RuntimeError('Invalid CPU integer')
    if not re.fullmatch(r'\d+',fields['age_ms']):raise RuntimeError('Unavailable CPU sample age')
    if not 0<=int(fields['level'])<=3 or not 0<=int(fields['trend'])<=3 or not 0<=int(fields['state'])<=4:
        raise RuntimeError('Invalid CPU state')
    temperature=float(fields['temperature_c']);offset=float(fields['offset_c'])
    if not math.isfinite(temperature) or temperature<=-273 or not math.isfinite(offset) or not -9.99<=offset<=9.99:
        raise RuntimeError('Invalid CPU temperature/calibration')
    return {'temperature':temperature,'offset':offset,'level':('NORMAL','WARNING','CRITICAL','EMERGENCY')[int(fields['level'])]}


def parse_read(text):
    fields=record(text,'cpu_sensor_read')
    if set(fields)!={'result','temperature_c'} or fields['result']!='read':raise RuntimeError('CPU read rejected')
    value=float(fields['temperature_c'])
    if not math.isfinite(value) or value<=-273:raise RuntimeError('Invalid live CPU sample')
    return value


def require_offset(text):
    if record(text,'cpu_sensor_command')!={'command':'offset','result':'applied','error':'0'}:
        raise RuntimeError('CPU calibration update rejected')
