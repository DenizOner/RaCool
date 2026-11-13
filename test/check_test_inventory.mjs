import {registeredCases, definedCases} from './test_case_inventory.mjs';
import {checkTestLayout} from './check_test_layout.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export function checkInventory(root) {
    checkTestLayout(root);
    const read=p=>JSON.parse(fs.readFileSync(path.join(root,p),'utf8').replace(/^\uFEFF/,''));
    const native=read('test/native_suites.json'), target=read('test/target_suites.json');
    const helpers=read('test/helper_suites.json'), live=read('test/live_suites.json');
    if (!helpers.length || !live.length) throw Error('Empty helper/live suite registration');
    const additional=[...helpers,...live];
    if(new Set(additional.map(s=>s.file)).size!==additional.length)
        throw Error('Duplicate helper/live file registration');
    for(const suite of additional) {
        if(!fs.existsSync(path.join(root,suite.file))) throw Error('Missing helper/live suite: '+suite.file);
        if(suite.runner && !['node','python','powershell'].includes(suite.runner))
            throw Error('Unknown helper runner: '+suite.runner);
    }
    const files=[...native,...target].map(s=>s.file);
    if(new Set(files).size!==files.length) throw Error('Duplicate test file registration');
    for(const suite of native) registeredCases(fs.readFileSync(path.join(root,suite.file),'utf8'),suite);
    for(const suite of target) {
        const expected=definedCases(fs.readFileSync(path.join(root,suite.file),'utf8'));
        if(!expected.length || !suite.cases || expected.some(name=>!suite.cases.includes(name)) || suite.cases.some(name=>!expected.includes(name)))
            throw Error('Target case manifest differs from source: '+suite.name);
    }
    const registrations=new Map([...native.map(s=>({...s,environment:'native'})),...target.map(s=>({...s,environment:'target'}))].map(s=>[s.file,s]));
    const walk=p=>fs.readdirSync(path.join(root,p),{withFileTypes:true}).flatMap(e=>e.isDirectory()
        ? (['build','mocks','support'].includes(e.name)?[]:walk(p+'/'+e.name)) : [p+'/'+e.name]);
    const rows=walk('test').filter(p=>p.endsWith('.c')).map(file=>{
        const source=fs.readFileSync(path.join(root,file),'utf8');
        const functions=[...source.matchAll(/^(?:static\s+)?void\s+(test_\w+)\s*\(\s*(?:void)?\s*\)/gm)]
            .map(m=>m[1]).filter(n=>!n.endsWith('_All'));
        const registration=registrations.get(file);
        return {file,functions,environment:registration?.environment??(functions.length?'unregistered':source.trim()?'support':'empty'),name:registration?.name};
    });
    const missing=rows.filter(r=>r.environment==='unregistered');
    const names=[...native,...target].map(s=>s.name);
    if(new Set(names).size!==names.length)throw Error('Duplicate suite name in manifests');
    for(const suite of [...native,...target])if(!fs.existsSync(path.join(root,suite.file)))throw Error('Missing suite: '+suite.file);
    const out=path.join(root,'test/build');fs.mkdirSync(out,{recursive:true});
    fs.writeFileSync(path.join(out,'test-coverage-inventory.json'),JSON.stringify(rows,null,2));
    if(missing.length)throw Error('Unregistered test files: '+missing.map(r=>r.file).join(', '));
    return {native:native.length,target:target.length,helpers:helpers.length,live:live.length,
        empty:rows.filter(r=>r.environment==='empty').length};
}
if(process.argv[1] && path.resolve(process.argv[1])===fileURLToPath(import.meta.url))
    console.log(JSON.stringify(checkInventory(path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'))));
