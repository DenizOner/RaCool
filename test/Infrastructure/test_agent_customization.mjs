import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const rules = read('AGENTS.md');
const legacy = read('.agent/rules/GEMINI.md');
const codebase = read('CODEBASE.md');

test('normal development keeps the existing authority and native test contract', () => {
    assert.match(legacy, /trigger: always_on/);
    assert.match(legacy, /repository-root \[AGENTS\.md\]/);
    assert.match(legacy, /primary authorized[\s\S]+executor handles reachable source/);
    assert.match(legacy, /baseline tests/);
    assert.match(legacy, /conditional PR merge follows/);
    assert.match(rules, /single writer/i);
    assert.doesNotMatch(legacy, /P0 \(GEMINI\.md\)|highest priority rule|GLOBAL SOCRATIC GATE/);
    assert.doesNotMatch(legacy, /ASK minimum 3|skipping the announcement.*VIOLATION/i);
});

test('separate read-only reviewer cannot write source or confer physical PASS', () => {
    assert.match(legacy, /separate execution session/);
    assert.match(legacy, /reviewer must not change code/);
    assert.match(legacy, /PASS\/FAIL\/BLOCKED/);
    assert.match(rules, /independent read-only execution session/i);
    assert.match(rules, /No physical PASS|physical PASS/i);
});

test('legacy catalog remains present but is not a new permission or harness guarantee', () => {
    for (const directory of ['.agent/agents', '.agent/skills', '.agent/workflows',
        '.agent/scripts']) {
        assert.ok(fs.statSync(path.join(root, directory)).isDirectory(), directory);
    }
    assert.match(legacy, /optional task references/);
    assert.match(legacy, /missing or skipped script is never a PASS/);
    assert.match(legacy, /Actual activation and tool availability require a test/);
    assert.match(codebase, /Antigravity runtime discovery/);
    assert.ok(fs.existsSync(path.join(root, '.vscode/settings.json')));
});

test('existing AGENTS policy and helper registration remain authoritative', () => {
    assert.match(rules, /AGENTS\.md contains durable/);
    assert.match(codebase, /single durable engineering/);
    const helpers = JSON.parse(read('test/helper_suites.json'));
    assert.equal(helpers.filter(s => s.file ===
        'test/Infrastructure/test_agent_customization.mjs').length, 1);
    assert.ok(helpers.some(s => s.file ===
        'test/Communication/Zigbee/test_tasklet_watchdog.py'));
});
