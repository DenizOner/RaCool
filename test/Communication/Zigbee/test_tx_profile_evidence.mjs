import assert from 'node:assert/strict';
import test from 'node:test';
import {parseTxProfile} from './diagnostic/tx_profile_evidence.mjs';

test('all TX presets require matching SDK dBm', () => {
    for (const [profile, dbm] of Object.entries({Eco: 0, Normal: 10, Boost: 20}))
        for (const newline of ['\n', '\r\n', '\r\r\n'])
            assert.deepEqual(parseTxProfile(`RACOOL_CLI v=1 type=zigbee_tx profile=${["Eco","Normal","Boost"].indexOf(profile)} dbm=${dbm}${newline}`), {profile, dbm});
});
test('missing, unknown, mismatch and incomplete TX responses fail', () => {
    for (const text of ['', 'RACOOL_CLI v=1 type=zigbee_tx profile=0 dbm=10\n',
        'Zigbee TX profile: Invalid; SDK power: 0 dBm\n', 'RACOOL_CLI v=1 type=zigbee_tx profile=2 dbm=20'])
        assert.throws(() => parseTxProfile(text));
});
test('an earlier status cannot hide a later invalid SDK result', () => {
    assert.throws(() => parseTxProfile('RACOOL_CLI v=1 type=zigbee_tx profile=1 dbm=10\nRACOOL_CLI v=1 type=zigbee_tx profile=2 dbm=10\n'));
});
