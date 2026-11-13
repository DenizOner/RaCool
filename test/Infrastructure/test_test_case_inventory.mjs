import assert from 'node:assert/strict';
import test from 'node:test';
import {definedCases, registeredCases, checkNativeCaseResults} from '../test_case_inventory.mjs';
test('comments and diagnostic strings do not register cases',()=>{
    const source='void test_real(void) {} /* RUN_TEST(test_real); */\n// void test_fake(void) {}\nconst char *x="RUN_TEST(test_real)";';
    assert.deepEqual(definedCases(source),['test_real']);
    assert.throws(()=>registeredCases(source,{}),/test_real/);
});
test('explicit and generated runners cover every defined case',()=>{
    const source='void test_one(void) {}\nstatic void test_two(void) {}\nRUN_TEST(test_one); RUN_TEST(test_two);';
    assert.deepEqual(registeredCases(source,{}),['test_one','test_two']);
    assert.throws(()=>registeredCases(source,{generateRunner:true,tests:['test_one']}),/test_two/);
    assert.deepEqual(registeredCases(source,{generateRunner:true}),['test_one','test_two']);
});
test('duplicate results cannot replace a missing native case',()=>{
    assert.throws(()=>checkNativeCaseResults('a:1:test_one:PASS\na:2:test_one:PASS',['test_one','test_two']),/test_two/);
    assert.throws(()=>checkNativeCaseResults('a:1:test_one:IGNORE',['test_one']),/test_one/);
    assert.equal(checkNativeCaseResults('a:1:test_one:PASS\na:2:test_two:PASS',['test_one','test_two']),2);
});
test('zero cases and duplicate registration are rejected',()=>{
    assert.throws(()=>registeredCases('',{}),/No test cases/);
    assert.throws(()=>registeredCases('void test_one(void) {}\nRUN_TEST(test_one); RUN_TEST(test_one);',{}),/Duplicate/);
});
