import {spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';

export function validateHealthCapture(result) {
    if (!result?.completed || !result.restoration_verified || result.initial_score !== 100 || result.changed_score !== 88)
        throw Error('Real health capture did not complete and restore the service');
    return result;
}

// The child is the sole serial owner and restores the real service in finally.
export async function beginHealthCapture(port) {
    if (!port) throw Error('Health change verification requires a serial port');
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts/python.exe' : 'bin/python');
    const output = path.join('test','build',`reporting-health-${Date.now()}`);
    const child = spawn(python,['test/Communication/SerialMonitor/diagnostic/capture_health_change.py',
        '--port',port,'--output',output],{stdio:['ignore','pipe','pipe']});
    let stdout='', stderr='', resolveReady, rejectReady;
    const ready = new Promise((resolve,reject)=>{resolveReady=resolve;rejectReady=reject;});
    const timer = setTimeout(()=>rejectReady(Error('Health change readiness timed out')),45000);
    const finished = new Promise((resolve,reject)=>{
        child.once('error',error=>{rejectReady(error);reject(error);});
        child.once('close',async code=>{
            clearTimeout(timer);
            rejectReady(Error('Health change exited before readiness'));
            try {
                const saved=JSON.parse(await fs.readFile(output+'.json','utf8'));
                if (code !== 0) throw Error(`Health change exited ${code}: ${saved.error ?? saved.restore_error ?? stderr.slice(-300)}`);
                const result=validateHealthCapture(saved);
                resolve({...result,artifact:output+'.json'});
            } catch(error) {reject(error);}
        });
    });
    finished.catch(()=>{});
    child.stdout.on('data',chunk=>{
        stdout+=chunk.toString();
        if(stdout.includes('HEALTH_CHANGE_READY')) {clearTimeout(timer);resolveReady();}
    });
    child.stderr.on('data',chunk=>{stderr+=chunk.toString();});
    try {await ready;} catch(error) {
        try {await finished;} catch(failure) {throw failure;}
        throw error;
    }
    return {finish:()=>finished};
}
