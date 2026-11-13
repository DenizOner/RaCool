"""Verify SDK-configured Zigbee TX profiles and reboot persistence; restore the original."""
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
from Communication.DebugConsole.diagnostic.verify_developer_mode import require_idle_owners
from Controllers.MenuManager.diagnostic.menu_cli_evidence import menu_state
from Core.SystemInitializer.diagnostic.startup_diagnostics_evidence import validate

POWERS = {"Eco": 0, "Normal": 10, "Boost": 20}


def parse_profile(text):
    start = text.rfind("RACOOL_CLI v=1 type=zigbee_tx ")
    match = re.match(r"RACOOL_CLI v=1 type=zigbee_tx profile=([0-2]) dbm=(-?\d+)\r*\n", text[start:]) if start >= 0 else None
    if not match:
        raise ValueError("Missing or incomplete SDK TX status")
    profile = tuple(POWERS)[int(match[1])]
    if POWERS[profile] != int(match[2]):
        raise ValueError("Inconsistent SDK TX status")
    return {"profile": profile, "dbm": int(match[2])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--menu-state", type=int, help="Built UI_STATE_SET_ZIGBEE_TX_PROFILE value")
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {"completed": False, "started": time.time(), "checks": []}
    original = None
    menu_owned = False
    restoring = False
    owned_profiles = set()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        port.reset_input_buffer()
        sequence = 0

        def receive(deadline):
            chunk = read_available(port, deadline, log.write).decode("utf-8", errors="replace")
            log.write(chunk)
            log.flush()
            return chunk

        def command(value, marker, timeout=8):
            nonlocal sequence
            sequence += 1
            log.write(f"\nCOMMAND[{sequence}] t={time.time()}: {value}\n")
            log.flush()
            port.write((value + "\r\n").encode())
            text = ""
            deadline = time.monotonic() + timeout
            matched = None
            while time.monotonic() < deadline:
                text += receive(deadline)
                if re.search(r"Guru Meditation|Task watchdog got triggered|ESP-ROM:", text):
                    raise RuntimeError("Runtime fault or unexpected restart: " + value)
                executed = text.find("Executing: " + value)
                if executed >= 0 and re.search(marker, text[executed:]) and matched is None:
                    matched = time.monotonic()
                if matched is not None and time.monotonic() - matched >= 0.3:
                    errors=re.findall(r'(?m)^E \([^\n]*',text)
                    if errors:
                        evidence.setdefault('asynchronous_errors',[]).extend(errors)
                        if not restoring:raise RuntimeError('Runtime error observed after checked command: '+value)
                    return text
            raise RuntimeError("Incomplete response: " + value)

        def read_profile():
            return parse_profile(command("ZB TX --machine", r"RACOOL_CLI v=1 type=zigbee_tx profile=[0-2] dbm=-?\d+\r*\n"))

        def read_profile_ready():
            for attempt in range(4):
                try:return read_profile()
                except RuntimeError as error:
                    if attempt == 3 or not str(error).startswith('Incomplete response:'):
                        raise
                    time.sleep(0.25)

        def apply(profile):
            owned_profiles.add(profile)
            actual = parse_profile(command("ZB TX " + profile + " --machine", r"RACOOL_CLI v=1 type=zigbee_tx profile=[0-2] dbm=-?\d+\r*\n"))
            if actual["profile"] != profile or read_profile() != actual:
                raise RuntimeError("TX command/readback mismatch")
            evidence["checks"].append({**actual, "timestamp": time.time()})

        session = DeveloperSession(lambda text: command(text, r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            original = read_profile()
            owned_profiles.add(original['profile'])
            evidence["original"] = original
            prefix.with_suffix(".json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
            for profile in POWERS:
                apply(profile)
            for invalid in ("ZB TX 20", "ZB TX Eco extra"):
                command(invalid + " --machine", r"RACOOL_CLI v=1 type=zigbee_command result=rejected")
                if read_profile()["profile"] != "Boost":
                    raise RuntimeError("Rejected input changed power")
            if args.menu_state is not None:
                state_text = command("menu status --machine", r"RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+")
                if menu_state(state_text)!=0:
                    raise RuntimeError("Refusing to interrupt an active menu session")
                status=command('UI STATUS --machine',r'RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle')
                evidence['original_screen']=re.findall(r'RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle',status)[-1]
                prefix.with_suffix('.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
                command('UI WAKE --machine',r'RACOOL_CLI v=1 type=ui_command command=WAKE result=queued error=0')
                time.sleep(0.6)
                menu_owned = True
                command(f"menu nav {args.menu_state} --machine", r"RACOOL_CLI v=1 type=menu_navigation result=queued state=")
                time.sleep(0.5)
                state_text = command("menu status --machine", r"RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+")
                if menu_state(state_text)!=args.menu_state:
                    raise RuntimeError("TX menu navigation did not apply")
                command("menu click --machine", r"RACOOL_CLI v=1 type=menu_input result=accepted input=button")
                command("menu enc 1 --machine", r"RACOOL_CLI v=1 type=menu_input result=accepted input=encoder")
                if read_profile()["profile"] != "Eco":
                    raise RuntimeError("TX menu increment did not wrap Boost to Eco")
                command("menu enc -1 --machine", r"RACOOL_CLI v=1 type=menu_input result=accepted input=encoder")
                if read_profile()["profile"] != "Boost":
                    raise RuntimeError("TX menu decrement did not restore Boost")
                command("menu reset --machine", r"RACOOL_CLI v=1 type=menu_navigation result=queued state=0")
                menu_owned = False
                evidence["menu_encoder_sdk_roundtrip"] = True
            menu=command('menu status --machine',r'(?m)^RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+\r*$')
            system=command('sys status --machine',r'(?m)^RACOOL_CLI v=1 type=system_status state=-?\d+\r*$')
            require_idle_owners(menu,system)
            command("power restart 500 --machine", r"RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0", timeout=4)
            boot = ""
            deadline = time.monotonic() + 35
            while time.monotonic() < deadline:
                boot += receive(deadline)
                if "Self-test finished. Health Score:" in boot:
                    evidence["boot"] = validate(boot)
                    break
            else:
                raise RuntimeError("Missing fresh startup/health evidence")
            if "SW_CPU" not in boot:
                raise RuntimeError("Missing software reset reason")
            session.after_boot()
            evidence["restart"] = read_profile_ready()
            if evidence["restart"]["profile"] != "Boost":
                raise RuntimeError("TX profile did not persist")
            evidence["checks_passed"] = True
        except Exception as exc:
            evidence["error"] = str(exc)
        finally:
            restoring=True
            try:
                if menu_owned:
                    command("menu reset --machine", r"RACOOL_CLI v=1 type=menu_navigation result=queued state=0")
                if original is not None:
                    # Read-only retries admit a reconnecting console. Never replay
                    # a mutable command whose original delivery is uncertain.
                    for attempt in range(4):
                        try:
                            current = read_profile()
                            break
                        except Exception:
                            if attempt == 3:
                                raise
                    if current['profile'] not in owned_profiles:
                        raise RuntimeError('Refusing to overwrite a foreign TX profile')
                    if current != original:
                        apply(original["profile"])
                    evidence["restored"] = read_profile() == original
                    if evidence.get('original_screen')=='suspended':
                        deadline=time.monotonic()+150
                        while True:
                            try:
                                status=command('UI STATUS --machine',r'RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle')
                            except RuntimeError:
                                if time.monotonic()>deadline: raise
                                time.sleep(1)
                                continue
                            if 'type=ui screen=suspended service=2 boot=idle' in status: break
                            if time.monotonic()>deadline: raise RuntimeError('Screen sleep restoration failed')
                            time.sleep(1)
                evidence["completed"] = bool(evidence.get("checks_passed") and evidence.get("restored"))
            except Exception as exc:
                evidence["restore_error"] = str(exc)
            try:
                session.close()
            except Exception as exc:
                evidence["completed"] = False
                evidence["developer_mode_restore_error"] = str(exc)
            prefix.with_suffix(".json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    if not evidence["completed"]:
        raise RuntimeError("TX profile acceptance or restoration failed; inspect evidence")


if __name__ == "__main__":
    main()
