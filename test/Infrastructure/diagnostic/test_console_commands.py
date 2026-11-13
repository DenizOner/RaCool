"""
RaCool Console Command Integration Test  v2
- Waits for system boot (racool> prompt) before sending any command
- Uses correct command names from each *ConsoleCommands.c source file
"""
import io
import serial
import sys
import time

# Force UTF-8 output so ESP32 ANSI/Unicode bytes don't crash on Windows CP1254
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")


PORT      = "COM5"
BAUD      = 115200
BOOT_TIMEOUT  = 30.0   # max seconds to wait for the racool> prompt after reset
CMD_TIMEOUT   = 4.0    # seconds to collect a command response
CMD_DELAY     = 0.5    # pause between commands

PROMPT_MARKERS = [b"racool>", b"esp32>", b"cmd>", b"> "]

GREEN  = "\033[92m"; RED    = "\033[91m"; YELLOW = "\033[93m"
CYAN   = "\033[96m"; RESET  = "\033[0m";  BOLD   = "\033[1m"
def ok(m):   print(f"  {GREEN}PASS  {m}{RESET}")
def fail(m): print(f"  {RED}FAIL  {m}{RESET}")
def info(m): print(f"  {CYAN}>>    {m}{RESET}")

# ---------------------------------------------------------------------------
# (name, command_string, expected_fragment_or_None, optional_timeout_seconds)
# ---------------------------------------------------------------------------
COMMANDS = [
    # ── SecurityManager (command = "sec", sub-commands as argv[1]) ─────────
    ("sec status",         "sec status",              "SecurityManager"),
    ("sec set-mode 0",     "sec set-mode 0",          "mode"),
    ("sec auth 0000",      "sec auth 0000",           "Auth"),
    ("sec reset-lockout",  "sec reset-lockout",       "Lockout"),

    # â”€â”€ DiagnosticsManager (command = "diag") â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("diag status",        "diag status",             "DiagnosticsManager"),
    ("diag selftest",      "diag selftest",           "self-test"),

    # â”€â”€ ApplicationManager (command = "app") â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("app status",         "app status",              "ApplicationManager"),

    # â”€â”€ SystemController (command = "sys") â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("sys status",         "sys status",              None),

    # â”€â”€ MenuManager (command = "menu") â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("menu status",        "menu status",             None),

    # â”€â”€ SerialMonitor (command = "SER_PERF") â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("SER_PERF",           "SER_PERF",                None),

    # â”€â”€ ErrorHandler â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("error_status",       "error_status",            "ErrorHandler"),
    ("error_history",      "error_history",           "Error History"),
    ("error_trigger",      "error_trigger TEST 42",   "queued"),
    ("error_trigger_isr",  "error_trigger_isr ISR 9", "ISR error"),
    ("error_selftest",     "error_selftest",          "Self test"),
    ("error_clear",        "error_clear",             "cleared"),

    # â”€â”€ EventBus â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("bus_status",         "bus_status",              "EventBus"),
    ("bus_publish",        "bus_publish test.event",  "published"),

    # â”€â”€ LogManager â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("log_status",         "log_status",              "LogManager"),
    ("log_level_set",      "log_level_set 3",         "log level"),
    ("log_module_set",     "log_module_set WiFi 2",   "Log level"),
    ("log_recent",         "log_recent 5",            "Log Entries"),

    # â”€â”€ PerformanceMonitor â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("perf_status",        "perf_status",             "Free Heap"),
    ("perf_start",         "perf_start",              "started"),
    ("perf_stop",          "perf_stop",               "stopped"),

    # â”€â”€ ServiceLocator â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ("svc_list",           "svc_list",                "Registered Services"),
    ("svc_get",            "svc_get DebugConsole",    "state="),

    # -- SystemInitializer --------------------------------------------------------
    ("sys_init_status",    "sys_init_status",         "SystemInitializer"),

    # -- Hardware -----------------------------------------------------------------
    ("rgb status",         "rgb status",              "RGBLED"),
    ("rgb set",            "rgb set 0 0 255",         "SetColorRGB"),
    ("rgb off",            "rgb off",                 "TurnOff"),
    ("pin status",         "pin status",              "Validation"),
    ("pin dump",           "pin dump",                None),
    ("oled status",        "oled status",             "OLED"),
    ("oled contrast",      "oled contrast 200",       "Contrast"),
    ("i2c status",         "i2c status",              "I2CManager"),
    ("i2c scan",           "i2c scan",                "Scan"),
    ("bled status",        "bled status",             "BuiltinLED"),
    ("bled on",            "bled on",                 "TurnOn"),
    ("bled off",           "bled off",                "TurnOff"),
    ("int_temp status",    "int_temp status",         "InternalTemperatureSensor"),
    ("int_temp read",      "int_temp read",           "C"),
    ("int_temp stats",     "int_temp stats",          "Thermal Stats"),
    ("int_temp selftest",  "int_temp selftest",       "SelfTest"),
    ("ir_temp status",     "ir_temp status",          "InfraredTemperatureSensor"),
    ("ir_temp trigger",    "ir_temp trigger",         "triggered"),

    # -- Restore LED States ----------------------------------------------------
    ("bled on restore",    "bled on",                 "TurnOn"),
    ("rgb set restore",    "rgb set 0 255 0",         "SetColorRGB"),

    # -- Destructive / reboot-risk (run LAST) ------------------------------------
    # sec self-test blocks the console task for ~3s and may trigger a WDT reboot.
    # Keeping it last ensures no other test is disrupted if the device resets.
    ("sec self-test",      "sec self-test",           "Self-test",    15.0),
]

# ---------------------------------------------------------------------------
def has_prompt(data: bytes) -> bool:
    for m in PROMPT_MARKERS:
        if m in data:
            return True
    return False

def wait_for_boot(ser, timeout=BOOT_TIMEOUT) -> bool:
    """Drain serial until we see the shell prompt, meaning boot is done."""
    print(f"\n{CYAN}[BOOT] Waiting for shell prompt (max {timeout}s)...{RESET}")
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            # Print boot lines as they arrive so the user can see progress
            lines = buf.split(b"\n")
            buf = lines[-1]          # keep incomplete last line
            for line in lines[:-1]:
                txt = line.decode(errors="replace").strip()
                if txt:
                    print(f"  {YELLOW}{txt}{RESET}")
            if has_prompt(b"\n".join(lines)):
                print(f"{GREEN}[BOOT] Prompt detected â€” system ready.{RESET}\n")
                return True
        else:
            time.sleep(0.05)
    print(f"{RED}[BOOT] Timeout â€” proceeding anyway.{RESET}\n")
    return False

def send_cmd(ser, cmd, timeout=CMD_TIMEOUT) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    deadline = time.time() + timeout
    response = b""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            response += chunk
            if has_prompt(response):
                break
        else:
            time.sleep(0.05)
    return response.decode(errors="replace")

# ---------------------------------------------------------------------------
def main():
    print(f"\n{BOLD}{'='*65}")
    print("  RaCool Console Command Integration Test  v2")
    print(f"  Port:{PORT}  Baud:{BAUD}")
    print(f"{'='*65}{RESET}")

    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except serial.SerialException as e:
        print(f"{RED}Cannot open {PORT}: {e}{RESET}"); sys.exit(1)

    # Wait for the device to finish booting
    wait_for_boot(ser)

    # Send a blank enter to refresh the prompt
    ser.write(b"\r\n"); time.sleep(0.3); ser.reset_input_buffer()

    passed = []; failed = []

    for name, cmd, *rest in COMMANDS:
        fragment   = rest[0] if len(rest) > 0 else None
        cmd_timeout = rest[1] if len(rest) > 1 else CMD_TIMEOUT
        print(f"\n{BOLD}[{name}]{RESET}")
        info(cmd)
        try:
            resp = send_cmd(ser, cmd, timeout=cmd_timeout)
            for line in [l.strip() for l in resp.splitlines() if l.strip()]:
                print(f"    {YELLOW}{line}{RESET}")

            if fragment is None:
                ok("no assertion"); passed.append(name)
            elif fragment.lower() in resp.lower():
                ok(f"found: '{fragment}'"); passed.append(name)
            else:
                fail(f"missing: '{fragment}'"); failed.append(name)
        except Exception as ex:
            fail(str(ex)); failed.append(name)
        time.sleep(CMD_DELAY)

    ser.close()

    total = len(passed) + len(failed)
    print(f"\n{BOLD}{'='*65}")
    print(f"  {GREEN}{len(passed)} PASS{RESET}  {RED}{len(failed)} FAIL{RESET}  / {total} total")
    print(f"{'='*65}{RESET}")
    if failed:
        print(f"\n{RED}Failed commands:{RESET}")
        for f in failed:
            print(f"  â€¢ {f}")
    if not failed:
        print(f"\n{GREEN}{BOLD}All commands passed! âœ“{RESET}")
    sys.exit(0 if not failed else 1)

main()


