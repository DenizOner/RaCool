"""Strict language-independent system CLI evidence parsing."""
import re

def parse_state(text):
    records = re.findall(r'^RACOOL_CLI v=1 type=system_status state=(-?\d+)\r?$', text, re.M)
    if len(records) != 1 or not 0 <= int(records[0]) <= 7:
        raise RuntimeError('Missing unambiguous valid system state')
    return int(records[0])

def parse_result(text):
    records = re.findall(r'^RACOOL_CLI v=1 type=system_state result=(applied|rejected) state=(-?\d+)(?: reason=([a-z_]+))?\r?$', text, re.M)
    if len(records) != 1:
        raise RuntimeError('Missing unambiguous system action result')
    result, state, reason = records[0]; state = int(state)
    if result == 'applied':
        if not 0 <= state <= 7 or reason: raise RuntimeError('Invalid applied system result')
    elif reason not in ('invalid', 'unapplied') or not -1 <= state <= 7:
        raise RuntimeError('Invalid rejected system result')
    return {'result': result, 'state': state, 'reason': reason}
