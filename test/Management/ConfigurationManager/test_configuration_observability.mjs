import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';

function validateObservableFields(owner, view) {
    const writable = [...owner.replace(/^#define.*$/gm, "").matchAll(/CONFIG_FIELD\(\s*(\w+)\s*,\s*\w+\s*,\s*(\w+)\s*\)/g)].map(match => `${match[1]}.${match[2]}`);
    const observed = [...view.matchAll(/FIELD\(\s*(\w+)\s*,\s*(\w+)\s*\)/g)].map(match => `${match[1]}.${match[2]}`);
    observed.push('localization.locale');
    assert.equal(new Set(observed).size, observed.length, 'Duplicate observable field');
    for (const field of writable) assert.ok(observed.includes(field), `Writable field missing from show/diff: ${field}`);
    assert.ok(writable.length > 60, 'Owner field inventory is incomplete');
}
const owner = fs.readFileSync('src/Management/ConfigurationManager/ConfigurationManager.c', 'utf8');
const view = fs.readFileSync('include/Management/ConfigurationManager/ConfigurationObservableFields.h', 'utf8');
test('every writable setting is present in the shared console view and diff', () => validateObservableFields(owner, view));
test('a newly writable field cannot silently disappear from the CLI', () => {
    assert.throws(() => validateObservableFields(owner + '\nCONFIG_FIELD(system, SystemBaseConfigs, future_setting)', view), /future_setting/);
});
test('duplicate observable fields fail inventory acceptance', () => {
    assert.throws(() => validateObservableFields(owner, view + '\nFIELD(system,temp_unit)'), /Duplicate/);
});
