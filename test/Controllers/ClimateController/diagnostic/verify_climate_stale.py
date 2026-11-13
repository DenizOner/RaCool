"""Observe the approved stale-IR policy through production workers; restore settings."""
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
from Hardware.InfraredTemperatureSensor.diagnostic.ir_cli_evidence import parse_status as parse_ir_status, require_poll
from fan_sources_evidence import parse_sources, require_empty_slots


from climate_cli_evidence import parse_status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {"completed": False, "started": time.time(), "observations": []}
    original = interval = None
    changed = False
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        port.reset_input_buffer()
        sequence = 0

        def command(value, marker, timeout=8):
            nonlocal sequence
            sequence += 1
            log.write(f"\nCOMMAND[{sequence}] t={time.time()}: {value}\n")
            log.flush()
            port.write((value + "\r\n").encode())
            deadline = time.monotonic() + timeout
            text = ""
            matched = None
            while time.monotonic() < deadline:
                chunk = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
                text += chunk
                log.write(chunk)
                log.flush()
                if re.search(r"(?m)^E \(|ESP-ROM:|Guru Meditation|Task watchdog got triggered", text):
                    raise RuntimeError("Unexpected runtime fault/restart: " + value)
                clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)
                executed = clean.find("Executing: " + value)
                if executed >= 0 and re.search(marker, clean[executed:]) and matched is None:
                    matched = time.monotonic()
                if matched is not None and time.monotonic() - matched >= 0.2:
                    return clean[executed:]
            raise RuntimeError("Missing response: " + value)

        def status():
            state = parse_status(command("CLIMATE STATUS --machine", r"RACOOL_CLI v=1 type=climate_status[^\r\n]*[\r\n]"))
            evidence["observations"].append({**state, "timestamp": time.time(), "sequence": sequence})
            return state

        def wait_state(predicate, timeout):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                current = status()
                if predicate(current):
                    return current
                time.sleep(0.4)
            raise RuntimeError("Expected climate worker state not reached")

        def mode(value):
            command("CLIMATE MODE " + value + " --machine", r"RACOOL_CLI v=1 type=climate_command command=MODE result=completed error=0[\r\n]")

        def poll(value):
            require_poll(command(f"ir_temp poll {value} --machine",r"RACOOL_CLI v=1 type=ir_sensor_command[^\r\n]*[\r\n]"),value)

        session = DeveloperSession(lambda text: command(text, r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            original = status()
            interval = parse_ir_status(command("ir_temp status --machine",r"RACOOL_CLI v=1 type=ir_sensor_status[^\r\n]*[\r\n]"))["polling_ms"]
            info = command("CLIMATE INFO --machine", r"RACOOL_CLI v=1 type=climate_info[^\r\n]*[\r\n]")
            stale_timeout = int(re.search(r"^RACOOL_CLI v=1 type=climate_info stale_timeout_ms=(\d+) cpu_protection=[01]\r*$",info,re.M)[1])
            if not 0 < stale_timeout < 30000:
                raise RuntimeError("Stale timeout unsuitable for bounded sampling-gap test")
            if original["mode"] != "OBJECT_SENSITIVE" or original["on"] or original["stale"] or original["enabled"] != "ENABLED":
                raise RuntimeError("Refusing to interrupt non-idle object-sensitive climate state")
            if original["radiator"] >= original["close"] - 1 or original["cpu"] >= 40:
                raise RuntimeError("Insufficient thermal margin for cold-sample test")
            sources=parse_sources(command("CLIMATE SOURCES",r"EXT3 state=.*[\r\n]"))
            require_empty_slots(sources)
            if sources["sources"]["object"]["configured"]!=0 or sources["sources"]["object"]["effective"]!=0 or not sources["sources"]["object"]["valid"]:
                raise RuntimeError("Refusing sampling-gap test with an external object source")
            evidence.update(original=original, original_sources=sources, original_poll_ms=interval, stale_timeout_ms=stale_timeout)
            prefix.with_suffix(".json").write_text(json.dumps(evidence,indent=2),encoding="utf-8")
            changed = True
            mode("OFF")
            wait_state(lambda x: x["mode"] == "MANUAL OFF" and not x["on"], 12)
            command("CLIMATE SOURCE OBJECT 1 --machine",r"RACOOL_CLI v=1 type=climate_command command=SOURCE result=completed error=0[\r\n]")
            poll(60000)
            stale = wait_state(lambda x: x["stale"], stale_timeout / 1000 + 15)
            mode("OBJECT_SENSITIVE")
            wait_state(lambda x: x["mode"] == "OBJECT_SENSITIVE" and x["stale"] and x["on"],12)
            invalid=parse_sources(command("CLIMATE SOURCES",r"EXT3 state=.*[\r\n]"))
            require_empty_slots(invalid)
            if invalid["sources"]["object"]["configured"]!=1 or invalid["sources"]["object"]["valid"] or invalid["sources"]["object"]["status"]!=2:
                raise RuntimeError("Selected external and internal fallback are not both invalid")
            evidence["invalid_selected_and_fallback"]=invalid
            for _ in range(4):
                held = status()
                if held["mode"] != "OBJECT_SENSITIVE" or not held["on"] or not held["stale"] or held["debouncing"] or held["tick"] != stale["tick"]:
                    raise RuntimeError("Fail-safe changed output, debounce or acquisition timestamp")
                time.sleep(0.5)
            evidence["stale_fail_safe_on_confirmed"] = True
            poll(interval)
            fresh = wait_state(lambda x: not x["stale"] and x["tick"] != stale["tick"] and not x["on"] and not x["debouncing"], 20)
            evidence["fresh_recovery"] = fresh
            evidence["completed"] = True
        except Exception as error:
            evidence["error"] = str(error)
            raise
        finally:
            try:
                if changed:
                    poll(interval)
                    command("CLIMATE SOURCE OBJECT 0 --machine",r"RACOOL_CLI v=1 type=climate_command command=SOURCE result=completed error=0[\r\n]")
                    mode("OBJECT_SENSITIVE")
                    restored = wait_state(lambda x: x["mode"] == "OBJECT_SENSITIVE" and not x["stale"] and x["on"] == original["on"], 20)
                    poll_value=parse_ir_status(command("ir_temp status --machine",r"RACOOL_CLI v=1 type=ir_sensor_status[^\r\n]*[\r\n]"))["polling_ms"]
                    if poll_value != interval:
                        raise RuntimeError("Original sampling interval not restored")
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
    print(json.dumps({"completed": evidence["completed"], "stale_fail_safe_on_confirmed": evidence.get("stale_fail_safe_on_confirmed"), "restored": bool(evidence.get("restored"))}))


if __name__ == "__main__":
    main()
