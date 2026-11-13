import assert from 'node:assert/strict';
import test from 'node:test';
import {parseFanEvidence} from './diagnostic/fan_mode_evidence.mjs';
function status(mode, on, gpio) {
    const modes={'OBJECT_SENSITIVE':0,'MANUAL ON':1,'MANUAL OFF':2,'AMBIENT_DELTA':3};
    return `RACOOL_CLI v=1 type=climate_status mode=${modes[mode]} on=${on ? 1 : 0} stale=0 debouncing=0 ir_tick=123 object_c=22 ambient_c=21 cpu_c=27 open_c=30 close_c=28 object_cal_c=0 ambient_cal_c=0 enabled=1 manual=${mode.startsWith('MANUAL') ? 1 : 0} manual_state=${mode==='MANUAL ON' ? 1 : 0} strategy=${mode==='AMBIENT_DELTA' ? 1 : 0} object_source=0 ambient_source=0 delta_on=20 delta_off=10\nRACOOL_CLI v=1 type=relay_status state=${on ? 1 : 0} mode=0 pin=7 level=${gpio}\n`;
}
test('manual and sensing fan modes require worker/relay/real GPIO consistency', () => {
    assert.equal(parseFanEvidence(status('MANUAL ON', true, 1), 'Sürekli Açık', true).on, true);
    assert.equal(parseFanEvidence(status('MANUAL OFF', false, 0), 'Kapalı', true).on, false);
    for (const on of [false, true])
        assert.equal(parseFanEvidence(status('OBJECT_SENSITIVE', on, Number(!on)), 'Nesneye duyarlı', false).on, on);
});
test('missing, mismatched and unactuated requests fail', () => {
    for (const text of ['', status('OBJECT_SENSITIVE', true, 1), status('MANUAL ON', false, 0), status('MANUAL ON', true, 0),
        status('MANUAL ON', true, 1).replace('type=relay_status state=1','type=relay_status state=0')])
        assert.throws(() => parseFanEvidence(text, 'Sürekli Açık', true));
});
test('later incomplete state cannot be replaced by older output', () => {
    assert.throws(() => parseFanEvidence(status('MANUAL ON',true,1)+'RACOOL_CLI v=1 type=climate_status\n', 'Sürekli Açık', true));
});
