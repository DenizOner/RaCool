import test from 'node:test';
import assert from 'node:assert/strict';
import {settleBeforeSnapshot} from './diagnostic/live_operation_completion.mjs';

test('snapshot waits for asynchronous cleanup before it is written',async()=>{
    const seen={completed:false};
    let release;
    const operation=new Promise(resolve=>{release=resolve;}).then(()=>{seen.cleanup='failed';});
    let saved;
    const pending=settleBeforeSnapshot(operation,seen,value=>{saved={...value};});
    await new Promise(resolve=>setTimeout(resolve,5));
    assert.equal(saved,undefined);
    release();
    await pending;
    assert.equal(saved.cleanup,'failed');
});

test('unexpected operation rejection is recorded before the snapshot',async()=>{
    const seen={completed:true};
    let saved;
    await settleBeforeSnapshot(Promise.reject(Error('cleanup failed')),seen,value=>{saved={...value};});
    assert.equal(saved.completed,false);
    assert.equal(saved.operationError,'cleanup failed');
});
