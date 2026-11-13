import test from 'node:test';
import assert from 'node:assert/strict';
import {selectVerificationExposes, selectReadExposes} from './diagnostic/verification_selection.mjs';
const calibration = {name:'calibration',property:'calibration_sensor',access:7,type:'numeric'};
const other = {name:'mode',property:'mode_led',access:7,type:'enum'};
const command = {name:'system_command',property:'command',access:7,type:'enum'};
const readOnly = {name:'temperature',property:'temperature',access:5,type:'numeric'};
const writeOnly = {name:'write_only',property:'write_only',access:2,type:'enum'};
const exposes=[calibration,other,command,readOnly,writeOnly];
test('default verification excludes commands and controls without readback',()=>{
    assert.deepEqual(selectVerificationExposes(exposes),[calibration,other]);
});
test('explicit names and properties select and deduplicate controls',()=>{
    assert.deepEqual(selectVerificationExposes(exposes,['calibration','calibration_sensor']),[calibration]);
});
test('unknown, unsafe or ambiguous requests fail before returning controls',()=>{
    for(const name of ['missing','system_command','temperature','write_only'])
        assert.throws(()=>selectVerificationExposes(exposes,['calibration',name]));
    assert.throws(()=>selectVerificationExposes([...exposes,{...calibration}],['calibration']));
});

test('runtime external measurements require dedicated snapshot and invalidation acceptance',()=>{
    const input={name:'external_temperature_1',property:'external_temperature_1_climate',access:7,type:'numeric'};
    assert.deepEqual(selectVerificationExposes([...exposes,input]),[calibration,other]);
    assert.throws(()=>selectVerificationExposes([...exposes,input],['external_temperature_1']));
});

test('explicit reads admit telemetry and deduplicate without enabling mutable verification',()=>{
    assert.deepEqual(selectReadExposes(exposes,['temperature','temperature','mode']),[readOnly,other]);
    assert.throws(()=>selectVerificationExposes(exposes,['temperature']));
});
test('read scope rejects empty, unsafe, unavailable and ambiguous requests',()=>{
    for (const scope of [[],['system_command'],['write_only'],['missing']]) assert.throws(()=>selectReadExposes(exposes,scope));
    assert.throws(()=>selectReadExposes([...exposes,{...readOnly}],['temperature']));
});
