"""Validate captured full-reset/reboot evidence; never send or retry commands."""
import argparse
import json
import re
from pathlib import Path


def validate(reset_log, after_boot_log, *, cpu_profile, tx_profile, tx_dbm):
    def clean(value):
        return re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", value)
    reset_log, after_boot_log = clean(reset_log), clean(after_boot_log)
    commands = list(re.finditer(r"^COMMAND: app reset_factory\s*$", reset_log, re.M))
    if len(commands) != 1:
        raise ValueError("Exactly one submitted full-reset command is required")
    timeline = reset_log[commands[0].end():]
    boots = list(re.finditer(r"ESP-ROM:", timeline + after_boot_log))
    if len(boots) != 1:
        raise ValueError("Exactly one new ROM boot is required; missing boot or reboot loop")
    before, boot = timeline.split("ESP-ROM:", 1)
    phases = list(re.finditer(r"\[FACTORY_RESET\] id=(\d+) phase=(\d+) error=(-?\d+)", before))
    # The worker publishes transitions after Advance; successful preflight first logs phase 2.
    values = [int(item[2]) for item in phases]
    if values not in (list(range(2, 8)), list(range(2, 9))) or not phases or len({item[1] for item in phases}) != 1:
        raise ValueError("Missing, duplicated, reordered or mixed reset phases")
    if any(int(item[3]) != 0 or int(item[2]) > 8 for item in phases):
        raise ValueError("Failed reset cannot authorize reboot")
    if sum(int(item[2]) == 8 for item in phases) > 1:
        raise ValueError("Duplicate restart request")
    leave = re.search(r"\[ZB-SIGNAL\] Signal:[^\n]*Leave[^\n]*Status: ESP_OK", before)
    if not leave or leave.end() >= next(item.start() for item in phases if int(item[2]) == 7):
        raise ValueError("Successful SDK LEAVE must precede verified factory state")
    if not re.search(r"rst:[^\n]*\(SW_CPU\)", boot):
        raise ValueError("New boot is not a software CPU reset")
    if "factory_new=Yes" not in boot or "Waiting for user pairing command" not in boot:
        raise ValueError("New boot must show SDK factory-new and wait for user pairing")
    if "User initiated Zigbee Network Steering" in boot + after_boot_log:
        raise ValueError("Commissioning must wait for the separate restoration step")
    if "All 7 persisted and runtime configuration blocks match defaults." not in after_boot_log:
        raise ValueError("Fresh post-boot owner defaults readback is missing")
    # Preserve validation of historical captures; current firmware uses v1 machine records.
    if "type=power_status" in after_boot_log or "type=power_selftest" in after_boot_log:
        modes = list(re.finditer(r"^RACOOL_CLI v=1 type=power_status mode=(-?\d+) locks=\d+ sleeps=\d+ wakeups=\d+ restarts=\d+ uptime_s=\d+\r*$", after_boot_log,re.M))
        mode = modes[-1] if modes else None
        selftest = bool(re.search(r"^RACOOL_CLI v=1 type=power_selftest result=passed error=0\r*$",after_boot_log,re.M))
    else:
        mode = re.search(r"Current Mode[^\n]*\((-?\d+)\)", after_boot_log)
        selftest = "PASSED: PowerManager self-test OK" in after_boot_log
    if not mode or int(mode[1]) != cpu_profile or not selftest:
        raise ValueError("Actual post-boot CPU profile/self-test is missing or mismatched")
    profiles = {"Eco":0,"Normal":1,"Boost":2}
    tx_record = f"RACOOL_CLI v=1 type=zigbee_tx profile={profiles.get(tx_profile,-1)} dbm={tx_dbm}"
    # Historical captures predate the versioned TX protocol.
    tx_matches = tx_record in after_boot_log if 'type=zigbee_tx' in after_boot_log else f"Zigbee TX profile: {tx_profile}; SDK power: {tx_dbm} dBm" in after_boot_log
    if not tx_matches:
        raise ValueError("Actual post-boot SDK TX power is missing or mismatched")
    return {"operation_id": int(phases[0][1]), "phases": [int(item[2]) for item in phases],
            "software_boots": 1, "sdk_leave_before_verified_state": True,
            "post_boot_defaults": True, "cpu_profile": cpu_profile,
            "tx_profile": tx_profile, "tx_dbm": tx_dbm, "completed": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reset-log", required=True)
    parser.add_argument("--after-boot-log", required=True)
    parser.add_argument("--cpu-profile", type=int, required=True)
    parser.add_argument("--tx-profile", required=True)
    parser.add_argument("--tx-dbm", type=int, required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    result = validate(Path(args.reset_log).read_text(encoding="utf-8"),
                      Path(args.after_boot_log).read_text(encoding="utf-8"),
                      cpu_profile=args.cpu_profile, tx_profile=args.tx_profile, tx_dbm=args.tx_dbm)
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Factory reset/software boot evidence accepted")


if __name__ == "__main__":
    main()
