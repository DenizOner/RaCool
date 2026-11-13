import {spawn} from 'node:child_process';
import {randomBytes} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {recoverOwnedValue} from './recovery_decision.mjs';
import {reportIntervalPair} from './reporting_interval_wire.mjs';

const helper=fileURLToPath(new URL('./live_zigbee.mjs',import.meta.url));
async function runChild(mode,args,identity) {
    const id=randomBytes(16).toString('hex');
    const artifact=path.join('test','build',`zigbee-live-${mode}-${id}.json`);
    const env={...process.env,RACOOL_DEVICE_IEEE:identity,RACOOL_RESULT_ID:id};
    delete env.RACOOL_READ_EXPECTED;
    const child=spawn(process.execPath,[helper,mode,...args],{env,stdio:'ignore',windowsHide:true});
    const code=await new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>{child.kill();reject(Error('Recovery session timed out; delivery may be unknown'));},45000);
        child.once('error',error=>{clearTimeout(timer);reject(error);});
        child.once('close',result=>{clearTimeout(timer);resolve(result);});
    });
    if(code!==0) throw Error('Recovery session failed; delivery may be unknown; result: '+artifact);
    const result=JSON.parse(await fs.readFile(artifact,'utf8'));
    if(result.device?.ieee_address?.toLowerCase()!==identity.toLowerCase()) throw Error('Recovery device identity mismatch');
    return {result,artifact};
}

// Recovery is restricted to the one reporting interval whose value and
// successful mutation were both confirmed through correlated raw ZCL reads.
export async function recoverReportingInterval(journal,device) {
    if(!/^0x[0-9a-f]{16}$/i.test(device?.ieee_address)) throw Error('Unsupported recovery target');
    reportIntervalPair(journal?.control,journal?.property);
    const artifacts=[];
    const outcome=await recoverOwnedValue(journal,{
        read:async()=>{
            const {result,artifact}=await runChild('read-interval-wire',[journal.control],device.ieee_address);
            artifacts.push(artifact);
            if(!result.completed || result.intervalWire?.control!==journal.control ||
                !Number.isInteger(result.intervalWire?.value)) throw Error('Recovery ZCL read was not confirmed');
            return result.intervalWire.value;
        },
        write:async value=>{
            const {result,artifact}=await runChild('set',[journal.property,JSON.stringify(value)],device.ieee_address);
            artifacts.push(artifact);
            if(result.command?.property!==journal.property || result.command.value!==value ||
                result.command.evidence?.actual!==value) throw Error('Recovery write was not confirmed');
        }
    });
    return {...outcome,artifacts};
}


const BRIDGE_LEVELS = new Set(['debug','info','warn','error']);

// Bridge logging is global. Only a confirmed helper-owned change can be
// conditionally restored, and a new connection must read back its current level.
// This read/compare/write is not atomic against other bridge administrators.
export async function recoverBridgeLogLevel(journal,device) {
    if(!/^0x[0-9a-f]{16}$/i.test(device?.ieee_address)) throw Error('Unsupported recovery target');
    if(!BRIDGE_LEVELS.has(journal?.baseline) || !BRIDGE_LEVELS.has(journal?.confirmed) ||
        typeof journal.uncertain!=='boolean') throw Error('Invalid bridge log recovery evidence');
    if(journal.uncertain) return {status:'delivery_unknown'};
    if(journal.confirmed===journal.baseline) return {status:'already_restored'};
    const artifacts=[];
    const read=async()=>{
        const {result,artifact}=await runChild('read-bridge-log-level',[],device.ieee_address);
        artifacts.push(artifact);
        if(!result.completed || !BRIDGE_LEVELS.has(result.bridgeLogLevel?.value))
            throw Error('Bridge log level was not freshly read');
        return result.bridgeLogLevel.value;
    };
    const before=await read();
    if(before===journal.baseline) return {status:'already_restored',before,artifacts};
    if(before!==journal.confirmed) return {status:'foreign_change',before,artifacts};
    const {result,artifact}=await runChild('set-bridge-log-level',
        [journal.baseline,journal.confirmed],device.ieee_address);
    artifacts.push(artifact);
    if(!result.completed || result.bridgeLogLevel?.value!==journal.baseline ||
        result.bridgeLogLevel?.expected!==journal.confirmed)
        throw Error('Bridge log level restoration was not acknowledged');
    const after=await read();
    if(after!==journal.baseline) throw Error('Bridge log level restoration did not persist');
    return {status:'restored',before,after,artifacts};
}
