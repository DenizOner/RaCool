"""Check manual fan ON does not masquerade as CPU protection; restore RAM mode."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession
from verify_climate_stale import parse_status


from climate_cli_evidence import parse_override


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {"completed": False, "started": time.time(), "checks": []}
    changed = False
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        port.reset_input_buffer()
        sequence = 0

        def command(value, marker):
            nonlocal sequence
            sequence += 1
            log.write(f"\nCOMMAND[{sequence}] t={time.time()}: {value}\n")
            log.flush()
            port.write((value + "\r\n").encode())
            deadline = time.monotonic() + 8
            text = ""
            matched = None
            while time.monotonic() < deadline:
                chunk = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
                text += chunk
                log.write(chunk)
                log.flush()
                clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)
                if re.search(r"(?m)^E \(|ESP-ROM:|Guru Meditation|Task watchdog got triggered", clean):
                    raise RuntimeError("Unexpected runtime fault/restart")
                executed = clean.find("Executing: " + value)
                if executed >= 0 and re.search(marker, clean[executed:]) and matched is None:
                    matched = time.monotonic()
                if matched is not None and time.monotonic() - matched >= 0.3:
                    return clean[executed:]
            raise RuntimeError("Missing response: " + value)

        def status():
            return parse_status(command("CLIMATE STATUS --machine", r"RACOOL_CLI v=1 type=climate_status[^\r\n]*[\r\n]"))

        def cpu():
            active = parse_override(command("CLIMATE CPU --machine", r"RACOOL_CLI v=1 type=climate_cpu[^\r\n]*[\r\n]"))
            evidence["checks"].append({"active": active, "sequence": sequence, "timestamp": time.time()})
            return active

        def mode(value):
            command("CLIMATE MODE " + value + " --machine", r"RACOOL_CLI v=1 type=climate_command command=MODE result=completed error=0[\r\n]")

        session = DeveloperSession(lambda text: command(text, r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            original = status()
            evidence["original"] = original
            if original["mode"] != "OBJECT_SENSITIVE" or original["on"] or original["stale"] or original["cpu"] >= 40 or cpu():
                raise RuntimeError("Refusing to interrupt an active/non-idle safety state")
            prefix.with_suffix(".json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
            changed = True
            mode("ON")
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                manual = status()
                if manual["mode"] == "MANUAL ON" and manual["on"]:
                    break
            else:
                raise RuntimeError("Manual fan ON not applied")
            evidence["manual_on"] = manual
            if cpu():
                raise RuntimeError("Manual fan ON incorrectly reported as CPU override")
            evidence["completed"] = True
        except Exception as error:
            evidence["error"] = str(error)
            raise
        finally:
            try:
                if changed:
                    mode("OBJECT_SENSITIVE")
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        restored = status()
                        if restored["mode"] == "OBJECT_SENSITIVE" and restored["on"] == original["on"]:
                            break
                    else:
                        raise RuntimeError("Initial OBJECT_SENSITIVE relay state not restored")
                    if cpu():
                        raise RuntimeError("Unexpected CPU protection after restoration")
                    evidence["restored"] = restored
            except Exception as error:
                evidence["completed"] = False
                evidence["restore_error"] = str(error)
                raise
            finally:
                try:
                    session.close()
                except Exception as error:
                    evidence["completed"] = False
                    evidence["developer_mode_restore_error"] = str(error)
                    raise
                finally:
                    prefix.with_suffix(".json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print(json.dumps({"completed": evidence["completed"], "restored": bool(evidence.get("restored"))}))


if __name__ == "__main__":
    main()
