import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs/promises';
import path from 'node:path';
const execute = promisify(execFile);

export function builtinBrightnessPercentToRaw(percent) {
    if (!Number.isInteger(percent) || percent < 0 || percent > 100) throw Error('Invalid brightness percentage');
    return Math.round(percent * 255 / 100);
}

export function parseBuiltinStatus(text) {
    const rows=text.match(/^RACOOL_CLI [^\r\n]*type=builtin_status[^\r\n]*(?:\r*\n|$)/gm);
    if(!rows?.length || !rows.at(-1).endsWith('\n')) throw Error('Missing complete LED worker record');
    const entries=rows.at(-1).trim().split(' ').slice(1).map(token=>token.split('='));
    if(entries.some(entry=>entry.length!==2)||new Set(entries.map(entry=>entry[0])).size!==entries.length) throw Error('Invalid LED fields');
    const fields=Object.fromEntries(entries);
    if(fields.v!=='1'||fields.type!=='builtin_status')throw Error('Unsupported LED record');
    const bounds={on:[0,1],enabled:[0,1],brightness:[0,255],animation:[-1,20],priority:[0,2],remaining_ms:[0,4294967295],plugins:[0,255],driver_error:[-2147483648,2147483647]};
    const result={};
    for(const [key,[min,max]] of Object.entries(bounds)) {
        if(!/^-?[0-9]+$/.test(fields[key]??'') || !Number.isSafeInteger(Number(fields[key])) || Number(fields[key])<min || Number(fields[key])>max)throw Error('Invalid LED field '+key);
        result[key]=Number(fields[key]);
    }
    return result;
}
export function parseBuiltinBrightness(text) {return parseBuiltinStatus(text).brightness;}

export async function observeBuiltinBrightness(port, expected) {
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts' : 'bin', process.platform === 'win32' ? 'python.exe' : 'python');
    const output = path.join('test', 'build', `brightness-mqtt-serial-${Date.now()}.log`);
    await execute(python, ['test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', port, '--duration', '3', '--output', output, '--developer', '--command', 'bled status --machine'], {timeout: 45000});
    const actual = parseBuiltinBrightness(await fs.readFile(output, 'utf8'));
    if (actual !== expected) throw Error(`Worker brightness ${actual}, expected ${expected}`);
    return {kind: 'serial_worker_status', actual, timestamp: Date.now(), artifact: output};
}

export function parseBuiltinMode(text) {
    const status=parseBuiltinStatus(text);
    return {enabled:Boolean(status.enabled),on:Boolean(status.on)};
}

export async function observeBuiltinMode(port, expected) {
    if (!['Kapalı', 'Otomatik'].includes(expected)) throw Error('Unsupported built-in LED mode');
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts' : 'bin', process.platform === 'win32' ? 'python.exe' : 'python');
    const output = path.join('test', 'build', `mode-mqtt-serial-${Date.now()}.log`);
    await execute(python, ['test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', port, '--duration', '3', '--output', output, '--developer', '--command', 'bled status --machine'], {timeout: 45000});
    const actual = parseBuiltinMode(await fs.readFile(output, 'utf8'));
    if (actual.enabled !== (expected === 'Otomatik') || (!actual.enabled && actual.on))
        throw Error(`Worker mode ${JSON.stringify(actual)}, expected ${expected}`);
    return {kind: 'serial_worker_global_mode', actual, timestamp: Date.now(), artifact: output};
}
