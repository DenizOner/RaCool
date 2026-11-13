import {test} from 'node:test';
import assert from 'node:assert/strict';
import {parseRgbStatus} from './diagnostic/rgb_brightness_evidence.mjs';
const status = 'RACOOL_CLI v=1 type=rgb_status brightness=254 mode=0 target_r=255 target_g=0 target_b=0 priority=0 animation=0 on=1 fading=0 current_r=255 current_g=0 current_b=0\r\r\n';
test('RGB worker parser accepts USB CRCRLF and full color', () => {
    assert.deepEqual(parseRgbStatus(status), {brightness:254, mode:0, color:[255,0,0], priority:0});
});
test('Latest incomplete RGB status cannot borrow older values', () => {
    assert.throws(() => parseRgbStatus(status + 'RACOOL_CLI v=1 type=rgb_status brightness=1\n'));
});
test('RGB worker parser rejects invalid channel values', () => {
    assert.throws(() => parseRgbStatus(status.replace('target_r=255', 'target_r=256')));
});

test('RGB worker parser rejects unsupported versions and unterminated records', () => {
    assert.throws(() => parseRgbStatus(status.replace('v=1', 'v=2')));
    assert.throws(() => parseRgbStatus(status.trimEnd()));
});
