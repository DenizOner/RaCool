"""Verify telemetry menu persistence readback and restore the original interval."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Controllers.UIController.diagnostic.ui_readiness import wait_for_ui_ready
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, has_mode_response


from Controllers.MenuManager.diagnostic.menu_cli_evidence import menu_state
from Hardware.OLED.diagnostic.frame_evidence import parse_frame, write_png


def interval(text):
    records = re.findall(r"RACOOL_CLI v=1 type=config_field key=system\.telemetry_interval_ms value=([^\r\n ]+)", text)
    matches = records if records and records[-1].isdigit() else []
    return int(matches[-1]) if matches and (int(matches[-1]) == 0 or 1000 <= int(matches[-1]) <= 600000) else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--state", required=True, type=int, help="UI_STATE_SET_TELEMETRY from the built firmware")
    parser.add_argument("--disable-check", action="store_true", help="Verify zero and restoration through the menu owner")
    parser.add_argument("--capture-output", help="Optional prefix for checked post-save OLED payloads")
    args = parser.parse_args()
    prefix = Path(args.output)
    result = {"completed": False, "checks": [], "physical_output_verified": False}
    original = None
    expected = None
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix('.log').open('w', encoding='utf-8') as log:
        def command(text, accept, timeout=8):
            time.sleep(0.35)  # Respect the real menu button debounce between commands.
            port.reset_input_buffer()
            port.write((text + "\r\n").encode())
            log.write("\nCOMMAND: " + text + "\n")
            received = ""
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                part = port.read(port.in_waiting or 1).decode('utf-8', errors='replace')
                log.write(part); log.flush(); received += part
                if accept(received): return received
            raise RuntimeError("Missing fresh CLI result: " + text)

        def capture_saved_page(name):
            if not args.capture_output:
                return
            text = command('oled frame --machine', lambda value: re.search(
                r'^RACOOL_CLI v=1 type=oled_frame_end seq=\d+ fnv=[0-9a-f]{8}\r*\n', value, re.M) is not None)
            frame = parse_frame(text)
            target = Path(args.capture_output + '-' + name)
            target.with_suffix('.bin').write_bytes(frame['pixels'])
            target.with_suffix('.json').write_text(json.dumps(
                {key: value for key, value in frame.items() if key != 'pixels'}, indent=2), encoding='utf-8')
            write_png(frame, target.with_name(target.name + '-8x').with_suffix('.png'), 8, visible=True)

        def read_interval(expected=None):
            for _ in range(6):
                actual = interval(command('config.show --machine', lambda t: interval(t) is not None))
                if expected is None or actual == expected: return actual
                time.sleep(0.25)
            raise RuntimeError(f"Configured telemetry interval {actual}, expected {expected}")

        def read_menu(expected=None):
            actual = menu_state(command('menu status --machine', lambda t: menu_state(t) is not None))
            if expected is not None and actual != expected:
                raise RuntimeError(f"Menu state {actual}, expected {expected}")
            return actual

        session = DeveloperSession(lambda text: command(text, has_mode_response))
        try:
            session.open()
            wait_for_ui_ready(command)
            if read_menu() != 0:
                raise RuntimeError("Refusing to interrupt a non-monitoring menu session")
            status=command('UI STATUS --machine',lambda t: re.search(r'^RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle\r*\n',t,re.M) is not None)
            result['original_screen']=re.findall(r'^RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle\r*\n',status,re.M)[-1]
            original = read_interval()
            result['original_interval_ms'] = original
            if args.disable_check and original % 5000:
                raise RuntimeError('Zero check requires an exactly restorable 5000 ms interval')
            prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
            command('UI WAKE --machine',lambda t: 'RACOOL_CLI v=1 type=ui_command command=WAKE result=queued error=0' in t)
            time.sleep(0.6)
            read_menu(0)
            direction = -1 if original > 595000 else 1
            command(f'menu nav {args.state} --machine', lambda t: 'RACOOL_CLI v=1 type=menu_navigation result=queued state=' in t)
            time.sleep(1)
            read_menu(args.state)
            command('menu click --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=button' in t)
            time.sleep(0.5)
            command(f'menu enc {direction} --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in t)
            expected = original + direction * 5000
            read_interval(original)
            command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
            actual = read_interval(expected)
            result['checks'].append({'path': 'menu_encoder_to_configuration', 'expected': expected, 'actual': actual})
            capture_saved_page('saved')
            command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
            command(f'menu enc {-direction} --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in t)
            command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
            actual = read_interval(original)
            result['checks'].append({'path': 'menu_reverse_to_configuration', 'expected': original, 'actual': actual})
            capture_saved_page('restored')
            if args.disable_check:
                command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
                command('menu enc -127 --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in t)
                command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
                result['checks'].append({'path': 'menu_zero', 'actual': read_interval(0)})
                capture_saved_page('disabled')
                command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
                if original:
                    command(f'menu enc {original // 5000} --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in t)
                command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
                result['checks'].append({'path': 'menu_positive_resume', 'actual': read_interval(original)})
                capture_saved_page('resumed')
            result['completed'] = True
        except Exception as exc:
            result['error'] = str(exc)
        finally:
            if original is not None:
                try:
                    command('menu reset --machine', lambda t: 'type=menu_navigation result=queued state=0' in t)
                    time.sleep(0.3)
                    read_menu(0)
                    current = read_interval()
                    if current not in (original, expected, 0 if args.disable_check else original):
                        raise RuntimeError('Refusing to overwrite a foreign telemetry change')
                    if current != original:
                        delta = original - current
                        if delta % 5000 or abs(delta // 5000) > 127:
                            raise RuntimeError('Cannot safely restore unexpected interval')
                        command(f'menu nav {args.state} --machine', lambda t: 'type=menu_navigation result=queued state=' in t)
                        time.sleep(0.3)
                        read_menu(args.state)
                        command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
                        command(f'menu enc {delta // 5000} --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in t)
                        command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
                    result['restored_interval_ms'] = read_interval(original)
                    command('menu reset --machine', lambda t: 'RACOOL_CLI v=1 type=menu_navigation result=queued state=0' in t)
                    time.sleep(0.3)
                    read_menu(0)
                    if result.get('original_screen')=='suspended':
                        deadline=time.monotonic()+150
                        while True:
                            status=command('UI STATUS --machine',lambda t: re.search(r'^RACOOL_CLI v=1 type=ui screen=(active|suspended) service=2 boot=idle\r*\n',t,re.M) is not None)
                            if 'type=ui screen=suspended service=2 boot=idle' in status: break
                            if time.monotonic()>deadline: raise RuntimeError('Screen sleep restoration failed')
                            time.sleep(1)
                    result['restoration_verified'] = True
                except Exception as exc:
                    result['completed'] = False
                    result['restore_error'] = str(exc)
            try:
                session.close()
            except Exception as exc:
                result['completed'] = False; result['developer_mode_restore_error'] = str(exc)
            prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['completed'] and result.get('restoration_verified') else 1


if __name__ == '__main__':
    raise SystemExit(main())
