import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {bridgeRequest} from './diagnostic/bridge_request.mjs';
test('bridge RPC ignores stale replies and correlates the exact topic and transaction',async()=>{
    const events=new EventEmitter();
    const result=await bridgeRequest(events,(topic,request)=>{
        events.emit('message','base/bridge/response/read',{transaction:'old',status:'ok'});
        events.emit('message','base/bridge/response/other',{transaction:request.transaction,status:'ok'});
        events.emit('message','base/bridge/response/read',{transaction:request.transaction,status:'ok',data:{value:1}});
    },'base','read',{});
    assert.equal(result.data.value,1);
    assert.equal(events.listenerCount('message'),0);
});
test('bridge errors and timeouts reject and remove listeners',async()=>{
    const events=new EventEmitter();
    const secret='FAKE_SECRET_BACKUP_CONTENT_MUST_NOT_APPEAR';
    await assert.rejects(bridgeRequest(events,(_,request)=>{
        events.emit('message','base/bridge/response/read',{transaction:request.transaction,
            status:'error',error:'device unavailable: '+secret,
            data:{backup:secret}});
    },'base','read',{}),error=>{
        assert.match(error.message,/Bridge request rejected/);
        assert.doesNotMatch(error.message,/FAKE_SECRET|device unavailable/);
        assert.equal(Object.hasOwn(error,'data'),false);
        return true;
    });
    await assert.rejects(bridgeRequest(events,(_,request)=>{
        events.emit('message','base/bridge/response/read',
            {transaction:request.transaction,status:'rejected',error:'another fake token'});
    },'base','read',{}),error=>{
        assert.equal(error.message,'Bridge request rejected; remote error details redacted');
        return true;
    });
    await assert.rejects(bridgeRequest(events,()=>{},'base','read',{},5),/timeout/);
    assert.equal(events.listenerCount('message'),0);
});
test('publish failures remove pending request listeners',async()=>{
    const events=new EventEmitter();
    await assert.rejects(bridgeRequest(events,()=>{throw Error('closed');},'base','read',{}),/closed/);
    assert.equal(events.listenerCount('message'),0);
    assert.equal(events.listenerCount('transport_closed'),0);
});

test('a closed transport rejects an in-flight request without waiting for timeout',async()=>{
    const events=new EventEmitter();
    const pending=bridgeRequest(events,()=>{},'base','read',{},10000);
    events.emit('transport_closed');
    await assert.rejects(pending,/connection closed/i);
    assert.equal(events.listenerCount('message'),0);
    assert.equal(events.listenerCount('transport_closed'),0);
});
