"""Hold a bounded CPU calibration change and restore its exact original value."""
import argparse
import json
import math
import re
import struct
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from cpu_cli_evidence import parse_status as parse_machine_status, parse_read, require_offset, record

NUMBER = r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'


def f32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


def parse_status(text):
    values = parse_machine_status(text)
    for key in ('offset','temperature'):values[key]=f32(values[key])
    return values


def parse_warning(text):
    fields=record(text,'climate_cpu')
    if set(fields)!={'override','cpu_c','warning_c','critical_c','emergency_c','polling_ms'} or fields['override']!='0':
        raise RuntimeError('Unavailable/in-use CPU protection configuration')
    value=float(fields['warning_c'])
    if not math.isfinite(value):
        raise RuntimeError('Missing configured CPU warning threshold')
    return value


def capture(command, hold, ready, checkpoint=lambda _: None):
    result = {'completed': False, 'restoration_verified': False}
    original = None
    attempted = False

    def status():
        return parse_status(command('int_temp status --machine', r'RACOOL_CLI v=1 type=cpu_sensor_status[^\r\n]*[\r\n]'))

    def set_offset(value):
        # A missing acknowledgment never causes a blind mutable retry.
        require_offset(command('int_temp offset ' + format(value, '.9g')+' --machine', r'RACOOL_CLI v=1 type=cpu_sensor_command[^\r\n]*[\r\n]'))
        if status()['offset'] != f32(value):
            raise RuntimeError('CPU calibration readback mismatch')

    try:
        warning = parse_warning(command('CLIMATE CPU --machine', r'RACOOL_CLI v=1 type=climate_cpu[^\r\n]*[\r\n]'))
        parse_read(command('int_temp read --machine', r'RACOOL_CLI v=1 type=cpu_sensor_read[^\r\n]*[\r\n]'))
        initial = status()
        original = initial['offset']
        if initial['level'] != 'NORMAL' or not -9.99 <= original <= 9.99:
            raise RuntimeError('Refusing invalid calibration or active thermal protection')
        requested = f32(original + 3 if original + 3 <= 9.99 else original - 3)
        if initial['temperature'] + max(0, requested - original) + 10 >= warning:
            raise RuntimeError('Insufficient CPU thermal margin')
        result.update(original_offset=original, requested_offset=requested,
                      initial_temperature=initial['temperature'], warning_threshold=warning)
        checkpoint(result.copy())
        attempted = True
        set_offset(requested)
        ready()
        hold()
        parse_read(command('int_temp read --machine', r'RACOOL_CLI v=1 type=cpu_sensor_read[^\r\n]*[\r\n]'))
        final = status()
        if final['offset'] != requested or final['level'] != 'NORMAL':
            raise RuntimeError('CPU calibration changed or thermal protection became active')
        result['changed_temperature'] = final['temperature']
        result['checks_passed'] = True
    except Exception as error:
        result['error'] = str(error)
    finally:
        if original is not None and attempted:
            try:
                if status()['offset'] != original:
                    set_offset(original)
                result['restoration_verified'] = status()['offset'] == original
            except Exception as error:
                result['restore_error'] = str(error)
        result['completed'] = result.get('checks_passed', False) and result['restoration_verified']
    return result


def main():
    import serial
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
    from Infrastructure.diagnostic.serial_reconnect import read_available
    from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port:
        def command(text, expected):
            port.reset_input_buffer()
            port.write((text + '\r\n').encode())
            response = ''
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                response += read_available(port, deadline, lambda _: None).decode('utf-8', errors='replace')
                if re.search(expected, response):
                    return response
            raise RuntimeError('Missing CPU command response: ' + text.split()[0])
        session=DeveloperSession(lambda text:command(text,r'(?m)^RACOOL_CLI v=1 type=developer_mode enabled=[01]\r*$'))
        try:
            session.open()
            result = capture(command, lambda: time.sleep(50),
                             lambda: print('CPU_CALIBRATION_READY', flush=True),
                             lambda result: Path(args.output + '.json').write_text(json.dumps(result,indent=2),encoding='utf-8'))
        finally:
            session.close()
    Path(args.output + '.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result), flush=True)
    return 0 if result['completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
