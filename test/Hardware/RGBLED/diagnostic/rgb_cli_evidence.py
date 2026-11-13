"""Strict worker-state evidence, independent of translated console headings."""
import re


def parse_rgb_status(text):
    rows = re.findall(r'(?m)^RACOOL_CLI [^\r\n]*type=rgb_status[^\r\n]*(?:\r*\n|$)', text)
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
    if fields.get('v') != '1' or fields.get('type') != 'rgb_status':
        return None
    limits = dict(brightness=255, mode=3, target_r=255, target_g=255,
                  target_b=255, priority=3, animation=22, on=1, fading=1,
                  current_r=255, current_g=255, current_b=255)
    result = {}
    for key, maximum in limits.items():
        value = fields.get(key, '')
        if not re.fullmatch(r'[0-9]+', value) or len(value) > 3:
            return None
        result[key] = int(value)
        if result[key] > maximum:
            return None
    return result
