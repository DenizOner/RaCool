import assert from 'node:assert/strict';
import test from 'node:test';
import {normalizeDeviceIEEE, selectRaCoolDevice} from './diagnostic/device_selection.mjs';
const device = address => ({ieee_address: address, definition: {model: 'RaCool-ESP32C6'}});
const first = device('0x0000000000000001'), second = device('0x0000000000000002');
test('single candidate is selected and absent inventory remains empty', () => {
 assert.equal(selectRaCoolDevice([first]), first);
 assert.equal(selectRaCoolDevice([]), undefined);
});
test('multiple candidates require an explicit selector', () => {
 assert.throws(() => selectRaCoolDevice([first, second]), /Multiple/);
 assert.equal(selectRaCoolDevice([first, second], second.ieee_address), second);
});
test('foreign and missing selectors are rejected', () => {
 assert.throws(() => selectRaCoolDevice([first, {ieee_address: second.ieee_address, model_id: 'Other'}], second.ieee_address), /absent/);
 assert.throws(() => selectRaCoolDevice([], first.ieee_address), /absent/);
});
test('selector syntax is strict and case insensitive', () => {
 assert.equal(normalizeDeviceIEEE('0xABCDEF0000000001'), '0xabcdef0000000001');
 for (const value of ['', '1', ' 0x0000000000000001', 1, {}]) assert.throws(() => normalizeDeviceIEEE(value));
});
test('malformed or duplicate candidate identities fail closed', () => {
 for (const devices of [[first, {...first}], [device(undefined)], [device('bad')]]) assert.throws(() => selectRaCoolDevice(devices));
 assert.throws(() => selectRaCoolDevice({}), /inventory/);
});
test('legacy model identifier is supported without selecting other products', () => {
 const legacy = {model_id: 'RaCool-ESP32C6', ieee_address: first.ieee_address};
 assert.equal(selectRaCoolDevice([{model_id: 'Other'}, legacy]), legacy);
});
