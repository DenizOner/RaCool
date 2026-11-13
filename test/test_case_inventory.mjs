// Static registration checks complement the actual Unity output acceptance.
function codeOnly(source) {
    return source.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\*[\s\S]*?\*\/|\/\/[^\n]*/g,
        match=>match.replace(/[^\r\n]/g,' '));
}
export function definedCases(source) {
    return [...codeOnly(source).matchAll(/^(?:static\s+)?void\s+(test_\w+)\s*\(\s*(?:void)?\s*\)\s*\{/gm)]
        .map(match=>match[1]).filter(name=>!name.endsWith('_All'));
}
export function registeredCases(source, suite) {
    const defined=definedCases(source);
    if(!defined.length) throw Error('No test cases in '+(suite.file??'source'));
    const called=suite.generateRunner ? (suite.tests??defined) :
        [...codeOnly(source).matchAll(/\bRUN_TEST\s*\(\s*(test_\w+)\s*\)/g)].map(match=>match[1]);
    const missing=defined.filter(name=>!called.includes(name));
    if(missing.length) throw Error('Unexecuted test cases: '+missing.join(', '));
    if(new Set(called).size!==called.length) throw Error('Duplicate test registration');
    return called;
}
export function checkNativeCaseResults(log, expected) {
    const passed=new Set([...log.matchAll(/:(test_\w+):PASS\b/g)].map(match=>match[1]));
    const missing=expected.filter(name=>!passed.has(name));
    if(missing.length) throw Error('Missing passing native cases: '+missing.join(', '));
    return expected.length;
}
