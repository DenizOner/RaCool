import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs/promises';
import path from 'node:path';
const execute = promisify(execFile);

export function parseRgbStatus(text) {
    const clean = text.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '');
    const rows = clean.split(/\r*\n/).filter(line => line.startsWith('RACOOL_CLI ') && line.includes('type=rgb_status'));
    if (!rows.length) throw Error('Missing RGB worker status');
    const fields = Object.fromEntries(rows.at(-1).split(' ').slice(1).map(field => field.split('=')));
    if (fields.v !== '1' || fields.type !== 'rgb_status') throw Error('Unsupported RGB worker status');
    const limits = {brightness:255, mode:3, target_r:255, target_g:255, target_b:255, priority:3, animation:22, on:1, fading:1, current_r:255, current_g:255, current_b:255};
    for (const [name, maximum] of Object.entries(limits)) {
        if (!/^\d+$/.test(fields[name] ?? '') || Number(fields[name]) > maximum)
            throw Error('Invalid or incomplete RGB worker field: ' + name);
    }
    const tail = clean.slice(clean.lastIndexOf('RACOOL_CLI '));
    if (!tail.includes('\n')) throw Error('Incomplete RGB worker record');
    return {brightness:Number(fields.brightness), mode:Number(fields.mode), color:['target_r','target_g','target_b'].map(key => Number(fields[key])), priority:Number(fields.priority)};
}

export async function observeRgbWorker(port) {
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts/python.exe' : 'bin/python');
    const output = path.join('test', 'build', `rgb-brightness-${Date.now()}.log`);
    await execute(python, ['test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', port, '--duration', '3', '--output', output,
        '--developer', '--command', 'rgbled_info --machine'], {timeout: 45000});
    const text = await fs.readFile(output, 'utf8');
    if (/Guru Meditation|ESP-ROM:|(?:^|\n)E \(/.test(text)) throw Error('Runtime fault during RGB observation');
    const actual = parseRgbStatus(text);
    const animation = [...text.matchAll(/RACOOL_CLI v=1 type=rgb_status[^\n]* animation=(\d+)[^\n]*\n/g)].at(-1);
    return {kind:'serial_rgb_worker',actual,activeAnimation:animation ? Number(animation[1]) : undefined,artifact:output,timestamp:Date.now()};
}
export async function observeRgbBrightness(port, percent) {
    if (!Number.isInteger(percent) || percent < 0 || percent > 100) throw Error('Invalid brightness expectation');
    const observation = await observeRgbWorker(port);
    const actual = observation.actual;
    // The converter uses the ZCL range 0..254.
    const expected = Math.round(percent * 254 / 100);
    if (actual.brightness !== expected) throw Error(`RGB brightness ${actual.brightness}, expected ${expected}`);
    if (process.env.RACOOL_RGB_EXPECT_COLOR) {
        const color = process.env.RACOOL_RGB_EXPECT_COLOR.split(',').map(Number);
        if (color.length !== 3 || color.some(v => !Number.isInteger(v) || v < 0 || v > 255)) throw Error('Invalid RGB color expectation');
        if (actual.mode !== 0 || actual.color.some((v, i) => v !== color[i])) throw Error('Brightness changed manual RGB color');
    }
    return observation;
}
