"""Verify CLI playback/override behavior, restoring the initial stable selection."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, has_mode_response
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners
from Hardware.BuiltinLED.diagnostic.builtin_cli_evidence import parse_builtin_status, parse_builtin_command


def parse_status(text):
    state = parse_builtin_status(text)
    if state is None: raise ValueError('Missing complete worker status')
    return dict(animation=state['animation'], priority=state['priority'],
                remaining=state['remaining_ms'], brightness=state['brightness'],
                on=bool(state['on']), enabled=bool(state['enabled']))



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    result = {"completed": False, "checks": [], "physical_output_verified": False}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    initial = None
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        def command(text, marker, timeout=8):
            parts = text.split()
            machine_name = None
            if parts[0].startswith('builtin_led_') and parts[0] != 'builtin_led_info':
                machine_name = parts[0].removeprefix('builtin_led_')
            elif parts[0] == 'bled' and len(parts) > 1:
                machine_name = parts[1]
            if machine_name:
                text += ' --machine'
            hardware_led = parts[:2] == ['HW','LED']
            if hardware_led:
                text += ' --machine'
            port.reset_input_buffer()
            log.write("\nCOMMAND: " + text + "\n")
            port.write((text + "\r\n").encode())
            received = ""
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
                log.write(part)
                log.flush()
                received += part
                if (parse_builtin_command(received, machine_name) == 'accepted') if machine_name else (has_mode_response(received) if parts[0] == 'DEVELOPER' else (bool(re.search(r'RACOOL_CLI v=1 type=hardware_command command=LED result=accepted\r*\n',received)) if hardware_led else (marker(received) if callable(marker) else marker in received))):
                    return received
            raise RuntimeError("Missing response: " + text)

        def status():
            return parse_status(command("builtin_led_info --machine", "type=builtin_status"))

        def check(name, observed, accepted):
            result["checks"].append({"name": name, "worker": observed, "passed": bool(accepted)})

        session = DeveloperSession(lambda text: command(text, 'type=developer_mode'))
        try:
            session.open()
            snapshot = status()
            if snapshot["priority"] != 0 or snapshot["remaining"] not in (0, 0xffffffff):
                raise RuntimeError("Refusing to interrupt an active timed/high-priority notification")
            initial = snapshot
            result["initial"] = initial
            if not initial["enabled"]:
                command("builtin_led_on", "Automatic mode saved")
                time.sleep(0.5)
            automatic = status()

            def automatic_status():
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    current = status()
                    if current["enabled"] and current["priority"] == 0 and current["animation"] == automatic["animation"]:
                        return current
                    time.sleep(0.2)
                raise RuntimeError("Automatic selection did not settle after enabling")

            command("builtin_led_set 7 2 2000", "queued")
            time.sleep(0.2)
            state = status()
            check("timed_high_priority_override", state,
                  state["animation"] == 7 and state["priority"] == 2 and 0 < state["remaining"] <= 2000)
            time.sleep(2.3)
            state = status()
            check("default_restored_after_expiry", state,
                  state["animation"] == automatic["animation"] and state["priority"] == 0)
            command("builtin_led_off", "Disabled mode saved")
            time.sleep(1)
            state = status()
            check("off_remains_off", state, not state["enabled"] and not state["on"] and state["animation"] == -1)
            command("builtin_led_set 7 2 1000", "queued")
            time.sleep(0.2)
            state = status()
            check("disabled_rejects_notification", state, not state["enabled"] and not state["on"])
            menu=command('menu status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$',text)))
            system=command('sys status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$',text)))
            require_idle_owners(menu,system)
            command("power restart 500 --machine", "RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0")
            boot = ""
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
                boot += part
                log.write(part)
                log.flush()
                marker = boot.rfind("ESP-ROM:")
                if marker >= 0 and "racool>" in boot[marker:] and "SYSTEM INITIALIZATION COMPLETE" in boot[marker:]:
                    break
            else:
                raise RuntimeError("Missing fresh ROM boot")
            result["fresh_boot"] = True
            session.after_boot()
            for attempt in range(4):
                try:
                    state = status()
                    break
                except (RuntimeError, ValueError):
                    if attempt == 3:
                        raise
            check("disabled_persists_after_restart", state,
                  not state["enabled"] and not state["on"] and state["animation"] == -1)
            command("builtin_led_on", "Automatic mode saved")
            time.sleep(1)
            state = status()
            state = automatic_status()
            check("on_restores_automatic_selection", state,
                  state["enabled"] and state["animation"] == automatic["animation"])
            for off_command, off_marker, on_command, on_marker in (
                ("bled off", "Disabled mode save -> 0", "bled on", "Automatic mode save -> 0"),
                ("HW LED OFF", "modu kaydedildi", "HW LED ON", "modu kaydedildi")):
                command(off_command, off_marker)
                time.sleep(0.5)
                state = status()
                check(off_command, state, not state["enabled"] and not state["on"])
                command(on_command, on_marker)
                time.sleep(0.5)
                state = status()
                state = automatic_status()
                check(on_command, state, state["enabled"] and state["animation"] == automatic["animation"])
        finally:
            try:
                if initial is not None:
                    command("builtin_led_off", "Disabled mode saved")
                    if initial["enabled"]:
                        command("builtin_led_on", "Automatic mode saved")
                        command(f"builtin_led_set {initial['animation']} 0 0", "queued")
                    time.sleep(0.5)
                    restored = status()
                    result["restored"] = restored
                    result["restoration_verified"] = all(restored[k] == initial[k]
                        for k in ("animation", "priority", "brightness", "on", "enabled"))
            finally:
                try:
                    session.close()
                except Exception as error:
                    result['developer_mode_restore_error'] = str(error)
                finally:
                    result["completed"] = 'developer_mode_restore_error' not in result and bool(result.get("restoration_verified")) and len(result["checks"]) == 10 and all(
                        c["passed"] for c in result["checks"])
                    prefix.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
