"""Validate fresh serial sensor/diagnostic responses without changing settings."""
import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[3]))
from Hardware.InfraredTemperatureSensor.diagnostic.ir_cli_evidence import parse_status, parse_stats


def validate(text, kind):
    if kind not in ('infrared', 'diagnostics'):
        raise ValueError('Unknown health evidence kind')
    clean = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', text)
    commands = clean.split('COMMAND: ')[1:]
    expected = ['ir_temp stats --machine', 'ir_temp status --machine', 'ir_temp stats --machine'] if kind == 'infrared' else ['DIAG METRICS --machine', 'DIAG METRICS --machine']
    if len(commands) != len(expected) or any(not response.startswith(command + '\n') for response, command in zip(commands, expected)):
        raise ValueError('Missing or out-of-order command evidence')
    if kind == 'infrared':
        def stats(response):
            values=parse_stats(response)
            return [values[key] for key in ('reads','pec_errors','i2c_errors')]
        before, after = stats(commands[0]), stats(commands[2])
        if after[0] <= before[0] or any(before[1:] + after[1:]):
            raise ValueError('Sensor did not advance clean measurements')
        sample=parse_status(commands[1]);temperatures=[sample['object_c'],sample['ambient_c']]
        return {'before': before, 'after': after, 'temperatures_c': temperatures}
    snapshots = []
    for response in commands:
        records=[line for line in re.findall(r'[^\n]*(?:\n|$)',response) if line.startswith('RACOOL_CLI ') and 'type=diagnostic_metrics' in line]
        match=re.fullmatch(r'RACOOL_CLI v=1 type=diagnostic_metrics free_heap=(\d+) min_free_heap=(\d+) largest_block=(\d+) uptime_s=(\d+) tasks=(\d+) score=(\d+) known=1\r*\n',records[-1] if records else '')
        if not match:raise ValueError('Incomplete or unknown diagnostics metrics')
        values=dict(zip(['Free Heap','Min Free Heap','Largest Block','Uptime','Active Tasks','Health Score'],map(int,match.groups())))
        if not 0 <= values['Health Score'] <= 100 or not values['Free Heap'] or not values['Active Tasks']:
            raise ValueError('Invalid diagnostics metrics')
        snapshots.append(values)
    if snapshots[1]['Uptime'] <= snapshots[0]['Uptime']:
        raise ValueError('Diagnostics uptime did not advance')
    return {'snapshots': snapshots, 'scope': 'Status response; not async self-test or complete system health acceptance'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=['infrared', 'diagnostics'], required=True)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    log = prefix.with_suffix('.log')
    commands = ['ir_temp stats --machine', 'ir_temp status --machine', 'ir_temp stats --machine'] if args.kind == 'infrared' else ['DIAG METRICS --machine', 'DIAG METRICS --machine']
    invocation = [sys.executable, str(Path(__file__).resolve().parents[3] / 'Communication/DebugConsole/diagnostic/serial_capture.py'), '--port', args.port,
                  '--duration', '10', '--output', str(log), '--developer']
    for command in commands:
        invocation.extend(['--command', command])
    run = subprocess.run(invocation, check=False, timeout=70)
    result = {'completed': False, 'kind': args.kind}
    try:
        if run.returncode:
            raise ValueError('Serial capture failed')
        result['evidence'] = validate(log.read_text(encoding='utf-8'), args.kind)
        result['completed'] = True
    except (ValueError, OSError) as error:
        result['error'] = str(error)
    prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
