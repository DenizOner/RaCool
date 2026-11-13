import test from 'node:test';
import assert from 'node:assert/strict';
import {builtinBrightnessPercentToRaw, parseBuiltinBrightness, parseBuiltinMode} from './diagnostic/builtin_brightness_evidence.mjs';

const record=(brightness=19,enabled=1,on=0)=>`RACOOL_CLI v=1 type=builtin_status on=${on} enabled=${enabled} brightness=${brightness} animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n`;
test('mode evidence separates global enable from an unlit idle selection',()=>{
    assert.deepEqual(parseBuiltinMode(record()),{enabled:true,on:false});
    assert.deepEqual(parseBuiltinMode(record(19,0)),{enabled:false,on:false});
    assert.throws(()=>parseBuiltinMode('Disabled mode saved.'));
});
test('brightness evidence requires latest complete supported worker record',()=>{
    assert.equal(parseBuiltinBrightness(record()),19);
    assert.equal(parseBuiltinBrightness(record(0)),0);
    for(const text of [record().trimEnd(),record(256),record()+record().replace('v=1','v=2'),record()+'RACOOL_CLI v=1 type=builtin_status brightness=1\n'])assert.throws(()=>parseBuiltinBrightness(text));
});
test('brightness percentage evidence maps HA percent to the worker uint8 level', () => {
    assert.deepEqual([0, 1, 50, 80, 100].map(builtinBrightnessPercentToRaw), [0, 3, 128, 204, 255]);
    assert.throws(() => builtinBrightnessPercentToRaw(-1));
    assert.throws(() => builtinBrightnessPercentToRaw(101));
    assert.throws(() => builtinBrightnessPercentToRaw(2.5));
});
