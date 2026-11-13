import test from 'node:test';
import assert from 'node:assert/strict';
import {findBuildDiagnostics} from './build_diagnostics.mjs';
test('compiler warning is rejected even alongside successful compilation',()=>{
    assert.equal(findBuildDiagnostics('file.h:40:9: warning: macro redefined\nBUILD SUCCESS').length,1);
});
test('full output retains uppercase configuration and linker errors',()=>{
    const text='CMake Warning at CMakeLists.txt\r\nfile.c:1: ERROR: invalid type\r\nDeprecationWarning: old option';
    assert.equal(findBuildDiagnostics(text).length,3);
});
test('clean build and diagnostic flag names are accepted',()=>{
    assert.deepEqual(findBuildDiagnostics('gcc -Wall -Werror -o tests.exe\nBuild succeeded'),[]);
});
