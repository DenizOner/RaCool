import test from 'node:test';
import assert from 'node:assert/strict';
import {fanSourceReadRequest,fanSourceReadResponse,fanSourceObservationDuration} from './diagnostic/fan_source_wire.mjs';
const device={ieee_address:'0x0000000000000001',network_address:42};
const response={clusterID:0x0204,endpoint:1,address:42,data:{type:'Buffer',data:[8,7,1,0x53,0x80,0,0x29,0xec,0xff]}};

test('observation window remains bounded and invalid values fail before log mutation',()=>{
 assert.equal(fanSourceObservationDuration(),22000);
 assert.equal(fanSourceObservationDuration('60000'),60000);
 assert.equal(fanSourceObservationDuration(45000),45000);
 for(const value of ['',null,'bad',NaN,Infinity,0,21999,60001,30000.5])
  assert.throws(()=>fanSourceObservationDuration(value));
});
test('signed delta wire read preserves negative tenths and route',()=>{
 assert.equal(fanSourceReadRequest(device,[0x8053]).params.dst_endpoint,1);
 assert.equal(fanSourceReadResponse(response,device,[0x8053])[0x8053],-20);
});
test('foreign, incomplete and failed wire responses rejected',()=>{
 for(const value of [{...response,endpoint:3},{...response,address:43},{...response,data:{type:'Buffer',data:response.data.data.slice(0,-1)}}])
  assert.throws(()=>fanSourceReadResponse(value,device,[0x8053]));
 assert.throws(()=>fanSourceReadResponse(response,device,[0x8053,0x8054]));
 const failed=structuredClone(response);failed.data.data[5]=0x86;assert.throws(()=>fanSourceReadResponse(failed,device,[0x8053]));
});
test('invalid target and duplicate requests rejected before submission',()=>{
 assert.throws(()=>fanSourceReadRequest(device,[0x8053,0x8053]));
 assert.throws(()=>fanSourceReadRequest({...device,network_address:0xfff8}));
});
