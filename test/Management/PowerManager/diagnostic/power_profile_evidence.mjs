import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs/promises';
import path from 'node:path';
const execute = promisify(execFile);
const profiles = ['Performans', 'Dengeli', 'Tasarruf'];

export function parsePowerProfile(text) {
    const records = [...text.matchAll(/^RACOOL_CLI v=1 type=power_status mode=(-?\d+) locks=\d+ sleeps=\d+ wakeups=\d+ restarts=\d+ uptime_s=\d+\r?$/gm)];
    const mode = records.length ? Number(records.at(-1)[1]) : -1;
    if (!Number.isInteger(mode) || mode < 0 || mode >= profiles.length) throw Error('Unknown applied power profile');
    return profiles[mode];
}

export async function observePowerProfile(port, expected) {
    if (!profiles.includes(expected)) throw Error('Invalid expected profile');
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts/python.exe' : 'bin/python');
    const stamp = Date.now();
    const artifacts = [];
    let actual;
    for (const command of ['power status --machine', 'power selftest --machine']) {
        const output = path.join('test', 'build', `power-mqtt-${stamp}-${artifacts.length}.log`);
        await execute(python, ['test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', port, '--duration', '3', '--output', output, '--developer', '--command', command], {timeout: 45000});
        artifacts.push(output);
        const text = await fs.readFile(output, 'utf8');
        if (command === 'power status --machine') {
            actual = parsePowerProfile(text);
            if (actual !== expected) throw Error(`Applied profile ${actual}, expected ${expected}`);
        } else if (!/^RACOOL_CLI v=1 type=power_selftest result=passed error=0\r?$/m.test(text)) throw Error('Power SDK self-test did not pass');
    }
    return {kind: 'serial_applied_profile_and_sdk_selftest', actual, timestamp: stamp, artifacts};
}
