"""Exercise the production menu event path, restore brightness and leave monitoring."""
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
from Hardware.BuiltinLED.diagnostic.builtin_cli_evidence import parse_builtin_status, parse_builtin_command


from Controllers.MenuManager.diagnostic.menu_cli_evidence import menu_state


def brightness(text):
    state = parse_builtin_status(text)
    return state['brightness'] if state else None



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--state", required=True, type=int, help="UI_STATE_SET_BUILTIN_BRIGHTNESS from the built firmware")
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

        def read_brightness(expected=None):
            for _ in range(6):
                actual = brightness(command('builtin_led_info --machine', lambda t: brightness(t) is not None))
                if expected is None or actual == expected: return actual
                time.sleep(0.25)
            raise RuntimeError(f"Worker brightness {actual}, expected {expected}")

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
            original = read_brightness()
            result['original_brightness'] = original
            prefix.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
            command('UI WAKE --machine',lambda t: 'RACOOL_CLI v=1 type=ui_command command=WAKE result=queued error=0' in t)
            time.sleep(0.6)
            read_menu(0)
            direction = -1 if original == 255 else 1
            command(f'menu nav {args.state} --machine', lambda t: 'RACOOL_CLI v=1 type=menu_navigation result=queued state=' in t)
            time.sleep(1)
            read_menu(args.state)
            command('menu click --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=button' in t)
            time.sleep(0.5)
            command(f'menu enc {direction} --machine', lambda t: 'RACOOL_CLI v=1 type=menu_input result=accepted input=encoder' in t)
            percent = (original * 100 + 127) // 255
            next_percent = max(0, min(100, percent + direction))
            expected = 255 if next_percent == 100 else (next_percent * 255 + 50) // 100
            actual = read_brightness(original)
            result['checks'].append({'path': 'unsaved_draft_keeps_worker', 'expected': original, 'actual': actual})
            command('menu click --machine', lambda t: 'type=menu_input result=accepted input=button' in t)
            actual = read_brightness(expected)
            result['checks'].append({'path': 'saved_draft_to_worker', 'expected': expected, 'actual': actual})
            result['completed'] = True
        except Exception as exc:
            result['error'] = str(exc)
        finally:
            if original is not None:
                try:
                    current = read_brightness()
                    if current not in (original, expected):
                        raise RuntimeError('Refusing to overwrite a foreign brightness change')
                    command(f'bled brightness {original} --machine', lambda t: parse_builtin_command(t, 'brightness') == 'accepted')
                    result['restored_brightness'] = read_brightness(original)
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
