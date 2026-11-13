import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {checkTestLayout} from '../check_test_layout.mjs';

function fixture(t) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'racool-layout-'));
    t.after(() => {
        assert.ok(path.resolve(root).startsWith(path.join(path.resolve(os.tmpdir()), 'racool-layout-')));
        fs.rmSync(root, {recursive: true, force: true});
    });
    for (const folder of ['src/Hardware/BuiltinLED', 'test/Hardware/BuiltinLED', 'test/Infrastructure', 'test/test_target_smoke'])
        fs.mkdirSync(path.join(root, folder), {recursive: true});
    fs.writeFileSync(path.join(root, 'test/target_suites.json'), JSON.stringify([{file: 'test/test_target_smoke/test_main.c'}]));
    return root;
}

test('allows module tests, shared infrastructure and declared PlatformIO entry directories', t => {
    const root = fixture(t);
    fs.mkdirSync(path.join(root, 'test/Hardware/BuiltinLED/diagnostic'), {recursive: true});
    assert.deepEqual(checkTestLayout(root), {modules: 1, targetEntryDirectories: 1});
});

test('rejects a module folder without a source counterpart', t => {
    const root = fixture(t);
    fs.mkdirSync(path.join(root, 'test/Hardware/Missing'));
    assert.throws(() => checkTestLayout(root), /Hardware\/Missing/);
});

test('rejects an undeclared standalone test directory', t => {
    const root = fixture(t);
    fs.mkdirSync(path.join(root, 'test/diagnostic'));
    assert.throws(() => checkTestLayout(root), /test\/diagnostic/);
});
