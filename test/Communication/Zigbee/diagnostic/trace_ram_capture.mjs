import {spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';

export function validateRamCapture(result) {
    if (!result?.completed || !result.trace_disabled || result.summary?.dropped !== 0 ||
        !(result.summary?.samples > 0) || result.initial_trace?.enabled)
        throw Error('RAM trigger capture did not complete and release its owned trace');
    return {completed:true, traceDisabled:true, samples:result.summary.samples, rows:result.summary.rows};
}

// The child owns the serial port and uses its finally path to release capture RAM.
export async function beginRamCapture(port) {
    if (!port) throw Error('RAM change verification requires RACOOL_SERIAL_PORT');
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts/python.exe' : 'bin/python');
    const output = path.join('test','build',`reporting-ram-${Date.now()}`);
    const child = spawn(python,['test/Hardware/BuiltinLED/diagnostic/capture_builtin_timing.py',
        '--port',port,'--duration','80','--ready-marker','--output',output],{stdio:['ignore','pipe','pipe']});
    let stdout='', stderr='', readyResolve, readyReject;
    const ready = new Promise((resolve,reject)=>{readyResolve=resolve;readyReject=reject;});
    const finished = new Promise((resolve,reject)=>{
        child.once('error',error=>{readyReject(error);reject(error);});
        child.once('close',async code=>{
            readyReject(Error('RAM trigger exited before its ready marker'));
            try {
                if (code !== 0) throw Error(`RAM capture exited ${code}: ${stderr.slice(-300)}`);
                const result=JSON.parse(await fs.readFile(output+'.json','utf8'));
                resolve({...validateRamCapture(result),artifact:output+'.json'});
            } catch(error) {reject(error);}
        });
    });
    // Observe failure immediately while retaining a promise for finally cleanup.
    finished.catch(()=>{});
    child.stdout.on('data',chunk=>{stdout+=chunk.toString();if(stdout.includes('LEDTRACE_READY'))readyResolve();});
    child.stderr.on('data',chunk=>{stderr+=chunk.toString();});
    try {await ready;} catch(error) {await finished.catch(()=>{});throw error;}
    return {finish:()=>finished};
}
