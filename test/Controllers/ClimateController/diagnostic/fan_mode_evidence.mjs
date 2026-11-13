import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs/promises';
import path from 'node:path';
const execute = promisify(execFile);
const modes = {'Kapalı': 'MANUAL OFF', 'Nesneye duyarlı': 'OBJECT_SENSITIVE', 'Sürekli Açık': 'MANUAL ON', 'Ortama duyarlı': 'AMBIENT_DELTA'};

export function parseFanEvidence(text, expected, activeHigh) {
    if (!Object.hasOwn(modes, expected) || typeof activeHigh !== 'boolean') throw Error('Invalid fan evidence expectation');
    const clean = text.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '');
    const climateStart=clean.lastIndexOf('type=climate_status');
    const relayStart=clean.lastIndexOf('type=relay_status');
    if (climateStart<0 || relayStart<climateStart) throw Error('Incomplete climate/relay observation');
    const climateLine=clean.slice(clean.lastIndexOf('RACOOL_CLI ',climateStart),relayStart).split('\n')[0];
    const hardwareLine=clean.slice(clean.lastIndexOf('RACOOL_CLI ',relayStart)).split('\n')[0];
    const climate=climateLine.match(/^RACOOL_CLI v=1 type=climate_status mode=([0-3]) on=([01]) stale=[01] debouncing=[01] ir_tick=\d+ object_c=\S+ ambient_c=\S+ cpu_c=\S+ open_c=\S+ close_c=\S+ object_cal_c=\S+ ambient_cal_c=\S+ enabled=[01] manual=([01]) manual_state=([01]) strategy=[01] object_source=[0-3] ambient_source=[0-3] delta_on=-?\d+ delta_off=-?\d+\r*$/);
    const hardware=hardwareLine.match(/^RACOOL_CLI v=1 type=relay_status state=([01]) mode=[01] pin=\d+ level=([01])\r*$/);
    const mode=climate ? ['OBJECT_SENSITIVE','MANUAL ON','MANUAL OFF','AMBIENT_DELTA'][Number(climate[1])] : undefined;
    if (mode!==modes[expected] || !climate || !hardware) throw Error('Missing or mismatched fan worker evidence');
    const manualMode=climate[1]==='1' || climate[1]==='2';
    if ((climate[3]==='1')!==manualMode || (manualMode && (climate[4]==='1')!==(climate[1]==='1'))) throw Error('Inconsistent fan configuration snapshot');
    const on=hardware[1]==='1';
    const gpio=Number(hardware[2]);
    if ((climate[2]==='1')!==on || gpio!==Number(on===activeHigh)) throw Error('Climate relay state and actual GPIO disagree');
    if (!['Nesneye duyarlı', 'Ortama duyarlı'].includes(expected) && on !== (expected === 'Sürekli Açık')) throw Error('Manual fan request was not actuated');
    return {mode, on, gpio: Number(gpio)};
}

export async function observeFanMode(port, expected) {
    const config = await fs.readFile('include/Hardware/Relay/RelayConfig.h', 'utf8');
    const polarity = config.match(/^#define\s+RELAY_ACTIVE_HIGH\s+(true|false)\s*$/m)?.[1];
    if (!polarity) throw Error('Resolve actual relay polarity before verification');
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts/python.exe' : 'bin/python');
    const stamp = Date.now();
    const output = path.join('test', 'build', `fan-mqtt-${stamp}.log`);
    await execute(python, ['test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', port, '--duration', '6', '--output', output,
        '--developer', '--command', 'CLIMATE STATUS --machine', '--command', 'relay_info --machine'], {timeout: 50000});
    const text = await fs.readFile(output, 'utf8');
    if (!text.includes('Executing: CLIMATE STATUS') || !text.includes('Executing: relay_info') ||
        /(?:^|\n)E \(|Guru Meditation|ESP-ROM:/.test(text)) throw Error('Missing queries or runtime fault');
    return {kind: 'serial_climate_relay_and_gpio', actual: parseFanEvidence(text, expected, polarity === 'true'),
        timestamp: stamp, artifacts: [output]};
}
