import {test} from 'node:test';
import assert from 'node:assert/strict';
import {isUnrelatedDeviceRouteError} from './diagnostic/bridge_error_scope.mjs';
const device={friendly_name:'under test',ieee_address:'0x0011223344556677'};
const other="z2m: Failed to configure 'other device', attempt 1 (Error: ZCL command 0x8899aabbccddeeff/1 genBasic.read([]) failed (Data request failed with error: 'NWK_NO_ROUTE' (0xcd)))";
test('identifies an explicit other-device route failure without hiding target failures',()=>{
    assert.equal(isUnrelatedDeviceRouteError(other,device),true);
    assert.equal(isUnrelatedDeviceRouteError(other.replace('other device','under test'),device),false);
    assert.equal(isUnrelatedDeviceRouteError(other.replace('0x8899aabbccddeeff',device.ieee_address),device),false);
});
test('unknown, global and incompletely identified errors still fail verification',()=>{
    for(const message of ['adapter disconnected','NWK_NO_ROUTE',other+' Inter-PAN active',
        other.replace('0x8899aabbccddeeff','unknown'),other.replace('NWK_NO_ROUTE','MAC_CHANNEL_ACCESS_FAILURE')])
        assert.equal(isUnrelatedDeviceRouteError(message,device),false);
    assert.equal(isUnrelatedDeviceRouteError(other,undefined),false);
});
