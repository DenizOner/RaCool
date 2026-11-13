import assert from 'node:assert/strict';
import test from 'node:test';
import {parsePowerProfile} from './diagnostic/power_profile_evidence.mjs';
const record = mode => `RACOOL_CLI v=1 type=power_status mode=${mode} locks=0 sleeps=0 wakeups=0 restarts=0 uptime_s=12`;
test('all applied profiles parse from complete machine records', () => {
    for (const [mode,name] of ['Performans','Dengeli','Tasarruf'].entries()) assert.equal(parsePowerProfile(record(mode)+'\n'),name);
});
test('human, unsupported version, unknown and malformed records are rejected', () => {
    for (const text of ['', 'Current Mode: (1)\n',record(-1),record(3),record(1).replace('v=1','v=2'),record(1).replace('locks=0','locks=invalid')]) assert.throws(()=>parsePowerProfile(text));
});
test('a newer unknown profile cannot be hidden by an older valid record',()=>{
    assert.throws(()=>parsePowerProfile(record(1)+'\n'+record(-1)+'\n'));
});
