"""Verify all six report controls through the real serial CLI and restore each value."""
import argparse
import json
import re
import time
import subprocess
import os
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners
from Communication.Zigbee.diagnostic.reporting_cli_evidence import report_values

NAMES = ("fan", "ambient", "cpu", "health", "memory", "uptime")
PROPERTIES = dict(zip(NAMES, (
    "report_interval_radiator_s_climate", "report_interval_ambient_s_ambient_sensor",
    "report_interval_processor_s_processor_sensor", "report_interval_health_s_system_health",
    "report_interval_memory_s_memory", "report_interval_uptime_s_uptime")))

def validate_mqtt_readback(document, expected):
    if not document.get("completed") or document.get("errors"):
        raise RuntimeError("Incomplete or failed MQTT readback")
    reads = document.get("initialReads", [])
    if len(reads) != len(expected):
        raise RuntimeError("Incomplete MQTT control selection")
    for name, value in expected.items():
        selected = [item for item in reads if item.get("property") == PROPERTIES[name]]
        if len(selected) != 1 or selected[0].get("value") != value:
            raise RuntimeError("CLI/MQTT interval mismatch: " + name)
        evidence = selected[0].get("evidence", {})
        if evidence.get("kind") != "mqtt_state_after_get" or evidence.get("actual") != value:
            raise RuntimeError("Missing fresh MQTT interval evidence: " + name)
    return reads

def has_fresh_console(boot):
    marker = boot.rfind("ESP-ROM:")
    return marker >= 0 and "racool>" in boot[marker:]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--restart-after-disable", action="store_true")
    parser.add_argument("--mqtt-readback", action="store_true")
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {"started": time.time(), "checks": [], "completed": False}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        def command(text, expected):
            port.reset_input_buffer()
            log.write("\nCOMMAND: " + text + "\n")
            port.write((text + "\r\n").encode())
            received = ""
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                part = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
                received += part
                log.write(part)
                log.flush()
                if expected(received):
                    return received
            raise RuntimeError("No matching CLI result: " + text)

        values = report_values

        def apply(name, value):
            command(f"ZB REPORT {name} {value} --machine",
                    lambda output: f"RACOOL_CLI v=1 type=report_saved report={name} seconds={value}" in output)
            result = command(f"ZB REPORT {name} --machine",
                             lambda output: values(output).get(name) == value)
            evidence["checks"].append({"name": name, "value": value,
                                       "actual": values(result)[name], "timestamp": time.time()})

        def restore(name, original):
            current=values(command(f"ZB REPORT {name} --machine",lambda output:name in values(output)))[name]
            if current not in (original,0,1):
                raise RuntimeError('Refusing to overwrite foreign report interval: '+name)
            if current != original:apply(name,original)

        def mqtt_readback(expected):
            if not args.mqtt_readback:
                return
            result = subprocess.run(["node", "test/Communication/Zigbee/diagnostic/live_zigbee.mjs", "read",
                                     *(PROPERTIES[name] for name in expected)],
                                    capture_output=True, text=True, encoding="utf-8", timeout=100,
                                    env={**os.environ, "RACOOL_READ_EXPECTED": json.dumps(
                                        {PROPERTIES[name]: value for name, value in expected.items()})})
            log.write("\nMQTT READBACK:\n" + result.stdout + result.stderr)
            log.flush()
            if result.returncode:
                raise RuntimeError("MQTT readback helper failed")
            document = json.loads(Path("test/build/zigbee-live-read.json").read_text(encoding="utf-8"))
            reads = validate_mqtt_readback(document, expected)
            evidence.setdefault("mqtt_readbacks", []).append({"timestamp": time.time(), "reads": reads})

        session=DeveloperSession(lambda text:command(text,has_mode_response))
        try:
            session.open()
            response = command("ZB REPORT --machine", lambda output: len(values(output)) == len(NAMES))
            original = values(response)
            evidence["original"] = original
            prefix.with_suffix(".json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
            if args.restart_after_disable:
                restore_errors = []
                try:
                    for name in NAMES:
                        apply(name, 0)
                    menu=command('menu status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$',text)))
                    system=command('sys status --machine',lambda text:bool(re.search(r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$',text)))
                    require_idle_owners(menu,system)
                    command("power restart 500 --machine", lambda output: "RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0" in output)
                    boot = ""
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        part = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
                        boot += part
                        log.write(part)
                        log.flush()
                        if has_fresh_console(boot) and 'SYSTEM INITIALIZATION COMPLETE' in boot[boot.rfind('ESP-ROM:'):]:
                            break
                    else:
                        raise RuntimeError("No fresh ROM boot and console prompt after restart")
                    session.after_boot()
                    # Retry only this read-only query while services settle.
                    for attempt in range(4):
                        try:
                            restored = command("ZB REPORT --machine", lambda output: len(values(output)) == len(NAMES))
                            break
                        except RuntimeError:
                            if attempt == 3:
                                raise
                    evidence["after_restart"] = values(restored)
                    if any(value != 0 for value in evidence["after_restart"].values()):
                        raise RuntimeError("Disabled report interval did not survive restart")
                    mqtt_readback(dict.fromkeys(NAMES, 0))
                    for name in NAMES:
                        apply(name, 1)
                    mqtt_readback(dict.fromkeys(NAMES, 1))
                finally:
                    for name in NAMES:
                        try:
                            restore(name, original[name])
                        except Exception as error:
                            restore_errors.append(str(error))
                    evidence["restore_errors"] = restore_errors
                    if restore_errors:
                        raise RuntimeError("Restore failed: " + "; ".join(restore_errors))
                    mqtt_readback(original)
                evidence["completed"] = True
                print("CLI reporting restart persistence passed and restored:", len(NAMES))
                return
            for name in NAMES:
                try:
                    apply(name, 0)
                    mqtt_readback({name: 0})
                    apply(name, 1)
                    mqtt_readback({name: 1})
                finally:
                    restore(name, original[name])
                    mqtt_readback({name: original[name]})
            evidence["completed"] = True
        finally:
            try:session.close()
            except Exception as error:
                evidence['completed']=False
                evidence['developer_mode_restore_error']=str(error)
                raise
            finally:
                prefix.with_suffix(".json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print("CLI reporting controls passed and restored:", len(NAMES))

if __name__ == "__main__":
    main()
