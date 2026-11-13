"""Strict IR sample/statistic records for live helper consumers."""
import math
import re


def fields(text, kind, expected):
    candidates=[line for line in re.findall(r'[^\n]*(?:\n|$)',text) if line.startswith('RACOOL_CLI ') and 'type='+kind in line]
    if not candidates or not candidates[-1].endswith('\n'):raise ValueError('Incomplete IR machine record')
    tokens=candidates[-1].rstrip('\r\n').split(' ')
    if tokens[:3]!=['RACOOL_CLI','v=1','type='+kind]:raise ValueError('Unsupported IR machine record')
    result={}
    for token in tokens[3:]:
        pair=token.split('=',1)
        if len(pair)!=2 or not pair[1] or pair[0] in result:raise ValueError('Invalid IR field')
        result[pair[0]]=pair[1]
    if set(result)!=set(expected):raise ValueError('Missing IR fields')
    return result


def parse_status(text):
    result=fields(text,'ir_sensor_status',('object_c','ambient_c','measured_time_us','age_ms','polling_ms'))
    for key in ('measured_time_us','age_ms','polling_ms'):
        if not re.fullmatch(r'\d+',result[key]):raise ValueError('Unavailable IR sample timing')
    values={key:float(result[key]) for key in ('object_c','ambient_c')}
    if any(not math.isfinite(value) or not -70<=value<=380 for value in values.values()):raise ValueError('Invalid IR temperatures')
    return {**values,**{key:int(result[key]) for key in ('measured_time_us','age_ms','polling_ms')}}


def parse_stats(text):
    result=fields(text,'ir_sensor_stats',('reads','pec_errors','i2c_errors','failure_kind','failure_error','failure_time_us','object_min_c','object_max_c','ambient_min_c','ambient_max_c'))
    for key in ('reads','pec_errors','i2c_errors','failure_kind'):
        if not re.fullmatch(r'\d+',result[key]) or int(result[key])>0xffffffff:raise ValueError('Invalid IR counter')
    if int(result['failure_kind'])>3:raise ValueError('Invalid IR failure kind')
    for key in ('failure_error','failure_time_us'):
        if not re.fullmatch(r'-?\d+',result[key]):raise ValueError('Invalid IR failure evidence')
    for key in ('object_min_c','object_max_c','ambient_min_c','ambient_max_c'):
        if not math.isfinite(float(result[key])):raise ValueError('Invalid IR statistic')
    return {key:int(result[key]) for key in ('reads','pec_errors','i2c_errors','failure_kind','failure_error','failure_time_us')}


def require_selftest(text):
    if fields(text,'ir_sensor_test',('result','error'))!={'result':'passed','error':'0'}:raise ValueError('IR self-test failed')


def require_poll(text, interval):
    expected={'command':'poll','result':'applied','error':'0','interval_ms':str(interval)}
    if fields(text,'ir_sensor_command',expected)!=expected:raise ValueError('IR polling change rejected')
