import fs from 'node:fs';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
export function decodeTargetLog(bytes) {
    // Windows PowerShell 5 Tee-Object writes UTF-16LE; PowerShell 7 writes UTF-8.
    return bytes[0]===0xff && bytes[1]===0xfe ? bytes.subarray(2).toString('utf16le') :
        bytes.toString('utf8').replace(/^\uFEFF/,'');
}
export function checkTargetResults(log, suites) {
    const clean=log.replace(/\x1b\[[0-9;]*m/g,'');
    const missing=[];
    for(const suite of suites) {
        if(!suite.cases?.length) throw Error('No expected target cases for '+suite.name);
        for(const name of suite.cases) {
            if(!clean.split(/\r?\n/).some(line=>(line.match(/:\s+(test_\w+)\s+\[PASSED\]/)?.[1] ??
                line.match(/^.*:\d+:\s*(test_\w+):PASS\s*$/)?.[1])===name)) missing.push(name);
        }
    }
    if(/\[FAILED\]|\[ERRORED\]|:\d+:\s*test_\w+:(?:FAIL|IGNORE)(?::|\s|$)/m.test(clean) || missing.length) throw Error('Target results incomplete or failed; missing: '+missing.join(', '));
    return suites.reduce((sum,suite)=>sum+suite.cases.length,0);
}
if(process.argv[1] && path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
    const suites=JSON.parse(fs.readFileSync(new URL('./target_suites.json',import.meta.url)));
    console.log('All expected target cases observed:',checkTargetResults(decodeTargetLog(fs.readFileSync(process.argv[2])),suites));
}
