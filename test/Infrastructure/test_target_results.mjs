import test from 'node:test';
import assert from 'node:assert/strict';
import {checkTargetResults, decodeTargetLog} from '../check_target_results.mjs';
const suites=[{name:'hardware',cases:['test_first','test_second']}];
test('suite success cannot hide a missed early case',()=>{
    assert.throws(()=>checkTargetResults('suite [PASSED]\nfile:2: test_second [PASSED]',suites),/test_first/);
});
test('all named cases must pass without an error',()=>{
    const log='file:1: test_first [PASSED]\nfile:2: test_second [PASSED]';
    assert.equal(checkTargetResults(log,suites),2);
    assert.throws(()=>checkTargetResults(log+'\nsuite [ERRORED]',suites));
});
test('similar case prefixes cannot substitute for a missing case',()=>{
    assert.throws(()=>checkTargetResults('file:1: test_first_extra [PASSED]\nfile:2: test_second [PASSED]',suites));
});

test('PowerShell UTF-16LE and UTF-8 logs preserve named results',()=>{
    const message='file.c:10: test_one [PASSED]\r\n';
    const suites=[{name:'sample',cases:['test_one']}];
    for(const bytes of [Buffer.from(message),Buffer.from('\uFEFF'+message),
        Buffer.concat([Buffer.from([0xff,0xfe]),Buffer.from(message,'utf16le')])]) {
        assert.equal(checkTargetResults(decodeTargetLog(bytes),suites),1);
    }
});


test('visible PlatformIO verbosity preserves raw named Unity acceptance',()=>{
    const log='test/test_main.c:10:test_first:PASS\ntest/test_main.c:11:test_second:PASS';
    assert.equal(checkTargetResults(log,suites),2);
    assert.throws(()=>checkTargetResults('test/test_main.c:10:test_first_extra:PASS\ntest/test_main.c:11:test_second:PASS',suites));
});

test('raw Unity failure or ignore rejects an otherwise complete log',()=>{
    const log='file.c:1:test_first:PASS\nfile.c:2:test_second:PASS';
    for(const status of ['FAIL: failed operation','IGNORE: unavailable']) {
        assert.throws(()=>checkTargetResults(log+'\nfile.c:3:test_other:'+status,suites));
    }
});
