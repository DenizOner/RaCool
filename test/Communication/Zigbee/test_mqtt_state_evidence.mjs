import test from 'node:test';
import assert from 'node:assert/strict';
import {MqttStateEvidence} from './diagnostic/mqtt_state_evidence.mjs';
test('retained or pre-command state cannot satisfy a new read',()=>{
    const e=new MqttStateEvidence(); e.observe({mode:'auto'},false,1);
    const before=e.sequence; e.observe({mode:'auto'},true,2);
    assert.equal(e.matchAfter(before,'mode','auto'),undefined);
});
test('a new message must contain the requested value',()=>{
    const e=new MqttStateEvidence(); e.observe({temperature:30},false,3);
    assert.equal(e.matchAfter(0,'mode','auto'),undefined);
    e.observe({mode:'off'},false,4);
    assert.equal(e.matchAfter(0,'mode','auto'),undefined);
});
test('matching state records observation metadata without claiming ZCL correlation',()=>{
    const e=new MqttStateEvidence(); e.observe({mode:'auto'},false,5);
    assert.deepEqual(e.matchAfter(0,'mode','auto'),{kind:'mqtt_state_after_get',sequence:1,timestamp:5,actual:'auto'});
});
test('a read that was never sent cannot be considered observed',()=>{
    const e=new MqttStateEvidence(); e.observe({mode:'auto'},false);
    assert.equal(e.matchAfter(undefined,'mode','auto'),undefined);
});

test('bounded wait accepts delayed matching state and ignores retained/wrong messages',async()=>{
    const e=new MqttStateEvidence(); e.observe({mode:'off'},false,1);
    const pending=e.waitAfter(e.sequence,'mode','auto',1000);
    e.observe({mode:'auto'},true,2); e.observe({mode:'off'},false,3);
    setTimeout(()=>e.observe({mode:'auto'},false,4),20);
    assert.equal((await pending).timestamp,4);
});
test('initial read waits for a post-request property without assuming a cached value',async()=>{
    const e=new MqttStateEvidence(); e.observe({period:30},false,1);
    const pending=e.waitAfter(e.sequence,'period',undefined,1000);
    e.observe({period:7},false,2);
    assert.equal((await pending).actual,7);
});
test('bounded wait times out without promoting pre-request or missing state',async()=>{
    const e=new MqttStateEvidence(); e.observe({mode:'auto'},false);
    assert.equal(await e.waitAfter(e.sequence,'mode','auto',5),undefined);
    e.observe({temperature:20},false);
    assert.equal(await e.waitAfter(0,'mode','auto',5),undefined);
});
test('invalid states and unbounded requests are rejected',async()=>{
    const e=new MqttStateEvidence(); e.observe(null,false); e.observe([],false); e.observe('auto',false);
    assert.equal(e.sequence,0);
    await assert.rejects(e.waitAfter(undefined,'mode','auto',10));
    await assert.rejects(e.waitAfter(0,'mode','auto',0));
    await assert.rejects(e.waitAfter(0,'mode','auto',Infinity));
});

test('transport closure releases pending reads and prevents new observations',async()=>{
    const e=new MqttStateEvidence();
    const pending=e.waitAfter(0,'mode','auto',10000);
    e.close();
    assert.equal(await pending,undefined);
    e.observe({mode:'auto'},false);
    assert.equal(await e.waitAfter(0,'mode','auto',5),undefined);
});
