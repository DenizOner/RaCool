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
    // A recovery child never inherits test-only socket-cut or observation
    // flags from its parent: only the original test client may disconnect.
    for(const name of ['RACOOL_READ_EXPECTED','RACOOL_TEST_CUT_CLIENT_AFTER_WIRE',
        'RACOOL_TEST_CUT_CLIENT_AFTER_BRIDGE_LOG','RACOOL_REPORT_TRAFFIC',
        'RACOOL_REPORT_CHANGES','RACOOL_REPORT_RECONFIGURE']) delete env[name];
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


// The live bridge publishes its allowed options in bridge/info.config_schema.
// Do not guess whether an older bridge accepts 'warn' or the current 'warning'.
export function bridgeLogLevels(info) {
    const schema=info?.config_schema;
    const allowed=info?.allowedLevels ??
        schema?.properties?.advanced?.properties?.log_level?.enum ??
        schema?.properties?.log_level?.enum;
    if(!Array.isArray(allowed) || !allowed.includes('debug') ||
        !allowed.includes('info') || allowed.some(level=>typeof level!=='string'))
        throw Error('Missing authoritative bridge log-level schema; no writes');
    return allowed;
}

// Bridge logging is global. An ACK confirms admission, not application.
// A live (non-retained) bridge/info change and a separately reconnected
// snapshot are both required to report a verified restoration.
export async function recoverBridgeLogLevel(journal,device) {
    if(!/^0x[0-9a-f]{16}$/i.test(device?.ieee_address)) throw Error('Unsupported recovery target');
    if(!Array.isArray(journal?.allowedLevels) || !journal.allowedLevels.includes(journal.baseline) ||
        !journal.allowedLevels.includes(journal.confirmed) ||
        typeof journal.uncertain!=='boolean') throw Error('Invalid bridge log recovery evidence');
    if(journal.uncertain || journal.phase!=='verified' || !journal.verified)
        return {status:'delivery_unknown'};
    if(journal.confirmed===journal.baseline) return {status:'already_restored'};
    const artifacts=[];
    const read=async()=>{
        const {result,artifact}=await runChild('read-bridge-log-level',[],device.ieee_address);
        artifacts.push(artifact);
        const evidence=result.bridgeLogLevel;
        if(!result.completed || !journal.allowedLevels.includes(evidence?.value) ||
            evidence.version!==journal.bridgeVersion)
            throw Error('Bridge log-level snapshot/schema/version not verified');
        return evidence;
    };
    const before=await read();
    if(before.value===journal.baseline) {
        // A retained broker snapshot alone cannot prove live restoration.
        return {status:before.retained?'snapshot_only':'already_restored',before,artifacts};
    }
    if(before.value!==journal.confirmed) return {status:'foreign_change',before,artifacts};
    const {result,artifact}=await runChild('set-bridge-log-level',
        [journal.baseline,journal.confirmed],device.ieee_address);
    artifacts.push(artifact);
    if(!result.completed || result.bridgeLogLevel?.value!==journal.baseline ||
        result.bridgeLogLevel?.expected!==journal.confirmed ||
        result.bridgeLogLevel?.version!==journal.bridgeVersion ||
        result.bridgeLogLevel?.verifiedLive!==true)
        throw Error('Bridge log-level restoration has no correlated live update');
    const after=await read();
    if(after.value!==journal.baseline) throw Error('Independent bridge snapshot mismatched restoration');
    return {status:'restored',before,after,verifiedLive:true,artifacts,
        warning:'Read-compare-write is not atomic; external writers may race'};
}
