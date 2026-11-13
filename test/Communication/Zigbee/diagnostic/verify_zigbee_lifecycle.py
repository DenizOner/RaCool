"""Verify real Zigbee owner restart without resetting network credentials."""
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


def reject_runtime_errors(text, allow_logged_errors=False, allow_report_backpressure=False,
                          allow_restart_selftest=False):
    clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)
    if re.search(r"assert failed|Guru Meditation|ESP-ROM:", clean):
        raise ValueError("Runtime error or unexpected reboot during SDK lifecycle")
    # Reject an incomplete error line as well: a command must not be sent while
    # an asynchronous fault is only partially received.
    errors = re.findall(r"E \(\d+\) [^\r\n]*", clean)
    def report_backpressure(line):
        return bool(re.search(r'^E \(\d+\) Zigbee: Attribute report request rejected: .*\bsdk_error=1(?:\s|$)', line) or
                    re.search(r'^E \(\d+\) Zigbee: Queued attribute failed: .*\bstatus=137\b.*\bretry=1(?:\s|$)', line))
    def known_observation(line):
        return ((allow_report_backpressure and report_backpressure(line)) or
                (allow_restart_selftest and re.search(
                    r'^E \(\d+\) DiagnosticsManager: \[SelfTestTask\] Service FAILED: Zigbee result=-1\b', line)))
    if errors and not allow_logged_errors and not all(map(known_observation, errors)):
        raise ValueError("Runtime error or unexpected reboot during SDK lifecycle")
    return errors


def lifecycle_acknowledged(text):
    stops = re.findall(r"RACOOL_CLI v=1 type=zigbee_lifecycle operation=stop result=(-?\d+) state=(\d+)", text)
    starts = re.findall(r"RACOOL_CLI v=1 type=zigbee_lifecycle operation=start result=(-?\d+) state=(\d+)", text)
    return stops == [('0', '3')] and starts == [('0', '2')]


def lifecycle_result(text):
    reject_runtime_errors(text)
    if not lifecycle_acknowledged(text):
        raise ValueError("Missing, failed or duplicated owner Stop/Start result")
    return {"stop_result": 0, "stop_state": 3, "start_result": 0, "start_state": 2}


def report_values(text):
    from Communication.Zigbee.diagnostic.reporting_cli_evidence import report_values as read_values
    return read_values(text, require_all=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--observe-report-backpressure', action='store_true',
                        help='Continue collecting lifecycle evidence for retryable SDK NoMem reports; keep the run failed')
    parser.add_argument('--observe-restart-selftest', action='store_true',
                        help='Continue after Zigbee self-test failure during restart; keep the run failed')
    args = parser.parse_args()
    prefix = Path(args.output)
    result = {"completed": False, "cycles": [], "started": time.time()}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        # Preserve all available input, including data already buffered at open.
        restoring = False
        last_command_output = ''
        def command(value, marker, timeout=12, on_submit=None):
            nonlocal last_command_output
            text = ''
            last_command_output = ''
            drain_deadline = time.monotonic() + timeout
            while port.in_waiting and time.monotonic() < drain_deadline:
                chunk = read_available(port, drain_deadline, log.write).decode('utf-8', errors='replace')
                text += chunk; log.write(chunk); log.flush()
            if port.in_waiting:
                raise RuntimeError('Serial input did not quiesce before command: ' + value)
            reject_runtime_errors(text, allow_logged_errors=restoring,
                                  allow_report_backpressure=args.observe_report_backpressure)
            command_start = len(text)
            log.write('\nCOMMAND: ' + value + '\n'); log.flush()
            if on_submit is not None:
                on_submit()
            port.write((value + '\r\n').encode())
            deadline = time.monotonic() + timeout
            matched_at = None
            while time.monotonic() < deadline:
                chunk = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                text += chunk; log.write(chunk); log.flush()
                last_command_output = text
                # Include between-command logs; do not discard late SDK faults.
                errors = reject_runtime_errors(text, allow_logged_errors=restoring or on_submit is not None,
                                               allow_report_backpressure=args.observe_report_backpressure)
                if re.search(marker, text[command_start:]) and matched_at is None:
                    matched_at = time.monotonic()
                if matched_at is not None and time.monotonic() - matched_at >= 0.5:
                    if errors and restoring:
                        result.setdefault('restoration_runtime_errors', []).extend(errors)
                        result['completed'] = False
                    elif errors and on_submit is None and args.observe_report_backpressure:
                        result.setdefault('observed_report_backpressure', []).extend(errors)
                    return text
            raise RuntimeError('Incomplete response: ' + value)

        def connected():
            for _ in range(10):
                data = command('ZB DIAG --machine', r'RACOOL_CLI v=1 type=zigbee_status state=\d+ connected=[01] endpoints=\d+', timeout=5)
                if re.search(r'RACOOL_CLI v=1 type=zigbee_status state=2 connected=1 endpoints=\d+', data): return True
                time.sleep(0.5)
            raise RuntimeError('Zigbee rejoin not observed')

        def idle_owners():
            menu = command('menu status --machine', r'RACOOL_CLI v=1 type=menu_status state=\d+ registry_label_hex=[0-9a-f]+')
            system = command('sys status --machine', r'RACOOL_CLI v=1 type=system_status state=-?\d+')
            require_idle_owners(menu, system)

        original = None
        restart_pending = False
        def mark_restart_submitted():
            nonlocal restart_pending
            # A partial write is ambiguous, so record responsibility first.
            restart_pending = True
        session = DeveloperSession(lambda text: command(text, r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            idle_owners()
            connected()
            original = report_values(command('ZB REPORT --machine', r'RACOOL_CLI v=1 type=report_interval report=uptime seconds=\d+ enabled=[01]'))
            result['original_reports'] = original
            for index in range(3):
                idle_owners()
                data = command('ZB RESTART --machine',
                    r'RACOOL_CLI v=1 type=zigbee_lifecycle operation=start result=-?\d+ state=\d+|RACOOL_CLI v=1 type=zigbee_lifecycle operation=stop result=-?[1-9]\d* state=\d+',
                    timeout=35, on_submit=mark_restart_submitted)
                if lifecycle_acknowledged(data):
                    restart_pending = False
                if args.observe_report_backpressure or args.observe_restart_selftest:
                    errors = reject_runtime_errors(data, allow_report_backpressure=args.observe_report_backpressure,
                                                   allow_restart_selftest=args.observe_restart_selftest)
                    if errors: result.setdefault('observed_restart_errors', []).extend(errors)
                    if not lifecycle_acknowledged(data):
                        raise ValueError('Missing, failed or duplicated owner Stop/Start result')
                    evidence = {'stop_result': 0, 'stop_state': 3, 'start_result': 0, 'start_state': 2}
                else:
                    evidence = lifecycle_result(data)
                connected()
                current = report_values(command('ZB REPORT --machine', r'RACOOL_CLI v=1 type=report_interval report=uptime seconds=\d+ enabled=[01]'))
                if current != original: raise RuntimeError('Restart changed reporting settings')
                evidence['cycle'] = index + 1
                evidence['connected_after_restart'] = True
                result['cycles'].append(evidence)
            result['lifecycle_completed'] = True
            result['completed'] = not (result.get('observed_report_backpressure') or result.get('observed_restart_errors'))
        except Exception as exc:
            result['error'] = str(exc)
        finally:
            restoring = True
            # No persistent setting is written. A failed partial SDK cleanup needs
            # one explicit chip restart, never another uncertain radio command.
            if not result['completed'] and restart_pending:
                try:
                    boot = last_command_output
                    # A panic may already be rebooting the chip. Observe that
                    # boot before considering a separate recovery reset.
                    if 'Guru Meditation' in boot and 'ESP-ROM:' not in boot:
                        natural_deadline = time.monotonic() + 10
                        while time.monotonic() < natural_deadline and 'ESP-ROM:' not in boot:
                            chunk = read_available(port, natural_deadline, log.write).decode('utf-8', errors='replace')
                            boot += chunk; log.write(chunk); log.flush()
                    if 'ESP-ROM:' in boot:
                        boot = boot[boot.rfind('ESP-ROM:'):]
                        result['recovery_boot'] = 'observed'
                    else:
                        boot = ''
                        log.write('\nRECOVERY: native USB Serial/JTAG hardware reset\n'); log.flush()
                        port.rts = True; port.dtr = port.dtr
                        time.sleep(0.2)
                        port.rts = False; port.dtr = port.dtr
                        result['recovery_boot'] = 'requested'
                    deadline = time.monotonic() + 40
                    while time.monotonic() < deadline:
                        if ('ESP-ROM:' in boot and 'SYSTEM INITIALIZATION COMPLETE' in boot and
                                'app_main: System ready.' in boot and 'racool>' in boot): break
                        chunk = read_available(port, deadline, log.write).decode('utf-8', errors='replace')
                        boot += chunk; log.write(chunk); log.flush()
                    if not all(marker in boot for marker in ('ESP-ROM:', 'SYSTEM INITIALIZATION COMPLETE',
                                                              'app_main: System ready.', 'racool>')):
                        raise RuntimeError('Recovery lacks fresh ROM boot/startup evidence')
                    if 'Guru Meditation' in boot or 'assert failed' in boot:
                        raise RuntimeError('Recovery boot contains a fatal error')
                    boot_errors = re.findall(r'E \(\d+\) [^\r\n]*', re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', boot))
                    if boot_errors:
                        result.setdefault('restoration_runtime_errors', []).extend(boot_errors)
                        result['completed'] = False
                    session.after_boot()
                    connected()
                    current = report_values(command('ZB REPORT --machine', r'RACOOL_CLI v=1 type=report_interval report=uptime seconds=\d+ enabled=[01]'))
                    result['recovery_verified'] = original is None or current == original
                except Exception as exc:
                    result['recovery_error'] = str(exc)
            try:
                session.close()
            except Exception as exc:
                result['completed'] = False
                result['developer_mode_restore_error'] = str(exc)
            prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
