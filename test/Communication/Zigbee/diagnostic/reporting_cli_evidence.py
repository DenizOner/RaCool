"""Read language-independent, bounded reporting interval records."""
import re
NAMES = ("fan", "ambient", "cpu", "health", "memory", "uptime")

def report_values(text, require_all=False):
    result={}
    for line in text.splitlines():
        if not line.startswith("RACOOL_CLI v=1 type=report_interval "): continue
        match=re.fullmatch(r"RACOOL_CLI v=1 type=report_interval report=(fan|ambient|cpu|health|memory|uptime) seconds=(\d+) enabled=([01])",line)
        if not match: raise ValueError("Malformed report interval record")
        name,value,enabled=match.groups();value=int(value)
        if value>3600 or int(enabled)!=(value!=0) or name in result:
            raise ValueError("Inconsistent or duplicate report interval record")
        result[name]=value
    if require_all and len(result)!=6: raise ValueError("Incomplete reporting snapshot")
    return result
