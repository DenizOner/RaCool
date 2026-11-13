import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs/promises';
import path from 'node:path';
const execute = promisify(execFile);
const powers = {Eco: 0, Normal: 10, Boost: 20};

export function parseTxProfile(text) {
    const clean = text.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '');
    const start = clean.lastIndexOf('RACOOL_CLI v=1 type=zigbee_tx ');
    if (start < 0) throw Error('Missing TX status');
    const match = clean.slice(start).match(/^RACOOL_CLI v=1 type=zigbee_tx profile=([0-2]) dbm=(-?\d+)\r*\n/);
    if (!match || powers[Object.keys(powers)[Number(match[1])]] !== Number(match[2])) throw Error('Invalid or incomplete SDK TX status');
    return {profile: Object.keys(powers)[Number(match[1])], dbm: Number(match[2])};
}

export async function observeTxProfile(port, expected) {
    if (!Object.hasOwn(powers, expected)) throw Error('Invalid expected TX profile');
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts/python.exe' : 'bin/python');
    const stamp = Date.now();
    const output = path.join('test', 'build', `tx-mqtt-${stamp}.log`);
    await execute(python, ['test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', port, '--duration', '3', '--output', output, '--command', 'ZB TX --machine'], {timeout: 10000});
    const text = await fs.readFile(output, 'utf8');
    if (!text.includes('Executing: ZB TX --machine') || /(?:^|\n)E \(|Guru Meditation|ESP-ROM:/.test(text))
        throw Error('Missing current query or runtime error in TX observation');
    const actual = parseTxProfile(text);
    if (actual.profile !== expected) throw Error(`SDK TX profile ${actual.profile}, expected ${expected}`);
    return {kind: 'serial_sdk_configured_tx_power', actual, timestamp: stamp, artifacts: [output]};
}
