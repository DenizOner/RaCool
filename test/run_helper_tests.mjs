import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const suites = JSON.parse(fs.readFileSync(path.join(root, 'test/helper_suites.json'), 'utf8'));
if (!suites.length || new Set(suites.map(suite => suite.file)).size !== suites.length)
    throw Error('Empty or duplicate helper suite registration');
const output = path.join(root, 'test/build/helpers');
fs.mkdirSync(output, {recursive: true});
const python = process.env.RACOOL_PYTHON || path.join(root,
    process.platform === 'win32' ? '.venv/Scripts/python.exe' : '.venv/bin/python');
const results = [];
for (const suite of suites) {
    const file = path.join(root, suite.file);
    if (!fs.existsSync(file)) throw Error('Missing helper suite: ' + suite.file);
    if (suite.runner === 'powershell' && process.platform !== 'win32')
        throw Error('PowerShell wrapper acceptance requires Windows: ' + suite.file);
    const command = suite.runner === 'node' ? process.execPath :
        suite.runner === 'python' ? python : suite.runner === 'powershell' ? 'powershell.exe' : null;
    if (!command) throw Error('Unknown helper runner: ' + suite.runner);
    const args = suite.runner === 'node' ? ['--test', '--test-reporter=tap', file] :
        suite.runner === 'python' ? [file] : ['-ExecutionPolicy', 'Bypass', '-File', file];
    const run = spawnSync(command, args, {cwd: root, encoding: 'utf8',
        timeout: 120000, maxBuffer: 16 * 1024 * 1024, windowsHide: true});
    const log = (run.stdout ?? '') + (run.stderr ?? '') + (run.error ? '\n' + run.error.message : '');
    const tests = suite.runner === 'node' ? Number(log.match(/^# tests (\d+)$/m)?.[1] ?? 0) :
        suite.runner === 'python' ? Number(log.match(/^Ran (\d+) tests? in /m)?.[1] ?? 0) :
        [...log.matchAll(/^PASS:/gm)].length;
    const complete = suite.runner === 'node' ?
        ['fail', 'cancelled', 'skipped', 'todo'].every(name => new RegExp('^# ' + name + ' 0$', 'm').test(log)) :
        suite.runner === 'python' ? /^OK$/m.test(log) : tests === 2;
    const passed = !run.error && run.status === 0 && tests > 0 && complete;
    fs.writeFileSync(path.join(output, path.basename(file) + '.log'), log);
    results.push({file: suite.file, tests, passed, exitCode: run.status});
    console.log(`${passed ? 'PASS' : 'FAIL'} ${suite.file}: ${tests} cases`);
    if (!passed) console.log(log.slice(-5000));
}
const summary = {timestamp: new Date().toISOString(), suites: results.length,
    tests: results.reduce((sum, result) => sum + result.tests, 0),
    failed: results.filter(result => !result.passed).length, results};
fs.writeFileSync(path.join(output, 'summary.json'), JSON.stringify(summary, null, 2));
console.log(`${summary.suites} helper suites, ${summary.tests} cases, ${summary.failed} failed`);
process.exitCode = summary.failed ? 1 : 0;
