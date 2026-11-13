"""Verify saved encoder controls through fresh USB-console boots and restore them."""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners


def controls(text):
    records=[line for line in re.findall(r'[^\n]*(?:\n|$)',text) if line.startswith('RACOOL_CLI ') and 'type=encoder_status' in line]
    pattern=r'RACOOL_CLI v=1 type=encoder_status state=([0-4]) steps=(\d+) inverted=([01]) acceleration=([01]) threshold_ms=(\d+) rotation_debounce_ms=\d+ button_debounce_ms=\d+ position=-?\d+ rotations=\d+ clicks=\d+ dropped_rotations=\d+ dropped_buttons=\d+ queue_overflows=\d+\r*\n'
    match=re.fullmatch(pattern,records[-1] if records else '')
    if not match:return None
    count,threshold=int(match[2]),int(match[5])
    if not 1<=count<=255 or not 0<=threshold<=255:return None
    return {'steps':count,'inverted':match[3]=='1','acceleration':match[4]=='1','threshold':threshold}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    result = {"completed": False, "restored": False, "physical_encoder_verified": False}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    original = None
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        def receive(deadline):
            text = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
            log.write(text); log.flush()
            return text

        def command(text, accept, timeout=8):
            port.reset_input_buffer()
            port.write((text + "\r\n").encode())
            log.write("\nCOMMAND: " + text + "\n")
            captured = ""
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                captured += receive(deadline)
                if accept(captured):
                    return captured
            raise RuntimeError("Missing fresh result: " + text)

        def read_controls():
            return controls(command("ENC --machine", lambda t: controls(t) is not None))

        def restart():
            menu = command('menu status --machine', lambda t: bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$', t)))
            system = command('sys status --machine', lambda t: bool(re.search(r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$', t)))
            require_idle_owners(menu, system)
            command("power restart 500 --machine", lambda t: "RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0" in t)
            captured = ""
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                captured += receive(deadline)
                marker = captured.rfind("ESP-ROM:")
                if marker >= 0 and "racool>" in captured[marker:] and "SYSTEM INITIALIZATION COMPLETE" in captured[marker:]:
                    break
            else:
                raise RuntimeError("Missing fresh ROM boot")
            session.after_boot()
            # Early console readiness is not command registration readiness.
            for _ in range(8):
                try:
                    return read_controls()
                except RuntimeError:
                    time.sleep(0.25)
            raise RuntimeError("Encoder command did not become ready after boot")

        def apply(value):
            def checked(text,name):
                response=command(text+' --machine',lambda t:'type=encoder_command' in t and t.endswith('\n'))
                if not re.search(r'^RACOOL_CLI v=1 type=encoder_command command='+name+r' result=completed error=0\r*$',response,re.M):raise RuntimeError('Encoder command rejected: '+name)
            checked(f"ENCCONFIG DETENTS {value['steps']}",'ENCCONFIG')
            direction = "ON" if value["inverted"] else "OFF"
            checked(f"ENCCONFIG INVERT {direction}",'ENCCONFIG')
            acceleration = "ON" if value["acceleration"] else "OFF"
            checked(f"ENCACCEL {acceleration} {value['threshold']}",'ENCACCEL')
            if read_controls() != value:
                raise RuntimeError("Runtime controls differ from requested values")
            checked('ENCSAVE','ENCSAVE')

        session = DeveloperSession(lambda text: command(text, has_mode_response))
        try:
            session.open()
            # Establish the persisted baseline before changing any setting.
            original = restart()
            result["original"] = original
            prefix.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
            selected = {"steps": 2 if original["steps"] != 2 else 4,
                        "inverted": not original["inverted"],
                        "acceleration": not original["acceleration"],
                        "threshold": 50 if original["threshold"] != 50 else 100}
            apply(selected)
            actual = restart()
            result["test_values"] = selected
            result["after_restart"] = actual
            if actual != selected:
                raise RuntimeError("Saved encoder controls did not survive restart")
            result["completed"] = True
        except Exception as error:
            result["error"] = str(error)
        finally:
            if original is not None:
                try:
                    apply(original)
                    result["restored_values"] = restart()
                    result["restored"] = result["restored_values"] == original
                except Exception as error:
                    result["restoration_error"] = str(error)
            try:
                session.close()
            except Exception as error:
                result["completed"] = False
                result["developer_mode_restore_error"] = str(error)
    prefix.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if not result["completed"] or not result["restored"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
