"""Capture RaCool serial evidence, optionally issuing CLI commands without resetting."""
import argparse
import codecs
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, parse_mode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
parser.add_argument('--duration', type=float, default=30)
parser.add_argument('--output', required=True)
parser.add_argument('--command', action='append', default=[])
parser.add_argument('--hardware-reset', action='store_true', help='Explicitly restart the native USB Serial/JTAG device before capture')
parser.add_argument('--developer', action='store_true', help='Temporarily enable Developer Tools and restore the session mode')
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.15)
port.dtr = False
port.rts = False
port.port = args.port
with port, Path(args.output).open('w', encoding='utf-8') as output:
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    if args.hardware_reset:
        output.write("HARDWARE_RESET: native USB Serial/JTAG RTS pulse\n")
        port.dtr = False
        port.rts = True
        port.dtr = port.dtr  # Windows usbser.sys needs a DTR request to apply RTS.
        time.sleep(0.2)
        port.rts = False
        port.dtr = port.dtr
        time.sleep(0.2)
        if args.developer:
            boot = ''; deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                chunk = read_available(port, deadline, output.write).decode('utf-8', errors='replace')
                boot += chunk; output.write(chunk); output.flush()
                start = boot.rfind('ESP-ROM:')
                if start >= 0 and 'racool>' in boot[start:] and 'SYSTEM INITIALIZATION COMPLETE' in boot[start:]: break
            else:
                raise RuntimeError('Fresh boot and command prompt were not observed')
    def exchange_mode(command):
        port.write((command + '\r\n').encode())
        response = ''; deadline = time.monotonic() + 8; matched_at = None
        while time.monotonic() < deadline:
            data = read_available(port, deadline, output.write).decode('utf-8', errors='replace')
            output.write(data); output.flush(); response += data
            try:
                parse_mode(response)
                if matched_at is None: matched_at = time.monotonic()
            except RuntimeError:
                if matched_at is not None: raise
            if matched_at is not None and time.monotonic() - matched_at >= 0.25: return response
        raise RuntimeError('Developer Mode response timed out or was ambiguous')
    session = DeveloperSession(exchange_mode)
    try:
        if args.developer: session.open()
        started = time.monotonic()
        next_command = started + 1
        commands = iter(args.command)
        while time.monotonic() - started < args.duration:
            if time.monotonic() >= next_command:
                command = next(commands, None)
                if command is not None:
                    port.write((command + '\r\n').encode())
                    output.write('\nCOMMAND: ' + command + '\n')
                next_command = time.monotonic() + 2
            data = read_available(port, started + args.duration, output.write)
            if data:
                output.write(decoder.decode(data))
                output.flush()
        output.write(decoder.decode(b"", final=True))
    finally:
        if args.developer: session.close()
