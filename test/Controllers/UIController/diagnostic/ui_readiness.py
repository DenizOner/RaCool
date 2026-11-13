"""Bounded read-only boot readiness checks before mutable menu tests."""
import re
import time


def status_record(text):
    matches = re.findall(r"^RACOOL_CLI v=1 type=ui screen=(?:active|suspended) service=(\d+) boot=(running|idle)\r*\n", text, re.M)
    return matches[-1] if matches else None


def boot_state(text):
    record = status_record(text)
    return record[1].upper() if record else None


def wait_for_ui_ready(command, attempts=12, pause=time.sleep):
    for _ in range(attempts):
        text = command('UI STATUS --machine', lambda response: boot_state(response) is not None)
        if status_record(text) == ('2', 'idle'):
            return
        pause(1)
    raise RuntimeError('UI boot animation did not finish; no menu mutations issued')
