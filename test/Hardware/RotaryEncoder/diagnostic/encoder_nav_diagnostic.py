#!/usr/bin/env python3
"""
encoder_nav_diagnostic.py
=========================
Autonomous encoder navigation diagnostic for RaCool ESP32-C6.

Connects to the device via serial, monitors live log output, and after each
detected physical encoder rotation, reports diagnostic hypotheses. Display
appearance and physical motion still require user observation.

  SCENARIO A - VISUAL BUG:
    UIController receives 1 ENCODER_MOVE event, MenuManager transitions
    correctly (MENU_ROOT -> CAT_TEMP), but the OLED display appears to show
    the WRONG item.  Fix: LVGL scroll / layout.

  SCENARIO B - NAVIGATION BUG:
    UIController receives 2+ ENCODER_MOVE events per physical rotation.
    The ISR bounce guard is not catching the bounce.  Fix: increase
    RE_PCNT_ISR_DEBOUNCE_US or diagnose PCNT configuration.

  SCENARIO C - COOLDOWN BLOCKED:
    UIController receives 1 event, but MenuManager blocks it with the button
    cooldown.  Fix: reduce RE_BUTTON_ROTATION_COOLDOWN_MS.

Usage:
    python encoder_nav_diagnostic.py [COM_PORT] [BAUD]
    Default: auto-detect COM port, 115200 baud.

Requirements:
    pip install pyserial
"""

import serial
import serial.tools.list_ports
import sys
import time
import re
import threading
import queue
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Optional, List, Tuple
from contextlib import ExitStack
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession
from Controllers.MenuManager.diagnostic.menu_cli_evidence import menu_state

# ---- Configuration ----------------------------------------------------------

BAUD_RATE          = 115200
MONITOR_DURATION_S = 60          # How long to listen for physical rotations
CLUSTER_WINDOW_S   = 0.5         # Events within this window = one physical rotation
ROTATION_COUNT     = 5           # How many physical rotations to capture
CMD_WAIT_S         = 1.2         # Seconds to wait for command response

# Log line patterns (must match firmware [DIAG-*] and [NAV] tags exactly)
RE_DIAG_UI  = re.compile(r'\[DIAG-UI\] ENCODER_MOVE #(\d+) diff=([+-]?\d+) t=(\d+)ms')
RE_DIAG_MM  = re.compile(r'\[DIAG-MM\] HandleEncoder #(\d+) dir=([+-]?\d+) t=(\d+)ms')
RE_NAV_PASS = re.compile(r'\[NAV\] ENC dir=([+-]?\d+) \| state=(\d+)\((\w+)\)')
RE_NAV_BLCK = re.compile(r'\[NAV\] ENC dir=([+-]?\d+) BLOCKED \(btn cooldown (\d+)ms')
RE_NAV_DDUP = re.compile(r'\[NAV\] ENC dir=([+-]?\d+) DEDUP')
RE_NAV_RSLT = re.compile(r'\[NAV\] ENC result: (\d+)\((\w+)\) -> (\d+)\((\w+)\)')
RE_NAV_MNTR = re.compile(r'\[NAV\] ENC ignored on MONITOR')

# ---- Data Model -------------------------------------------------------------

@dataclass
class RotationCluster:
    """Events captured from a single physical encoder rotation."""
    index: int
    start_time_s: float

    diag_ui_events: List[Tuple] = field(default_factory=list)
    diag_mm_events: List[Tuple] = field(default_factory=list)
    nav_passed:     List[Tuple] = field(default_factory=list)
    nav_blocked:    List[Tuple] = field(default_factory=list)
    nav_deduped:    List[Tuple] = field(default_factory=list)
    nav_results:    List[Tuple] = field(default_factory=list)
    nav_monitor:    int         = 0

    def ui_event_count(self):
        return len(self.diag_ui_events)

    def mm_call_count(self):
        return len(self.diag_mm_events)

    def state_steps(self):
        return len(self.nav_results)

    def duration_ms(self):
        if len(self.diag_ui_events) < 2:
            return 0
        times_s = [e[0] for e in self.diag_ui_events]
        return int((max(times_s) - min(times_s)) * 1000)

    def diagnose(self):
        ui  = self.ui_event_count()
        blk = len(self.nav_blocked)
        ded = len(self.nav_deduped)
        nav = self.state_steps()
        mon = self.nav_monitor

        if ui == 0:
            return "NO_DATA: no [DIAG-UI] events seen — was screen off?"

        if ui > 1:
            dur = self.duration_ms()
            return (
                f"NAVIGATION_BUG: UIController received {ui} ENCODER_MOVE events "
                f"for 1 physical rotation (debounce not catching bounce). "
                f"Delta-t between events: {dur}ms. "
                f"Action: increase RE_PCNT_ISR_DEBOUNCE_US."
            )

        if blk > 0:
            return (
                f"COOLDOWN_BLOCKED: 1 UI event, HandleEncoder blocked by button "
                f"cooldown ({self.nav_blocked[0][1]}ms remaining). "
                f"Action: wait longer before rotating, or reduce RE_BUTTON_ROTATION_COOLDOWN_MS."
            )

        if ded > 0:
            return (
                f"DEDUP_BLOCKED: 1 UI event, HandleEncoder blocked by software "
                f"dedup. RE_ENCODER_DEDUP_WINDOW_MS may be too large."
            )

        if mon > 0:
            return (
                f"ENC_IGNORED: device was in MONITOR state — encoder is ignored "
                f"there. Click first to open the menu."
            )

        if nav == 1:
            result = self.nav_results[0]
            return (
                f"VISUAL_BUG: 1 UI event -> 1 navigation step "
                f"({result[1]}->{result[3]}). State machine CORRECT. "
                f"Display shows wrong item. Action: fix LVGL scroll/layout."
            )

        if nav == 0 and self.mm_call_count() > 0:
            return (
                f"UNKNOWN: 1 UI event, HandleEncoder called but no state "
                f"transition. Plugin may have consumed the event."
            )

        return f"OK: {ui} event(s), {nav} step(s)."


# ---- Serial Helpers ----------------------------------------------------------

def find_serial_port():
    ports = list(serial.tools.list_ports.comports())
    candidates = [p.device for p in ports if p.vid == 0x303A and p.pid == 0x1001]
    return candidates[0] if len(candidates) == 1 else None


def send_cmd(ser, cmd, wait_s=CMD_WAIT_S):
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait_s)
    out = ""
    while ser.in_waiting:
        out += ser.read(ser.in_waiting).decode(errors="replace")
        time.sleep(0.05)
    return out


def log_reader(ser, log_q, stop_evt):
    buf = ""
    while not stop_evt.is_set():
        try:
            chunk = ser.read(ser.in_waiting or 1).decode(errors="replace")
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                log_q.put((time.time(), line.strip()))
        except Exception:
            time.sleep(0.01)


# ---- Main -------------------------------------------------------------------

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else BAUD_RATE

    if port is None:
        port = find_serial_port()
        if port is None:
            print("ERROR: No unique native USB Serial/JTAG port; select a port explicitly.")
            print("Usage: python encoder_nav_diagnostic.py <port>")
            sys.exit(1)
        print(f"Auto-detected port: {port}")

    print(f"\n{'='*64}")
    print(f"  RaCool Encoder Navigation Diagnostic")
    print(f"  Port={port}  Baud={baud}")
    print(f"{'='*64}\n")

    ser = serial.Serial(port=None, baudrate=baud, timeout=0.1)
    ser.dtr = False
    ser.rts = False
    ser.port = port
    with ser, ExitStack() as cleanup:
        time.sleep(0.5)
        ser.reset_input_buffer()

        log_q = queue.Queue()
        stop_evt = threading.Event()
        t = threading.Thread(target=log_reader, args=(ser, log_q, stop_evt), daemon=True)
        session = DeveloperSession(lambda text: send_cmd(ser, text, wait_s=1))
        menu_attempted = False
        def close_session():
            stop_evt.set()
            if t.ident is not None:
                t.join(timeout=2)
                if t.is_alive(): raise RuntimeError('Serial reader did not stop; mode restoration is unverified')
            try:
                if menu_attempted:
                    current = menu_state(send_cmd(ser, 'menu status --machine', wait_s=1))
                    if current is None: raise RuntimeError('Menu restoration baseline is unavailable')
                    if current != 0:
                        response = send_cmd(ser, 'menu reset --machine', wait_s=1)
                        if 'type=menu_navigation result=queued state=0' not in response:
                            raise RuntimeError('Menu restoration was not admitted')
                        if menu_state(send_cmd(ser, 'menu status --machine', wait_s=1)) != 0:
                            raise RuntimeError('Menu restoration was not applied')
            finally:
                session.close()
        cleanup.callback(close_session)
        session.open()

        # Step 1: Initial state
        print("[1] Querying initial state...")
        resp = send_cmd(ser, "menu status --machine")
        if menu_state(resp) != 0:
            raise RuntimeError('Refusing an unavailable or foreign menu session')
        for l in resp.splitlines():
            if "State" in l or "state" in l:
                print(f"    {l.strip()}")

        # Step 2: Open menu
        print("\n[2] Sending 'menu click --machine' to open MENU_ROOT...")
        menu_attempted = True
        resp = send_cmd(ser, "menu click --machine", wait_s=1.5)
        if 'type=menu_input result=accepted input=button' not in resp:
            raise RuntimeError('Menu click was not admitted')
        for l in resp.splitlines():
            l = l.strip()
            if any(k in l for k in ["NAV", "state", "State"]):
                print(f"    {l}")

        t.start()
        # Drain stray events
        time.sleep(0.3)
        while not log_q.empty():
            try:
                log_q.get_nowait()
            except queue.Empty:
                break

        # Step 3: Listen for rotations
        print(f"\n[3] Listening for {ROTATION_COUNT} rotations ({MONITOR_DURATION_S}s window).")
        print(f"    >>> PHYSICALLY ROTATE THE ENCODER NOW <<<\n")

        clusters = []
        current = None
        last_event_s = 0.0
        deadline = time.time() + MONITOR_DURATION_S

        while time.time() < deadline and len(clusters) < ROTATION_COUNT:
            try:
                wall_s, line = log_q.get(timeout=0.1)
            except queue.Empty:
                # Close current cluster if quiet for CLUSTER_WINDOW_S
                if current and (time.time() - last_event_s) > CLUSTER_WINDOW_S:
                    clusters.append(current)
                    print(f"  -> Rotation #{current.index} done: "
                          f"{current.ui_event_count()} UI events, "
                          f"{current.state_steps()} state steps.")
                    current = None
                continue

            m_ui  = RE_DIAG_UI.search(line)
            m_mm  = RE_DIAG_MM.search(line)
            m_nav = RE_NAV_PASS.search(line)
            m_blk = RE_NAV_BLCK.search(line)
            m_ded = RE_NAV_DDUP.search(line)
            m_res = RE_NAV_RSLT.search(line)
            m_mon = RE_NAV_MNTR.search(line)

            if not any([m_ui, m_mm, m_nav, m_blk, m_ded, m_res, m_mon]):
                continue

            # New cluster starts on first DIAG-UI event
            if m_ui and current is None:
                current = RotationCluster(index=len(clusters)+1, start_time_s=wall_s)
                print(f"  [ROT #{current.index}] first event...")

            if current is None:
                continue

            last_event_s = wall_s

            if m_ui:
                ev = (wall_s, int(m_ui.group(1)), int(m_ui.group(2)), int(m_ui.group(3)))
                current.diag_ui_events.append(ev)
                print(f"    [DIAG-UI] #%s diff=%s t=%sms" % (m_ui.group(1), m_ui.group(2), m_ui.group(3)))

            if m_mm:
                ev = (wall_s, int(m_mm.group(1)), int(m_mm.group(2)), int(m_mm.group(3)))
                current.diag_mm_events.append(ev)
                print(f"    [DIAG-MM] HandleEncoder #%s dir=%s t=%sms" % (m_mm.group(1), m_mm.group(2), m_mm.group(3)))

            if m_nav:
                ev = (int(m_nav.group(1)), int(m_nav.group(2)), m_nav.group(3))
                current.nav_passed.append(ev)
                print(f"    [NAV] PASSED dir=%s state=%s(%s)" % (m_nav.group(1), m_nav.group(2), m_nav.group(3)))

            if m_blk:
                ev = (int(m_blk.group(1)), int(m_blk.group(2)))
                current.nav_blocked.append(ev)
                print(f"    [NAV] BLOCKED cooldown=%sms" % m_blk.group(2))

            if m_ded:
                ev = (int(m_ded.group(1)),)
                current.nav_deduped.append(ev)
                print(f"    [NAV] DEDUP dir=%s" % m_ded.group(1))

            if m_res:
                ev = (int(m_res.group(1)), m_res.group(2),
                      int(m_res.group(3)), m_res.group(4))
                current.nav_results.append(ev)
                print(f"    [NAV] RESULT %s(%s) -> %s(%s)" % (
                    m_res.group(2), m_res.group(1), m_res.group(4), m_res.group(3)))

            if m_mon:
                current.nav_monitor += 1
                print(f"    [NAV] IGNORED (MONITOR state)")

        if current:
            clusters.append(current)
            print(f"  -> Rotation #{current.index} done: "
                  f"{current.ui_event_count()} UI events, "
                  f"{current.state_steps()} state steps.")

        stop_evt.set()
        t.join(timeout=2)
        if t.is_alive(): raise RuntimeError('Serial reader did not stop')

        # Step 4: Final state
        print("\n[4] Final state query...")
        time.sleep(0.3)
        resp = send_cmd(ser, "menu status --machine")
        for l in resp.splitlines():
            if "State" in l or "state" in l:
                print(f"    {l.strip()}")

    # Step 5: Report
    print(f"\n{'='*64}")
    print(f"  DIAGNOSTIC HYPOTHESES  ({len(clusters)} rotation(s))")
    print(f"{'='*64}")

    if not clusters:
        print("\n  !! No rotations detected.")
        print("     Check: encoder connected? Firmware has [DIAG-UI] logs? Right port?")
        sys.exit(1)

    print(f"\n  {'ROT':>3} | {'UI':>4} | {'MM':>4} | {'STEPS':>5} | {'Dms':>5} | VERDICT")
    print(f"  {'-'*3}-+-{'-'*4}-+-{'-'*4}-+-{'-'*5}-+-{'-'*5}-+-{'-'*35}")
    for c in clusters:
        tag = c.diagnose().split(":")[0]
        print(f"  {c.index:>3} | {c.ui_event_count():>4} | {c.mm_call_count():>4} |"
              f" {c.state_steps():>5} | {c.duration_ms():>5} | {tag}")

    # Aggregate
    verdicts = defaultdict(int)
    for c in clusters:
        verdicts[c.diagnose().split(":")[0]] += 1

    primary = max(verdicts, key=verdicts.get)

    print(f"\n  PRIMARY: {primary} ({verdicts[primary]}/{len(clusters)} rotations)\n")

    if "NAVIGATION_BUG" in primary:
        durs = [c.duration_ms() for c in clusters if c.ui_event_count() > 1]
        avg  = int(sum(durs)/len(durs)) if durs else 0
        print(f"  CAUSE:  ISR bounce guard not effective.")
        print(f"  Avg Dt: {avg}ms between duplicate events.")
        if avg > 0:
            new_us = int(avg * 1000 * 1.5)
            print(f"  FIX:    Set RE_PCNT_ISR_DEBOUNCE_US >= {new_us} in RotaryEncoderConfig.h")
        else:
            print(f"  FIX:    Verify firmware was actually reflashed (DIAG-UI count=2 at ISR).")

    elif "VISUAL_BUG" in primary:
        print(f"  CAUSE:  LVGL scroll not applying correctly after navigation.")
        print(f"  FIX:    Ensure lv_timer_handler() is called AFTER lv_obj_scroll_to_y().")
        print(f"          Or: use lv_obj_scroll_to_view(selectedBtn, LV_ANIM_OFF) instead.")

    elif "COOLDOWN_BLOCKED" in primary:
        print(f"  CAUSE:  RE_BUTTON_ROTATION_COOLDOWN_MS too large.")
        print(f"  FIX:    Reduce RE_BUTTON_ROTATION_COOLDOWN_MS in RotaryEncoderConfig.h")

    print()
    for c in clusters:
        print(f"  Rotation #{c.index}: {c.diagnose()}")

    print(f"\n{'='*64}\n")


if __name__ == "__main__":
    main()
