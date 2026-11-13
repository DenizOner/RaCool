"""Decode versioned menu worker state; admission is separate evidence."""
import re

def parse_status(text):
    records=re.findall(r'^RACOOL_CLI v=1 type=menu_status state=(\d+) registry_label_hex=([0-9a-f]*)\r*$',text,re.M)
    if not records:return None
    state,label=records[-1]
    if not 0<=int(state)<=65535 or len(label)>62:return None
    try:label=bytes.fromhex(label).decode('utf-8')
    except (ValueError,UnicodeDecodeError):return None
    if not label:return None
    return int(state),label

def menu_state(text):
    record=parse_status(text)
    return record[0] if record else None
