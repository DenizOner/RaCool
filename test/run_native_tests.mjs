import {registeredCases, checkNativeCaseResults} from './test_case_inventory.mjs';
import {checkInventory} from './check_test_inventory.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {findBuildDiagnostics} from './Infrastructure/build_diagnostics.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
checkInventory(root);
const output = process.env.RACOOL_NATIVE_OUTPUT
    ? path.resolve(root, process.env.RACOOL_NATIVE_OUTPUT)
    : path.join(root, 'test/build/native_manual');
fs.mkdirSync(output, {recursive: true});
const versionOutput=path.join(output,'product-version');
const versionPython=process.env.RACOOL_PYTHON??path.join(root,process.platform==='win32'?'.venv/Scripts/python.exe':'.venv/bin/python');
const versionRun=spawnSync(versionPython,[path.join(root,'tools/product_version.py'),'generate',
    '--manifest',path.join(root,'resources/product-version.json'),'--root',root,'--output',versionOutput],
    {cwd:root,encoding:'utf8',timeout:30000,windowsHide:true});
if(versionRun.error||versionRun.status!==0||(versionRun.stderr??'').trim())
    throw Error('Product version generation failed: '+(versionRun.error?.message??versionRun.stderr??''));
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'test/native_suites.json'), 'utf8').replace(/^\uFEFF/, ''));
const pattern = process.argv[2] ?? '';
const filter = new RegExp(pattern.split('*').map(x=>x.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')).join('.*'), 'i');
const selected = manifest.filter(s=>filter.test(s.name));
if (!selected.length) throw Error('No native suites match: ' + pattern);
const catalogOutput=path.join(output,'localization');
if(selected.some(suite=>suite.localization)) {
    const python=process.env.RACOOL_PYTHON??path.join(root,process.platform==='win32'?'.venv/Scripts/python.exe':'.venv/bin/python');
    const generated=spawnSync(python,[path.join(root,'tools/generate_translations.py'),path.join(root,'resources/translations'),catalogOutput],
        {cwd:root,encoding:'utf8',timeout:30000,windowsHide:true});
    if(generated.error||generated.status!==0||(generated.stderr??'').trim())
        throw Error('Native catalog generation failed: '+(generated.error?.message??generated.stderr??''));
}
const resolve = p=>path.join(root, p);
const includes = ['include','src','test','test/Infrastructure/mocks','test/Infrastructure/mocks/driver','test/Infrastructure/mocks/freertos',
    'test/Infrastructure/mocks/esp_common','test/Infrastructure/mocks/esp_rom','test/Infrastructure/support','.pio/libdeps/native/Unity/src'];
const flags = includes.flatMap(p=>['-I',resolve(p)]).concat([
    '-I',versionOutput,
    '-include',resolve('test/Infrastructure/support/test_mocks.h'),'-DRACOOL_TEST_NATIVE','-D_GNU_SOURCE',
    '-DCORE_DEBUG_LEVEL=5','-DUNITY_OUTPUT_FORMAT=UNITY_FORMAT_LEGACY','-g','-Wno-format-security',...(process.platform==='win32'?['-mconsole']:[]),'-lm']);
const results = [];
for (const suite of selected) {
    console.log('[BUILD] ' + suite.name);
    const exe = path.join(output, 'test_' + suite.name + '_native' + (process.platform==='win32'?'.exe':''));
    const inputs = [suite.file,...(suite.sources ?? [])].map(resolve);
    if(suite.localization) inputs.push(resolve('src/Core/Localization/Localization.c'),path.join(catalogOutput,'LocalizationCatalog.generated.c'));
    const expectedCases=registeredCases(fs.readFileSync(resolve(suite.file),'utf8'),suite);
    if (suite.generateRunner) {
        const content = fs.readFileSync(resolve(suite.file), 'utf8');
        const tests = expectedCases;
        if (!tests.length) throw Error('No test functions in ' + suite.file);
        let runner = '#include "unity.h"\n';
        for (const name of tests) runner += 'void ' + name + '(void);\n';
        for (const [name,callback] of [['setUp',suite.setup],['tearDown',suite.teardown]]) {
            if (!new RegExp('^void\\s+'+name+'\\s*\\(', 'm').test(content)) {
                if(callback) runner+='void '+callback+'(void);\n';
                runner += 'void '+name+'(void) {'+(callback?callback+'();':'')+'}\n';
            }
        }
        runner += 'int main(void) { UNITY_BEGIN();\n' + tests.map(n=>'RUN_TEST('+n+');').join('\n') + '\nreturn UNITY_END();}\n';
        const runnerPath=path.join(output, suite.name+'_runner.c');
        fs.writeFileSync(runnerPath,runner);inputs.push(runnerPath);
    }
    if (suite.centralMocks !== false) inputs.push(resolve('test/Infrastructure/support/test_mocks.c'));
    inputs.push(resolve('.pio/libdeps/native/Unity/src/unity.c'));
    const build = spawnSync(process.env.CC ?? 'gcc',['-o',exe,...inputs,...flags,...(suite.localization?['-I',catalogOutput]:[]),...(suite.flags??[])],
        {cwd:root,encoding:'utf8',maxBuffer:16*1024*1024,timeout:120000,windowsHide:true});
    let log=(build.stdout??'')+(build.stderr??'');
    let result={name:suite.name,status:'build_failed',tests:0,failures:0};
    const diagnostics=findBuildDiagnostics(log);
    if(diagnostics.length)result.compilerDiagnostics=diagnostics;
    if(build.status===0 && !diagnostics.length) {
        console.log('[RUN] '+suite.name);
        const run=spawnSync(exe,[],{cwd:root,encoding:'utf8',maxBuffer:16*1024*1024,timeout:suite.timeoutMs??30000,windowsHide:true});
        log+=(run.stdout??'')+(run.stderr??'');
        const counts=[...log.matchAll(/(\d+) Tests (\d+) Failures (\d+) Ignored/g)].at(-1);
        result={...result,status:run.status===0 && counts && +counts[1]>0 && +counts[2]===0 && +counts[3]===0?'passed':'failed',
            tests:counts?+counts[1]:0,failures:counts?+counts[2]:0,ignored:counts?+counts[3]:0,exitCode:run.status};
        if(run.error){result.error=run.error.code;log+='\n'+run.error.message;}
        try { checkNativeCaseResults(run.stdout??'',expectedCases); }
        catch(error) { result.status='failed'; result.error=error.message; log+='\n'+error.message; }
    } else if(build.error){result.error=build.error.code;log+='\n'+build.error.message;}
    const logPath=path.join(output,suite.name+'.log');fs.writeFileSync(logPath,log);
    results.push(result);
    console.log('['+result.status.toUpperCase()+'] '+suite.name+' ('+result.tests+' tests)');
    if(result.status!=='passed')console.log(log.slice(-5000));
}
const passed=results.filter(r=>r.status==='passed').length;
const summary={timestamp:new Date().toISOString(),selected:selected.length,passed,failed:results.length-passed,
    tests:results.reduce((n,r)=>n+r.tests,0),results};
fs.writeFileSync(path.join(output,pattern?'summary-filtered.json':'summary.json'),JSON.stringify(summary,null,2));
console.log('Results: '+summary.passed+' passed, '+summary.failed+' failed; '+summary.tests+' tests');
process.exitCode=summary.failed?1:0;
