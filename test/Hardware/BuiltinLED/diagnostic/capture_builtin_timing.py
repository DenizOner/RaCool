"""Capture existing built-in animation timing without changing its selection."""
import argparse
import codecs
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response



def phase_metrics(rows, phase_offset=None):
    """Infer a constant initial phase; never infer physical edges from these rows."""
    if not rows or len({r["gen"] for r in rows}) != 1 or any(r["cmd"] >= 0 for r in rows):
        return {"available": False, "reason": "Selection changes or commands require segmented analysis"}
    def on(phase):
        return phase < 80 or 200 <= phase < 280
    offsets = range(2000) if phase_offset is None else [phase_offset % 2000]
    candidates = [offset for offset in offsets
                  if all(on((r["advance"] + offset) % 2000) == any(r["rgb"]) for r in rows)]
    if not candidates:
        return {"available": False, "reason": "Output does not match a constant double-blink phase"}
    offset = candidates[len(candidates)//2]
    missed = observed = expected = 0
    lateness = []
    output_lateness = []
    for a, b in zip(rows, rows[1:]):
        start, end = a["advance"] + offset, b["advance"] + offset
        boundaries = [cycle * 2000 + phase
                      for cycle in range(start//2000, end//2000 + 1)
                      for phase in (0, 80, 200, 280)
                      if start < cycle*2000 + phase <= end]
        changed = any(a["rgb"]) != any(b["rgb"])
        expected += len(boundaries)
        observed += int(changed)
        missed += len(boundaries) - int(changed)
        if changed and boundaries:
            late = end - boundaries[-1]
            lateness.append(late)
            output_lateness.append(late + (b.get("output", b["t"]) - b["t"])/1000)
    elapsed = (rows[-1]["t"]-rows[0]["t"])/1000
    advanced = rows[-1]["advance"]-rows[0]["advance"]
    return {"available": True, "phase_offset_candidates": candidates,
            "phase_source": "inferred" if phase_offset is None else "provided_reset_anchor",
            "complete_engine_cycles": advanced//2000,
            "expected_transitions": expected, "observed_transitions": observed,
            "skipped_transitions": missed, "unexpected_transitions": 0,
            "phase_lateness_max_ms": max(lateness, default=0),
            "phase_lateness_mean_ms": sum(lateness)/len(lateness) if lateness else None,
            "output_lateness_max_ms": max(output_lateness, default=0),
            "output_lateness_mean_ms": sum(output_lateness)/len(output_lateness) if output_lateness else None,
            "clock_lag_ms": elapsed-advanced,
            "note": "Integer-millisecond engine phase; phase uncertainty is listed in candidates. Lateness excludes skipped edges. Software output only."}


def analyze(text):
    summaries = re.findall(r"LEDTRACE summary ([^\r\n]+)", text)
    if len(summaries) != 1:
        raise ValueError("Expected exactly one capture summary")
    summary = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", summaries[0])}
    rows = []
    for match in re.finditer(r"LEDTRACE row ([^\r\n]+)", text):
        line = match.group(1)
        row = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)(?![,\d])", line)}
        rgb = re.search(r"rgb=(\d+),(\d+),(\d+)", line)
        if not rgb:
            raise ValueError("Incomplete RGB row")
        row["rgb"] = tuple(map(int, rgb.groups()))
        rows.append(row)
    if len(rows) != summary["rows"] or summary["dropped"]:
        raise ValueError("Incomplete or overflowed trace")
    if any(b["t"] <= a["t"] for a, b in zip(rows, rows[1:])):
        raise ValueError("Nonmonotonic trace")
    result = {"summary": summary, "rows": rows, "physical_output_verified": False}
    if rows and all(r["anim"] == 14 for r in rows):
        edges = []
        for row in rows:
            on = any(row["rgb"])
            if not edges or on != edges[-1]["on"]:
                edges.append({"on": on, "t": row["output"], "gen": row["gen"]})
        starts = [edge["t"] for i, edge in enumerate(edges) if i and edge["on"] and
                  edge["t"] - edges[i-1]["t"] > 1000000]
        periods = [(b-a)/1000 for a, b in zip(starts, starts[1:])]
        result["double_blink"] = {"complete_cycles": len(periods),
            "period_min_ms": min(periods) if periods else None,
            "period_max_ms": max(periods) if periods else None,
            "period_mean_ms": sum(periods)/len(periods) if periods else None,
            "generation_values": sorted({r["gen"] for r in rows}),
            "transitions": len(edges),
            "note": "Periods are software output timestamps; missing pulses require separate phase analysis."}
    if "double_blink" in result:
        result["double_blink"]["phase_analysis"] = phase_metrics(rows)
    return result


def parse_trace_status(text):
    matches = re.findall(r"LEDTRACE status enabled=(\d+) capacity=(\d+) rows=(\d+) dropped=(\d+)", text)
    if not matches:
        raise RuntimeError("Missing trace ownership status")
    enabled, capacity, rows, dropped = map(int, matches[-1])
    if enabled not in (0, 1) or capacity < 1 or rows > capacity:
        raise RuntimeError("Invalid trace ownership status")
    return {"enabled": bool(enabled), "capacity": capacity, "rows": rows, "dropped": dropped}


class TraceAlreadyActive(RuntimeError):
    pass


def capture_session(command, wait, on_started=None):
    result = {"completed": False}
    attempted = False
    try:
        result["initial_status"] = command("builtin_led_info --machine", "type=builtin_status")
        initial = parse_trace_status(command("builtin_led_trace status", "LEDTRACE status"))
        result["initial_trace"] = initial
        if initial["enabled"]:
            raise RuntimeError("Refusing to replace an existing trace capture")
        attempted = True
        command("builtin_led_trace start", "LEDTRACE start done")
        if on_started:
            on_started()
        wait()
        trace = command("builtin_led_trace dump", "LEDTRACE dump done", 60)
        result.update(analyze(trace))
        result["final_status"] = command("builtin_led_info --machine", "type=builtin_status")
        result["checks_passed"] = True
    except TraceAlreadyActive as error:
        attempted = False
        result["foreign_trace_preserved"] = True
        result["error"] = str(error)
    except Exception as error:
        result["error"] = str(error)
    finally:
        # One serial owner and an initially inactive trace establish ownership.
        # After uncertain delivery, read state instead of replaying start/dump.
        if attempted:
            try:
                current = parse_trace_status(command("builtin_led_trace status", "LEDTRACE status"))
                if current["enabled"]:
                    command("builtin_led_trace stop", "LEDTRACE stop done")
                    current = parse_trace_status(command("builtin_led_trace status", "LEDTRACE status"))
                result["trace_disabled"] = not current["enabled"]
            except Exception as error:
                result["cleanup_error"] = str(error)
        else:
            result["foreign_trace_preserved"] = result.get("foreign_trace_preserved", False) or bool(result.get("initial_trace", {}).get("enabled"))
        result["completed"] = result.get("checks_passed", False) and result.get("trace_disabled", False)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--duration", type=float, default=230)
    parser.add_argument("--ready-marker", action="store_true", help="Notify an orchestrator after capture is acknowledged")
    args = parser.parse_args()
    prefix = Path(args.output)
    result = {"completed": False}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        def receive():
            chunk = decoder.decode(port.read(port.in_waiting or 1))
            log.write(chunk)
            log.flush()
            return chunk
        def command(command, marker, timeout=20):
            log.write("\nCOMMAND: " + command + "\n")
            port.write((command + "\r\n").encode())
            text = ""
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                text += receive()
                if "LEDTRACE already active" in text:
                    raise TraceAlreadyActive("Trace ownership changed before capture start")
                if "LEDTRACE allocation failed" in text:
                    raise RuntimeError("Trace buffer allocation failed; capture did not start")
                if (has_mode_response(text) if command.startswith('DEVELOPER ') else marker in text):
                    return text
            raise RuntimeError("Missing CLI response: " + command)
        def wait_capture():
            deadline = time.monotonic() + args.duration
            while time.monotonic() < deadline:
                receive()
        notify = (lambda: print("LEDTRACE_READY", flush=True)) if args.ready_marker else None
        session = DeveloperSession(lambda text: command(text, 'type=developer_mode'))
        try:
            session.open()
            result = capture_session(command, wait_capture, notify)
        finally:
            session.close()
        prefix.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k not in ("rows", "initial_status", "final_status")}))

    return 0 if result["completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
