"""Strict built-in LED worker evidence, independent of human translations."""
import re


def parse_builtin_status(text):
    rows = re.findall(r'(?m)^RACOOL_CLI [^\r\n]*type=builtin_status[^\r\n]*(?:\r*\n|$)', text)
    if not rows or not rows[-1].endswith('\n'):
        return None
    fields = {}
    for token in rows[-1].strip().split()[1:]:
        if token.count('=') != 1:
            return None
        key, value = token.split('=')
        if key in fields:
            return None
        fields[key] = value
    if fields.get('v') != '1' or fields.get('type') != 'builtin_status':
        return None
    bounds = dict(on=(0,1), enabled=(0,1), brightness=(0,255), animation=(-1,20),
                  priority=(0,2), remaining_ms=(0,4294967295), plugins=(0,255),
                  driver_error=(-2147483648,2147483647))
    result = {}
    for key, (minimum, maximum) in bounds.items():
        value = fields.get(key,'')
        if not re.fullmatch(r'-?[0-9]+',value) or len(value) > 11:
            return None
        result[key] = int(value)
        if not minimum <= result[key] <= maximum:
            return None
    return result


def parse_builtin_command(text, command):
    rows = re.findall(r'(?m)^RACOOL_CLI v=1 type=builtin_command command=([^ ]+) result=(accepted|rejected) error=(-?[0-9]+)\r*\n',text)
    if not rows or rows[-1][0] != command:
        return None
    result, error = rows[-1][1], int(rows[-1][2])
    if (result == 'accepted') != (error == 0):
        return None
    return result
