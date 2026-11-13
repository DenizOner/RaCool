import {test} from 'node:test';
import assert from 'node:assert/strict';
import {colorRequest,colorResponse} from './diagnostic/color_command.mjs';
const device={ieee_address:'0x0011223344556677',network_address:123};
const response=bytes=>({clusterID:0x300,endpoint:3,address:123,data:{type:'Buffer',data:bytes}});
test('color requests use unicast standard commands and reject broadcast or invalid coordinates',()=>{
    const request=colorRequest(device,{x:45875,y:19661});
    assert.equal(request.params.zcl.command_key,'moveToColor');
    assert.equal(request.params.disable_response,false);
    assert.equal(request.params.zcl.payload.optionsOverride,1);
    assert.throws(()=>colorRequest({...device,network_address:0xfffc},{x:1,y:2}));
    assert.throws(()=>colorRequest(device,{x:1,y:0}));
    assert.equal(colorRequest(device).params.zcl.command_key,'read');
});
test('acceptance requires complete successful default and coordinate wire responses',()=>{
    assert.equal(colorResponse(response([24,1,11,7,0]),device,'moveToColor').status,0);
    assert.throws(()=>colorResponse(response([24,1,11,7,1]),device,'moveToColor'));
    assert.throws(()=>colorResponse({...response([24,1,11,7,0]),endpoint:4},device,'moveToColor'));
    const bytes=[24,1,1,3,0,0,33,51,179,4,0,0,33,205,76];
    assert.deepEqual(colorResponse(response(bytes),device,'read'),{x:45875,y:19661});
    assert.throws(()=>colorResponse(response(bytes.slice(0,9)),device,'read'));
    assert.throws(()=>colorResponse(response([...bytes.slice(0,9),...bytes.slice(3,9)]),device,'read'));
});

import {zoneColorRequest,zoneColorResponse,rangeResponse,normalizedZoneColor} from './diagnostic/zone_color.mjs';
test('zone wire parser rejects wrong routes, malformed pairs and failed responses',()=>{
    const target={ieee_address:'0x0000000000000001',network_address:123};
    for (let zone=0;zone<5;++zone) {
        assert.equal(zoneColorRequest(target,zone).params.zcl.payload[0].attrId,0x8030+zone);
        const bytes=[0x18,1,1,0x30+zone,0x80,0,0x23,0xd2,4,0xe1,0x10];
        const response={clusterID:6,endpoint:3,address:123,data:{type:'Buffer',data:bytes}};
        assert.deepEqual(zoneColorResponse(response,target,zone),{x:1234,y:4321});
        assert.throws(()=>zoneColorResponse({...response,endpoint:2},target,zone));
        assert.throws(()=>zoneColorResponse(response,target,(zone+1)%5));
        bytes[5]=1;assert.throws(()=>zoneColorResponse(response,target,zone));bytes[5]=0;
        bytes[9]=0;bytes[10]=0;assert.throws(()=>zoneColorResponse(response,target,zone));
    }
});

test('range parser requires six distinct successful signed attribute responses',()=>{
    const target={ieee_address:'0x0000000000000001',network_address:123};
    const bytes=[0x18,1,1];
    for (let index=0;index<6;++index) {const value=(17+index)*100;bytes.push(0x20+index,0x80,0,0x29,value&255,value>>8);}
    const response={clusterID:6,endpoint:3,address:123,data:{type:'Buffer',data:bytes}};
    assert.deepEqual(rangeResponse(response,target),[[17,18],[19,20],[21,22]]);
    bytes[5]=1;assert.throws(()=>rangeResponse(response,target));
});

test('legacy wire colors normalize exactly and malformed pairs are rejected',()=>{
 assert.deepEqual(normalizedZoneColor({x:16384,y:32768}),{x:0.25,y:0.5});
 for(const value of [null,[],{x:1},{x:NaN,y:1},{x:1,y:0},{x:65535,y:1},{x:1,y:1,z:1}]) assert.throws(()=>normalizedZoneColor(value));
});
