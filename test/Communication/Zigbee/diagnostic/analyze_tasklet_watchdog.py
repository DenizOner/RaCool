"""Analyze one ESP32-C6 watchdog register snapshot against its exact ELF."""

import argparse
import json
import re
import subprocess
from pathlib import Path


def analyze(log, symbols, disassembly):
    if "task_wdt:  - IDLE (CPU 0)" not in log or "task_wdt: CPU 0: Zigbee_main" not in log:
        raise ValueError("Missing Zigbee_main / IDLE watchdog evidence")
    dumps = log.split("Core  0 register dump:")
    if len(dumps) != 2:
        raise ValueError("Expected exactly one register dump")
    registers = dict(re.findall(r"\b(MEPC|RA|S1|A0)\s*:\s*(0x[0-9a-fA-F]+)", dumps[1]))
    if len(registers) != 4:
        raise ValueError("Incomplete watchdog register dump")
    registers = {name: int(value, 16) for name, value in registers.items()}
    ranges = {}
    for line in symbols.splitlines():
        match = re.fullmatch(r"\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+(tasklet_sched_process|s_nwk_fwd)\s*", line)
        if match:
            name = match.group(3)
            if name in ranges:
                raise ValueError("Ambiguous ELF symbol: " + name)
            ranges[name] = (int(match.group(1), 16), int(match.group(2), 16))
    if set(ranges) != {"tasklet_sched_process", "s_nwk_fwd"}:
        raise ValueError("Required sized ELF symbols not found")
    start, size = ranges["tasklet_sched_process"]
    if not (start <= registers["MEPC"] < start + size and start <= registers["RA"] < start + size):
        raise ValueError("Snapshot does not resolve inside tasklet scheduler")
    instructions = {}
    for line in disassembly.splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+):\s+[0-9a-fA-F]+\s+(.+?)\s*$", line)
        if match:
            instructions[int(match.group(1), 16)] = re.sub(r"\s+", "", match.group(2))
    pc = registers["MEPC"]
    # Recognize only the observed layout; SDK changes must not create a false inference.
    expected = {0: "andia5,a5,2", -24: "lws1,0(a0)", -22: "addia4,a0,4",
                -18: "fencerw,rw", -14: "lwa5,0(a4)", -12: "fencer,rw",
                -8: "andia5,a5,1", -4: "amoand.w.aqrla5,s0,(a4)"}
    if any(instructions.get(pc + offset) != instruction for offset, instruction in expected.items()) or not instructions.get(pc - 6, "").startswith("bneza5,"):
        raise ValueError("Unrecognized scheduler instruction sequence")
    pointer = registers["A0"]
    base, length = ranges["s_nwk_fwd"]
    return {
        "mepc": hex(pc), "ra": hex(registers["RA"]),
        "current": hex(pointer), "next": hex(registers["S1"]),
        "self_link_consistent": pointer != 0 and pointer == registers["S1"],
        "current_in_nwk_fwd": base <= pointer < base + length,
        "nwk_fwd_offset": hex(pointer - base) if base <= pointer < base + length else None,
        "limitation": "One register snapshot and matching instructions do not identify how the link was formed.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("log", "elf", "nm", "objdump"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    def run(*argv):
        return subprocess.run([str(x) for x in argv], check=True, capture_output=True, text=True).stdout
    result = analyze(args.log.read_text(encoding="utf-8", errors="replace"),
                     run(args.nm, "-S", "-n", args.elf), run(args.objdump, "-d", args.elf))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
