import test from 'node:test';
import assert from 'node:assert/strict';
import {reportIntervalRequest,reportIntervalResponse,reportIntervalPair} from './diagnostic/reporting_interval_wire.mjs';

const device={ieee_address:'0x0000000000000123',network_address:123};
const names=['radiator','ambient','processor','health','memory','uptime'];
const endpoints=[1,4,5,6,7,8];
const suffixes=['climate','ambient_sensor','processor_sensor','system_health','memory','uptime'];
function response(endpoint,id,value=3){return {clusterID:0x0204,endpoint,address:123,
    data:{type:'Buffer',data:[0x18,9,1,id&255,id>>8,0,0x21,value&255,value>>8]}};}
test('six reporting controls use their actual endpoint and correlated uint16 response',()=>{
    names.forEach((name,index)=>{
        const control='report_interval_'+name+'_s';
        assert.deepEqual(reportIntervalPair(control,control+'_'+suffixes[index]),{endpoint:endpoints[index],attribute:0x8030+index});
        assert.throws(()=>reportIntervalPair(control,control+'_'+suffixes[(index+1)%suffixes.length]));
        const req=reportIntervalRequest(device,control);
        assert.equal(req.params.dst_endpoint,endpoints[index]);
        assert.equal(req.params.zcl.payload[0].attrId,0x8030+index);
        assert.equal(reportIntervalResponse(response(endpoints[index],0x8030+index),device,control),3);
    });
});
test('foreign, malformed, failed and out-of-range responses cannot authorize restoration',()=>{
    const control='report_interval_radiator_s', good=response(1,0x8030);
    for(const data of [{...good,address:124},{...good,endpoint:4},response(1,0x8031),
        {...good,data:{type:'Buffer',data:[0x18,9,1,0x30,0x80,1]}},response(1,0x8030,601)])
        assert.throws(()=>reportIntervalResponse(data,device,control));
    assert.throws(()=>reportIntervalRequest(device,'unrelated'));
    assert.throws(()=>reportIntervalPair('unrelated','unrelated'));
});
