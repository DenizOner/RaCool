"""Read-only USB diagnostic capture; this is not hardware acceptance."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


import argparse
import os
import subprocess


def run_test(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default=os.environ.get('RACOOL_SERIAL_PORT'))
    parser.add_argument('--output', default='test/build/hardware-diagnostic.log')
    args = parser.parse_args(argv)
    if not args.port:
        parser.error('Pass --port or set RACOOL_SERIAL_PORT to the enumerated USB device.')
    helper = Path(__file__).resolve().parents[1] / 'Communication' / 'DebugConsole' / 'diagnostic' / 'serial_capture.py'
    command = [sys.executable, str(helper), '--port', args.port,
               '--duration', '30', '--output', args.output, '--developer']
    for diagnostic in ('power status', 'ZB INFO', 'CC STATUS', 'menu status --machine'):
        command.extend(['--command', diagnostic])
    result = subprocess.run(command, check=False)
    if result.returncode == 0:
        print('Diagnostic capture completed; command readback and hardware acceptance remain separate.')
    return result.returncode


if __name__ == '__main__':
    sys.exit(run_test())
