import {spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';

export function validateCpuCapture(result) {
    if (!result?.completed || !result.restoration_verified ||
        !Number.isFinite(result.original_offset) || !Number.isFinite(result.requested_offset) ||
        result.original_offset === result.requested_offset)
        throw Error('CPU calibration capture did not complete and restore its original value');
    return result;
}

// The child is the sole serial owner and restores calibration in finally.
export async function beginCpuCapture(port) {
    if (!port) throw Error('CPU calibration verification requires a serial port');
    const python = process.env.RACOOL_PYTHON ?? path.join('.venv', process.platform === 'win32' ? 'Scripts/python.exe' : 'bin/python');
    const output = path.join('test','build',`reporting-cpu-${Date.now()}`);
    const child = spawn(python,['test/Hardware/InternalTemperatureSensor/diagnostic/capture_calibration_change.py',
        '--port',port,'--output',output],{stdio:['ignore','pipe','pipe']});
    let stdout='', stderr='', resolveReady, rejectReady;
    const ready = new Promise((resolve,reject)=>{resolveReady=resolve;rejectReady=reject;});
    const timer = setTimeout(()=>rejectReady(Error('CPU calibration readiness timed out')),45000);
    const finished = new Promise((resolve,reject)=>{
        child.once('error',error=>{rejectReady(error);reject(error);});
        child.once('close',async code=>{
            clearTimeout(timer);
            rejectReady(Error('CPU calibration exited before readiness'));
            try {
                if (code !== 0) throw Error(`CPU calibration exited ${code}: ${stderr.slice(-300)}`);
                const result=validateCpuCapture(JSON.parse(await fs.readFile(output+'.json','utf8')));
                resolve({...result,artifact:output+'.json'});
            } catch(error) {reject(error);}
        });
    });
    finished.catch(()=>{});
    child.stdout.on('data',chunk=>{
        stdout+=chunk.toString();
        if(stdout.includes('CPU_CALIBRATION_READY')) {clearTimeout(timer);resolveReady();}
    });
    child.stderr.on('data',chunk=>{stderr+=chunk.toString();});
    try {await ready;} catch(error) {await finished.catch(()=>{});throw error;}
    return {finish:()=>finished};
}
