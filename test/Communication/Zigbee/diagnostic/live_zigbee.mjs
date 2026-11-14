import {beginHealthCapture} from './health_capture.mjs';
import {normalizeDeviceIEEE, selectRaCoolDevice} from './device_selection.mjs';
import {fanSourceReadRequest,fanSourceReadResponse,fanSourceObservationDuration} from './fan_source_wire.mjs';
import {beginCpuCapture} from '../../../Hardware/InternalTemperatureSensor/diagnostic/calibration_capture.mjs';
import {colorRequest,colorResponse} from './color_command.mjs';
import {zoneColorRequest,zoneColorResponse,rangeResponse,normalizedZoneColor} from './zone_color.mjs';
import {beginRamCapture} from './trace_ram_capture.mjs';
import {builtinBrightnessPercentToRaw, observeBuiltinBrightness, observeBuiltinMode} from '../../../Hardware/BuiltinLED/diagnostic/builtin_brightness_evidence.mjs';
import {observePowerProfile} from '../../../Management/PowerManager/diagnostic/power_profile_evidence.mjs';
import {observeTxProfile} from './tx_profile_evidence.mjs';
import {observeFanMode} from '../../../Controllers/ClimateController/diagnostic/fan_mode_evidence.mjs';
import {observeRgbBrightness,observeRgbWorker} from '../../../Hardware/RGBLED/diagnostic/rgb_brightness_evidence.mjs';
import {isUnrelatedDeviceRouteError} from './bridge_error_scope.mjs';
import {captureLogCursor, readNewLog, selectAttributeReports} from './reporting_traffic.mjs';
import {validateReportingReadback} from './reporting_readback.mjs';
import {buildReportingChangePlan, requestedChangeValue, measuredReadResponses, validateChangedMeasurement} from './reporting_changes.mjs';
import fs from 'node:fs';
import {bridgeRequest} from './bridge_request.mjs';
import {settleBeforeSnapshot} from './live_operation_completion.mjs';
import {reportIntervalRequest,reportIntervalResponse,reportIntervalPair} from './reporting_interval_wire.mjs';
import {recoverReportingInterval,recoverBridgeLogLevel,bridgeLogLevels} from './recovery_session.mjs';
import {selectVerificationExposes, selectReadExposes} from './verification_selection.mjs';
import {MqttStateEvidence} from './mqtt_state_evidence.mjs';
const stateEvidence = new MqttStateEvidence();
import path from 'node:path';
import {createHash} from 'node:crypto';
import net from 'node:net';
import {EventEmitter} from 'node:events';
import {setTimeout as delay} from 'node:timers/promises';

// Credentials stay in memory and are never included in logs or artifacts.
const localPath = process.env.RACOOL_LOCAL_CONFIG ?? '.racool-local.json';
const resultId=process.env.RACOOL_RESULT_ID;
if(resultId!==undefined && !/^[0-9a-f]{32}$/.test(resultId)) throw Error('Invalid result identifier');
const local = fs.existsSync(localPath) ? JSON.parse(fs.readFileSync(localPath, 'utf8')) : {};
const deviceSelector = normalizeDeviceIEEE(process.env.RACOOL_DEVICE_IEEE ?? local.deviceIEEE);
const configurationPath = process.env.RACOOL_ZIGBEE_CONFIG ?? local.zigbeeConfiguration;
const liveConverter = process.env.RACOOL_LIVE_CONVERTER ?? local.liveConverter;
const host = process.env.RACOOL_MQTT_HOST ?? local.mqttHost;
const port = Number(process.env.RACOOL_MQTT_PORT ?? local.mqttPort ?? 1883);
if (!configurationPath || !liveConverter || !host || !Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error('Configure Zigbee configuration path, live converter path and MQTT host/port through RACOOL_* variables or the untracked local JSON file.');
}
const configuration = fs.readFileSync(configurationPath, 'utf8');
function scalar(key) {
    const raw = configuration.match(new RegExp('^  ' + key + ':\\s*(.+)$', 'm'))?.[1]?.trim();
    if (!raw) return undefined;
    if (raw.startsWith('"')) return JSON.parse(raw);
    if (raw.startsWith("'")) return raw.slice(1, -1).replaceAll("''", "'");
    return raw;
}
const base = scalar('base_topic') ?? 'zigbee2mqtt';
function str(value) {const body=Buffer.from(value);const size=Buffer.alloc(2);size.writeUInt16BE(body.length);return Buffer.concat([size,body]);}
function packet(header, body) {let n=body.length;const size=[];do {let x=n%128;n=Math.floor(n/128);if(n)x|=128;size.push(x);}while(n);return Buffer.concat([Buffer.from([header,...size]),body]);}
const events = new EventEmitter();
const socket = net.createConnection({host,port});
const transportAbort = new AbortController();
let buffer=Buffer.alloc(0), id=1;
function publish(topic, value) {
    if (socket.destroyed || !socket.writable) throw Error('MQTT connection closed; delivery may be unknown');
    socket.write(packet(0x30,Buffer.concat([str(topic),Buffer.from(JSON.stringify(value))])));
}
function subscribe(topic) {const identifier=Buffer.alloc(2);identifier.writeUInt16BE(id++);socket.write(packet(0x82,Buffer.concat([identifier,str(topic),Buffer.from([0])])));}
let timer, mqttPing;
socket.on('connect',()=>{let flags=2;const fields=[str('racool-audit-'+Date.now())];const user=scalar('user'),password=scalar('password');if(user){flags|=128;fields.push(str(user));}if(password){flags|=64;fields.push(str(password));}socket.write(packet(0x10,Buffer.concat([str('MQTT'),Buffer.from([4,flags,0,30]),...fields])));});
socket.on('error',error=>{console.error('MQTT connection:',error.code);process.exitCode=1;});
socket.on('data',chunk=>{buffer=Buffer.concat([buffer,chunk]);while(buffer.length>1){let length=0,multiplier=1,offset=1,byte;do{if(offset>=buffer.length)return;byte=buffer[offset++];length+=(byte&127)*multiplier;multiplier*=128;}while(byte&128);if(buffer.length<offset+length)return;const type=buffer[0]>>4,flags=buffer[0];const body=buffer.subarray(offset,offset+length);buffer=buffer.subarray(offset+length);if(type===2){if(body[1]){console.error('MQTT authentication rejected:',body[1]);socket.end();process.exitCode=1;return;}events.emit('ready');}else if(type===3){const size=body.readUInt16BE();const topic=body.subarray(2,2+size).toString();let begin=2+size;if((flags>>1&3)>0){const ack=body.subarray(begin,begin+2);begin+=2;socket.write(packet(0x40,ack));}try{events.emit('message',topic,JSON.parse(body.subarray(begin).toString()),!!(flags&1));}catch(error){if(error instanceof SyntaxError)continue;throw error;}}}});
const mode=process.argv[2]??'inspect';
const longVerificationModes = ['verify', 'verify-color', 'read-color', 'verify-zone', 'verify-fan', 'read-fan-sources', 'verify-fan-sources', 'observe-fan-sources', 'read-interval-wire'];
if(mode==='inspect')console.log('CONVERTER_FILES',JSON.stringify(fs.readdirSync(path.dirname(liveConverter))));
const seen={};
// Retained discovery can arrive before bridge/devices. Keep it in memory until
// the target identity is known; never persist unrelated device definitions.
const discoveryInbox = new Map();
function collectDiscovery(topic, value) {
    if (seen.device && JSON.stringify(value).includes(seen.device.ieee_address.replace(/^0x/, ''))) {
        seen.discovery ??= {}; seen.discovery[topic] = value;
    }
}
let started=false, activeOperation=null, operationSettled=false, recoveryJournal=null, bridgeLogJournal=null;
function trackOperation(operation) {
    operationSettled=false;
    activeOperation=operation.finally(()=>{operationSettled=true;});
}
events.on('ready',()=>{mqttPing=setInterval(()=>socket.write(Buffer.from([0xc0,0])),20000);subscribe(base+'/bridge/definitions');subscribe(base+'/bridge/info');subscribe(base+'/bridge/devices');subscribe(base+'/bridge/response/#');subscribe(base+'/bridge/logging');subscribe('homeassistant/#');timer=setTimeout(()=>socket.end(),longVerificationModes.includes(mode)?900000:(mode==='configure'||mode==='interview'||mode==='read'||mode==='read-ranges'||mode==='read-zone')?90000:15000);});
events.on('message',(topic,value,retained)=>{
    if(topic===base+'/bridge/definitions') seen.actions=value.actions;
    if(topic===base+'/bridge/info'){
        seen.logLevel=value.config?.advanced?.log_level;
        seen.info={version:value.version,commit:value.commit,frontendPort:value.config?.frontend?.port};
        const observed={sequence:(seen.bridgeInfoSequence??0)+1,level:seen.logLevel,
            version:value.version,retained,raw:value};
        seen.bridgeInfoSequence=observed.sequence;
        seen.bridgeInfo=observed;
        // Keep only minimal correlation evidence; never write bridge config/secrets.
        seen.bridgeInfoEvents??=[];
        seen.bridgeInfoEvents.push({sequence:observed.sequence,level:observed.level,
            version:observed.version,retained:observed.retained});
        if(seen.bridgeInfoEvents.length>24)seen.bridgeInfoEvents.shift();
        events.emit('bridge_info',observed);
        console.log('BRIDGE',JSON.stringify(seen.info));
    }
    if(topic===base+'/bridge/devices') {
        let d;
        try { d = selectRaCoolDevice(value, deviceSelector); }
        catch (error) {
            seen.selectionError = error.message;
            process.exitCode = 1;
            console.error('DEVICE_SELECTION_FAILED', error.message);
            socket.destroy();
            return;
        }
        if(d){
        if(!started && mode==='deploy') {
            started=true;
            const source=fs.readFileSync('tools/racool.mjs','utf8');
            const remote=liveConverter;
            if(fs.existsSync(remote))fs.copyFileSync(remote,'test/build/racool-before-deploy-'+Date.now()+'.mjs');
            publish(base+'/bridge/request/converter/save',{name:'racool.mjs',code:source});
        }
        if(!started && mode==='restart') {
            started=true;
            socket.write(packet(0x30,str(base+'/bridge/request/restart')));
        }
        if(!started && mode==='interview') {
            started=true;
            trackOperation(bridgeRequest(events,publish,base,'device/interview',{id:d.ieee_address},85000)
                .then(result=>{seen.interview=result;console.log('INTERVIEW_OK',JSON.stringify(result));})
                .catch(error=>{seen.interviewError=error.message;process.exitCode=1;console.error('INTERVIEW_FAILED',error.message);})
                .finally(()=>socket.end()));
        }
        if(!started && mode==='configure') {
            started=true;
            trackOperation(bridgeRequest(events,publish,base,'device/configure',{id:d.ieee_address},85000)
                .then(result=>{
                    if (result.data?.id!==d.ieee_address || seen.errors?.length) throw Error('Unmatched configure device or bridge error');
                    seen.configure=result;seen.completed=true;
                    console.log('CONFIGURE_OK',JSON.stringify(result));
                })
                .catch(error=>{seen.configureError=error.message;process.exitCode=1;console.error('CONFIGURE_FAILED',error.message);})
                .finally(()=>socket.end()));
        }
        if(!started && mode==='rediscover') {
            started=true;
            trackOperation(bridgeRequest(events,publish,base,'device/options',{id:d.ieee_address,options:{}},12000)
                .then(result=>{
                    if (result.data?.id!==d.ieee_address ||
                        JSON.stringify(result.data?.from)!==JSON.stringify(result.data?.to)) {
                        throw Error('Rediscovery changed options or returned another device');
                    }
                    seen.completed=true;
                    console.log('REDISCOVER_OK');
                })
                .catch(error=>{seen.rediscoverError=error.message;process.exitCode=1;console.error('REDISCOVER_FAILED',error.message);}));
        }
        seen.device=d;
        if((mode==='read-bridge-log-level'||mode==='set-bridge-log-level') && !started) {
            started=true;
            try {
                const permitted=bridgeLogLevels(seen.bridgeInfo?.raw);
                if(typeof seen.info?.version!=='string' || !permitted.includes(seen.logLevel))
                    throw Error('Missing validated bridge version/log baseline');
                if(mode==='read-bridge-log-level') {
                    // Retained bridge/info is a broker snapshot, not fresh
                    // evidence that the live service applied a mutation.
                    seen.bridgeLogLevel={value:seen.logLevel,retained:seen.bridgeInfo.retained,
                        version:seen.info.version,allowedLevels:permitted};
                    seen.completed=true;socket.end();
                } else {
                    const desired=process.argv[3], expected=process.argv[4];
                    if(!permitted.includes(desired)||!permitted.includes(expected)||seen.logLevel!==expected)
                        throw Error('Foreign or invalid bridge log level');
                    const sequence=seen.bridgeInfoSequence;
                    trackOperation(bridgeRequest(events,publish,base,'options',
                        {options:{advanced:{log_level:desired}}}).then(async reply=>{
                        if(reply?.data?.restart_required===true)
                            throw Error('Bridge restart required; no live restoration verification');
                        await waitBridgeLogUpdate(sequence,desired,seen.info.version);
                        seen.bridgeLogLevel={value:desired,expected,version:seen.info.version,
                            verifiedLive:true};
                        seen.completed=true;
                    }).catch(error=>{
                        seen.operationError=error.message;process.exitCode=1;
                    }).finally(()=>socket.end()));
                }
            } catch(error) {
                seen.operationError=error.message;process.exitCode=1;socket.end();
            }
            return; // Recovery child modes need no device-state subscription.
        }
        if(mode==='read-interval-wire' && !started) {
            started=true;
            trackOperation(readIntervalWire(process.argv[3]).then(value=>{
                seen.intervalWire={control:process.argv[3],value};seen.completed=true;
            }).catch(error=>{seen.operationError=error.message;seen.completed=false;process.exitCode=1;})
              .finally(()=>socket.end()));
        }
        for (const [topic, value] of discoveryInbox) collectDiscovery(topic, value);
        discoveryInbox.clear();
        console.log('DEVICE',JSON.stringify({name:d.friendly_name,ieee:d.ieee_address,interview:d.interview_state,exposes:d.definition?.exposes?.length}));subscribe(base+'/'+d.friendly_name);
        if(mode==='set' && !started) {
            started=true;
            const candidates=d.definition.exposes.flatMap(e=>[e,...(e.features??[])]);
            const exp=candidates.find(e=>e.property===process.argv[3]) ?? candidates.find(e=>e.name===process.argv[3]);
            if(!exp || !(exp.access&2))throw Error('Unknown writable property');
            let requested=process.argv[4];
            try {requested=JSON.parse(requested);} catch {}
            if (/^(cold|warm|hot|danger|freeze)_region_color_xy_thermal_indicator$/.test(exp.property) &&
                Number.isInteger(requested?.x) && Number.isInteger(requested?.y)) requested=normalizedZoneColor(requested);
            const root=base+'/'+d.friendly_name+(exp.endpoint?'/'+exp.endpoint:'');
            seen.command={property:exp.property,value:requested,readable:!!(exp.access&4) && exp.name!=='system_command'};
            console.log('SUBMITTED',JSON.stringify(seen.command));
            publish(root+'/set',{[exp.property]:requested});
            if(seen.command.readable)setTimeout(()=>{try {seen.command.readSequence=stateEvidence.sequence;publish(root+'/get',{[exp.property]:''});} catch(error) {seen.command.readError=error.message;process.exitCode=1;}},2000);
        }
        if(mode==='verify'||mode==='read'||mode==='verify-fan')publish(base+'/'+d.friendly_name+'/get',{temperature_climate:''});}
    }
    if(topic.startsWith('homeassistant/')) {
        if (seen.device) collectDiscovery(topic, value);
        else discoveryInbox.set(topic, value);
    }
    if(topic===base+'/'+seen.device?.friendly_name){seen.state=value;stateEvidence.observe(value,retained);if(mode==='inspect')console.log('STATE',JSON.stringify(value));
        /* Recovery child SET can finish as soon as a fresh post-GET state
         * confirms the requested value. Interactive SET keeps its old window. */
        if(mode==='set' && resultId && seen.command?.readable &&
            Number.isInteger(seen.command.readSequence)) {
            const evidence=stateEvidence.matchAfter(seen.command.readSequence,
                seen.command.property,seen.command.value);
            if(evidence) {seen.command.evidence=evidence;seen.command.actual=evidence.actual;socket.end();}
        }
        if([...longVerificationModes,'read','read-ranges','read-zone'].includes(mode)&&!started){started=true;trackOperation((['read-fan-sources','verify-fan-sources','observe-fan-sources'].includes(mode) ? runFanSourceChecks() : mode==='verify-fan' ? runFanChecks() : mode==='read-zone' ? runZoneRead() : mode==='read-ranges' ? runRangeRead() : mode==='verify-zone' ? runZoneChecks() : mode==='read-color'||mode==='verify-color' ? runColorChecks() : runChecks()).catch(e=>{seen.operationError=e.message;seen.completed=false;console.error('VERIFY_FAILED',e.message);process.exitCode=1;}).finally(()=>socket.end()));}}
    if(topic.includes('/bridge/response/')){console.log('RESPONSE',JSON.stringify(value));seen.responses??=[];seen.responses.push({topic,...value});if(value.status==='error')process.exitCode=1;}
    if(topic.endsWith('/bridge/logging') && value.level==='error'){
        if(isUnrelatedDeviceRouteError(value.message,seen.device)){
            console.log('UNRELATED_DEVICE_ERROR',value.message);
            seen.unrelatedDeviceErrors??=[];seen.unrelatedDeviceErrors.push(value.message);
        }else{console.log('BRIDGE_ERROR',value.message);seen.errors??=[];seen.errors.push(value.message);}
    }
});
socket.on('close',()=>{
    events.emit('transport_closed');
    stateEvidence.close();
    transportAbort.abort();
    clearTimeout(timer);clearInterval(mqttPing);
    if(activeOperation && !operationSettled) {seen.transportClosedBeforeCompletion=true;process.exitCode=1;}
    settleBeforeSnapshot(activeOperation,seen,async()=>{
    if(seen.transportClosedBeforeCompletion && recoveryJournal) {
        if(recoveryJournal.uncertain) seen.recovery={status:'delivery_unknown'};
        else if(recoveryJournal.confirmed!==recoveryJournal.baseline) {
            try {seen.recovery=await recoverReportingInterval(recoveryJournal,seen.device);}
            catch(error) {seen.recovery={status:'failed',error:error.message};}
        }
    }
    if(bridgeLogJournal && (bridgeLogJournal.uncertain ||
        bridgeLogJournal.confirmed!==bridgeLogJournal.baseline)) {
        try {seen.bridgeLogRecovery=await recoverBridgeLogLevel(bridgeLogJournal,seen.device);}
        catch(error) {seen.bridgeLogRecovery={status:'failed',error:error.message};}
        if(!['already_restored','restored'].includes(seen.bridgeLogRecovery?.status)) process.exitCode=1;
    }
    if(mode==='set' && seen.command?.readable) {
        seen.command.actual=seen.state?.[seen.command.property];
        seen.command.evidence=stateEvidence.matchAfter(seen.command.readSequence,seen.command.property,seen.command.value);
        if(!seen.command.evidence)process.exitCode=1;
        console.log('READBACK',JSON.stringify(seen.command));
    }
    if(mode==='deploy') {
        const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
        seen.converterHashes={local:hash('tools/racool.mjs'),live:hash(liveConverter)};
        if(seen.converterHashes.local!==seen.converterHashes.live)process.exitCode=1;
        console.log('CONVERTER_HASHES',JSON.stringify(seen.converterHashes));
    }
    if(seen.operationError)process.exitCode=1;if(mode==='configure'&&!seen.configure)process.exitCode=1;if(mode==='rediscover'&&!seen.completed)process.exitCode=1;if(!seen.device || (mode==='interview'&&!seen.interview) || (['read-bridge-log-level','set-bridge-log-level',...longVerificationModes,'read','read-ranges','read-zone'].includes(mode)&&!seen.completed))process.exitCode=1;fs.writeFileSync('test/build/zigbee-live-'+mode+(resultId?'-'+resultId:mode==='set'?'-'+Date.now():'')+'.json',JSON.stringify(seen,null,2));console.log('DISCOVERY_COUNT',Object.keys(seen.discovery??{}).length);
    }).catch(error=>{console.error('SNAPSHOT_FAILED',error.message);process.exitCode=1;});
});
setTimeout(()=>{if(!socket.destroyed)socket.destroy();},longVerificationModes.includes(mode)?905000:['configure','interview','read','read-ranges','read-zone'].includes(mode)?95000:20000).unref();

const pause=ms=>delay(ms,undefined,{signal:transportAbort.signal});
async function readIntervalWire(control) {
    const reply=await bridgeRequest(events,publish,base,'action',reportIntervalRequest(seen.device,control),30000);
    return reportIntervalResponse(reply.data,seen.device,control);
}
function waitBridgeLogUpdate(afterSequence,level,version) {
    return new Promise((resolve,reject)=>{
        const acceptable=event=>event.sequence>afterSequence && !event.retained &&
            event.level===level && event.version===version;
        const prior=seen.bridgeInfoEvents?.find(acceptable);
        if(prior){resolve(prior);return;}
        let completed=false;
        const done=(error,value)=>{
            if(completed)return;
            completed=true;clearTimeout(timer);
            events.off('bridge_info',onInfo);
            transportAbort.signal.removeEventListener('abort',onAbort);
            if(error)reject(error);else resolve(value);
        };
        const onInfo=event=>{if(acceptable(event))done(null,event);};
        const onAbort=()=>done(Error('Bridge transport closed before live info verification'));
        const timer=setTimeout(()=>done(Error('No non-retained bridge/info change confirmed')),6000);
        events.on('bridge_info',onInfo);
        transportAbort.signal.addEventListener('abort',onAbort,{once:true});
    });
}
function beginBridgeLogJournal(baseline) {
    const permitted=bridgeLogLevels(seen.bridgeInfo?.raw);
    if(!permitted.includes(baseline) || typeof seen.info?.version!=='string')
        throw Error('Missing validated bridge log baseline/version');
    if(bridgeLogJournal) throw Error('Bridge log mutation already owned');
    bridgeLogJournal={baseline,confirmed:baseline,allowedLevels:permitted,
        bridgeVersion:seen.info.version,requested:null,acknowledged:false,
        verified:false,phase:'baseline_snapshot',uncertain:false};
}
async function applyBridgeLogLevel(level) {
    if(!bridgeLogJournal || !bridgeLogJournal.allowedLevels.includes(level))
        throw Error('Invalid bridge log mutation for this live schema');
    if(bridgeLogJournal.uncertain)
        throw Error('Unknown bridge log delivery; no blind write');
    if(bridgeLogJournal.confirmed===level) return;
    if(socket.destroyed || !socket.writable) throw Error('Bridge connection closed before log change');
    const sequence=seen.bridgeInfoSequence;
    bridgeLogJournal.requested=level;
    bridgeLogJournal.phase='requested';
    bridgeLogJournal.acknowledged=false;
    bridgeLogJournal.verified=false;
    bridgeLogJournal.uncertain=true;
    const reply=await bridgeRequest(events,publish,base,'options',
        {options:{advanced:{log_level:level}}});
    bridgeLogJournal.acknowledged=true;
    bridgeLogJournal.phase='acknowledged';
    if(reply?.data?.restart_required===true)
        throw Error('Bridge log-level change requires restart');
    await waitBridgeLogUpdate(sequence,level,bridgeLogJournal.bridgeVersion);
    bridgeLogJournal.confirmed=level;
    bridgeLogJournal.verified=true;
    bridgeLogJournal.phase='verified';
    bridgeLogJournal.uncertain=false;
}
async function runFanSourceChecks() {
    const read=async attributes=>{
        const reply=await bridgeRequest(events,publish,base,'action',fanSourceReadRequest(seen.device,attributes),30000);
        const values=fanSourceReadResponse(reply.data,seen.device,attributes);
        seen.fanSourceReads??=[];seen.fanSourceReads.push({values,reply});return values;
    };
    const attributes=Array.from({length:18},(_,index)=>0x8050+index),initial={};
    for(let offset=0;offset<attributes.length;offset+=6)Object.assign(initial,await read(attributes.slice(offset,offset+6)));
    seen.fanSourceInitial=initial;
    if(mode==='observe-fan-sources') {
        const observationMs=fanSourceObservationDuration(process.env.RACOOL_FAN_SOURCE_OBSERVATION_MS);
        const originalLevel=seen.logLevel;
        const originalErrors=seen.errors?.length??0;
        beginBridgeLogJournal(originalLevel);
        try {
            await applyBridgeLogLevel('debug');
            const cursor=await captureLogCursor(configurationPath);
            await read([0x8058]);const start=seen.fanSourceReads.at(-1).reply.transaction;
            await pause(observationMs);
            await read([0x8058]);const end=seen.fanSourceReads.at(-1).reply.transaction;
            const window=await readNewLog(cursor,start,end);
            const reports=selectAttributeReports(window,seen.device.friendly_name,1,'hvacUserInterfaceCfg');
            seen.fanSourceTraffic={start,end,durationMs:observationMs,reports};
            const measured=[0x8058,0x8059,0x805a];
            if(measured.some(id=>!reports.some(line=>line.includes('"'+id+'":'))))
                throw Error('Missing unsolicited fan decision temperature/difference reports');
            console.log('FAN_SOURCE_TRAFFIC',JSON.stringify({durationMs:observationMs,reports:reports.length,attributes:measured}));
        } finally {
            await applyBridgeLogLevel(originalLevel);
        }
        if((seen.errors?.length??0)!==originalErrors) throw Error('Bridge error during fan source observation/restoration');
        seen.completed=true;
        return;
    }
    if(mode==='read-fan-sources'){seen.completed=true;console.log('FAN_SOURCES_WIRE',JSON.stringify(initial));return;}
    // External writes are harmless only with both internal selections and an
    // object-sensitive strategy; refuse someone else's populated RAM slots.
    if(initial[0x8050]!==0||initial[0x8051]!==0||initial[0x8052]!==0||
        [0x8055,0x8056,0x8057].some(id=>initial[id]!==-32768)||
        [0x805f,0x8060,0x8061].some(id=>initial[id]!==0))throw Error('Refusing foreign fan source configuration or slot ownership');
    const exposes=seen.device.definition.exposes;
    const control=name=>{const exp=exposes.find(e=>e.name===name);if(!exp||(exp.access&6)!==6)throw Error('Missing fan source control');return exp;};
    const on=control('fan_on_temperature_difference'),off=control('fan_off_temperature_difference');
    const slots=[1,2,3].map(slot=>control(`external_temperature_${slot}`));
    fs.writeFileSync('test/build/fan-source-wire-baseline.json',JSON.stringify(initial,null,2));
    const pair=async (nextOn,nextOff)=>{
        const current=await read([0x8053,0x8054]);
        if(nextOn*10<=current[0x8054]){await setAndRead(off,nextOff);await setAndRead(on,nextOn);}
        else {await setAndRead(on,nextOn);await setAndRead(off,nextOff);}
    };
    const errors=seen.errors?.length??0;let passed=false;
    try {
        await pair(-1,-2);
        const values=[20.1,-5.2,25.5];
        for(let index=0;index<slots.length;++index){
            await setAndRead(slots[index],values[index]);
            const raw=await read([0x8055+index]);
            if(raw[0x8055+index]!==Math.round(values[index]*100))throw Error('External MQTT injection wire mismatch');
        }
        if((seen.errors?.length??0)!==errors)throw Error('Bridge error during fan source acceptance');
        passed=true;
    } finally {
        const failures=[];
        for(const slot of slots)try{await setAndRead(slot,null);}catch(error){failures.push(error.message);}
        try{await pair(initial[0x8053]/10,initial[0x8054]/10);}catch(error){failures.push(error.message);}
        const restored=await read([0x8053,0x8054,0x8055,0x8056,0x8057]);
        const states=await read([0x805f,0x8060,0x8061]);
        if(Object.entries(restored).some(([id,value])=>value!==initial[id])||Object.values(states).some(value=>value!==0))failures.push('Fan source restoration wire mismatch');
        seen.fanSourceRestoration={restored,states,failures};
        if(failures.length)throw Error('Fan source restoration failed: '+failures.join('; '));
    }
    if((seen.errors?.length??0)!==errors)throw Error('Bridge error during fan source restoration');
    seen.completed=passed;
}
async function runFanChecks() {
    if (!process.env.RACOOL_SERIAL_PORT) throw Error('Fan acceptance requires the real console before any writes');
    const exposes = seen.device.definition.exposes;
    const modeControl = exposes.find(e => e.name === 'fan_operating_mode');
    const interval = exposes.find(e => e.name === 'report_interval_radiator_s');
    const output = exposes.find(e => e.name === 'fan_relay_state');
    if (!modeControl || !interval || !output || output.access !== 5) throw Error('Missing read-only fan contract');
    const originals = new Map();
    for (const control of [modeControl, interval]) {
        const sequence = stateEvidence.sequence;
        publish(base+'/'+seen.device.friendly_name+'/get', {[control.property]: ''});
        const fresh = await stateEvidence.waitAfter(sequence, control.property);
        if (!fresh) throw Error('No fresh fan baseline; no writes started');
        originals.set(control, fresh.actual);
    }
    seen.fanBaseline = Object.fromEntries([...originals].map(([e, v]) => [e.property, v]));
    fs.writeFileSync('test/build/fan-before-live-tests.json', JSON.stringify(seen.fanBaseline, null, 2));
    const originalLevel = seen.logLevel, initialErrors = seen.errors?.length ?? 0;
    beginBridgeLogJournal(originalLevel);
    const reporting = () => bridgeRequest(events,publish,base,'device/reporting/read',
        {id:seen.device.ieee_address,endpoint:1,cluster:'genOnOff',configs:[{attribute:'onOff'}]});
    const readWire = async expected => {
        const request = zoneColorRequest(seen.device, 0);
        request.params.dst_endpoint = 1;
        request.params.zcl.payload = [{attrId: 0}];
        const reply = await bridgeRequest(events,publish,base,'action',request,30000);
        const data = reply.data, bytes = Buffer.from(data?.data?.data ?? []);
        if (data?.clusterID !== 6 || data.endpoint !== 1 || data.address !== seen.device.network_address ||
            data.data?.type !== 'Buffer' || bytes.length !== 8 || (bytes[0]&15) !== 8 || bytes[2] !== 1 ||
            bytes.readUInt16LE(3) !== 0 || bytes[5] !== 0 || bytes[6] !== 0x10 || bytes[7] !== Number(expected))
            throw Error('Fan wire response does not match the applied state');
        return reply;
    };
    try {
        await applyBridgeLogLevel('debug');
        await setAndRead(interval, 0);
        await setAndRead(modeControl, 'Sürekli Açık');
        seen.fanEdges = [];
        for (const [requested, expected] of [['Kapalı', false], ['Sürekli Açık', true], ['Kapalı', false]]) {
            const cursor = await captureLogCursor(configurationPath), start = await reporting();
            const sequence = stateEvidence.sequence, submittedAt = Date.now();
            publish(base+'/'+seen.device.friendly_name+'/set', {[modeControl.property]: requested});
            // No GET precedes this observation: an unsolicited state must arrive.
            const event = await stateEvidence.waitAfter(sequence, output.property, expected ? 'ON' : 'OFF');
            if (!event) throw Error('No unsolicited fan state after command');
            const worker = await observeFanMode(process.env.RACOOL_SERIAL_PORT, requested);
            const wire = await readWire(expected), end = await reporting();
            const lines = selectAttributeReports(await readNewLog(cursor,start.transaction,end.transaction),
                seen.device.friendly_name,1,'genOnOff');
            if (!lines.some(line => line.includes('"onOff":'+Number(expected)))) throw Error('No actual OnOff attribute report in correlated window');
            seen.fanEdges.push({requested, expected, submittedAt, unsolicitedState: event, worker, wire, reports: lines});
            console.log('FAN_EDGE_OK', JSON.stringify({requested, expected, reports:lines.length}));
        }
        if ((seen.errors?.length ?? 0) !== initialErrors) throw Error('Bridge error during fan acceptance');
    } finally {
        const failures = [];
        for (const control of [modeControl, interval]) try {await setAndRead(control, originals.get(control));}
        catch (error) {failures.push(error.message);}
        try {await applyBridgeLogLevel(originalLevel);}
        catch (error) {failures.push(error.message);}
        seen.fanRestorationErrors = failures;
        if (failures.length) throw Error('Fan restoration failed: '+failures.join('; '));
    }
    seen.completed = true;
}
async function setAndRead(exp,value) {
    const root=base+'/'+seen.device.friendly_name+(exp.endpoint?'/'+exp.endpoint:'');
    const errors=seen.errors?.length??0;
    const tracked=recoveryJournal && exp.name===recoveryJournal.control;
    if(tracked && !socket.destroyed && socket.writable) recoveryJournal.uncertain=true;
    publish(root+'/set',{[exp.property]:value});
    await pause(1800);
    const readSequence=stateEvidence.sequence;
    publish(root+'/get',{[exp.property]:''});
    const evidence=await stateEvidence.waitAfter(readSequence,exp.property,value);
    const actual=evidence?.actual ?? seen.state[exp.property];
    if(!evidence || (seen.errors?.length??0)!==errors)throw Error(exp.property+' has no matching fresh MQTT observation after GET; expected '+value+', got '+actual);
    if(tracked) {
        const wire=await readIntervalWire(exp.name);
        if(wire!==value) throw Error('Reporting interval ZCL readback mismatch');
        recoveryJournal.confirmed=value;
        recoveryJournal.uncertain=false;
    }
    seen.checks??=[];seen.checks.push({property:exp.property,value,actual,evidence});
    console.log('MATCHED_MQTT_STATE',exp.property,JSON.stringify(actual));
    const fanAttributes={fan_operating_mode:0x8050,object_temperature_source:0x8051,
        ambient_temperature_source:0x8052,fan_on_temperature_difference:0x8053,fan_off_temperature_difference:0x8054};
    if (Object.hasOwn(fanAttributes,exp.name)) {
        const attribute=fanAttributes[exp.name];
        const expected=attribute===0x8050 ? {'Nesneye duyarlı':0,'Sürekli Açık':1,'Kapalı':2,'Ortama duyarlı':3}[value] :
            attribute<=0x8052 ? {'Dahili':0,'Harici 1':1,'Harici 2':2,'Harici 3':3}[value] : Math.round(value*10);
        const reply=await bridgeRequest(events,publish,base,'action',fanSourceReadRequest(seen.device,[attribute]),30000);
        const raw=fanSourceReadResponse(reply.data,seen.device,[attribute]);
        if(raw[attribute]!==expected) throw Error('Fan source correlated wire readback mismatch');
        seen.fanSourceWireChecks??=[];seen.fanSourceWireChecks.push({property:exp.property,expected,raw,reply});
        console.log('MATCHED_FAN_SOURCE_WIRE',exp.property,raw[attribute]);
    }
    if (exp.name === 'processor_power_profile' && process.env.RACOOL_SERIAL_PORT) {
        const observation = await observePowerProfile(process.env.RACOOL_SERIAL_PORT, value);
        seen.powerProfileObservations ??= [];
        seen.powerProfileObservations.push(observation);
        console.log('MATCHED_APPLIED_PROFILE', observation.actual);
    }
    if (exp.name === 'radio_tx_profile' && process.env.RACOOL_SERIAL_PORT) {
        const observation = await observeTxProfile(process.env.RACOOL_SERIAL_PORT, value);
        seen.txProfileObservations ??= [];
        seen.txProfileObservations.push(observation);
        console.log('MATCHED_SDK_TX_PROFILE', observation.actual);
    }
    if (exp.name === 'fan_operating_mode' && process.env.RACOOL_SERIAL_PORT) {
        const observation = await observeFanMode(process.env.RACOOL_SERIAL_PORT, value);
        seen.fanModeObservations ??= [];
        seen.fanModeObservations.push(observation);
        console.log('MATCHED_FAN_WORKER_GPIO', observation.actual);
    }
    if (exp.name === 'brightness' && process.env.RACOOL_SERIAL_PORT) {
        const worker = await observeRgbBrightness(process.env.RACOOL_SERIAL_PORT, value);
        seen.rgbBrightnessObservations ??= [];
        seen.rgbBrightnessObservations.push(worker);
        console.log('MATCHED_RGB_WORKER_BRIGHTNESS', JSON.stringify(worker.actual));
    }
    if (exp.name === 'status_led_brightness' && process.env.RACOOL_SERIAL_PORT) {
        const worker = await observeBuiltinBrightness(process.env.RACOOL_SERIAL_PORT,builtinBrightnessPercentToRaw(value));
        seen.brightnessWorkerObservations ??= [];
        seen.brightnessWorkerObservations.push(worker);
        console.log('MATCHED_WORKER_BRIGHTNESS',worker.actual);
    }
    if (exp.name === 'status_led_mode' && process.env.RACOOL_SERIAL_PORT) {
        const worker = await observeBuiltinMode(process.env.RACOOL_SERIAL_PORT, value);
        seen.modeWorkerObservations ??= [];
        seen.modeWorkerObservations.push(worker);
        console.log('MATCHED_WORKER_MODE', JSON.stringify(worker.actual));
    }
}
async function runChecks() {
    if (mode === 'read' && process.argv.length <= 3) throw Error('Read mode requires explicit control selection');
    const controls = (mode === "read" ? selectReadExposes : selectVerificationExposes)(seen.device.definition.exposes, process.argv.slice(3));
    if (!controls.length) throw Error("No controls selected for verification");
    const observeChanges = process.env.RACOOL_REPORT_CHANGES === '1';
    if (observeChanges && (mode !== 'verify' || process.env.RACOOL_REPORT_TRAFFIC !== '1'))
        throw Error('Controlled report changes require verify mode and traffic observation');
    const changePlans = observeChanges ? buildReportingChangePlan(seen.device.definition.exposes, controls) : [];
    if (changePlans.some(plan => ['trace_ram','cpu_calibration','real_service_health'].includes(plan.kind)) && !process.env.RACOOL_SERIAL_PORT)
        throw Error('RAM/CPU change verification requires a serial port before any writes');
    const expectedReads = mode === 'read' && process.env.RACOOL_READ_EXPECTED
        ? JSON.parse(process.env.RACOOL_READ_EXPECTED) : undefined;
    if (expectedReads !== undefined && (!expectedReads || typeof expectedReads !== 'object' || Array.isArray(expectedReads) ||
        Object.keys(expectedReads).length !== controls.length ||
        controls.some(exp => !Object.hasOwn(expectedReads, exp.property))))
        throw Error('Expected read values must exactly match the selected controls');
    seen.selectedControls = controls.map(e => e.property);
    // Fail before writes if any initial value cannot be refreshed.
    seen.initialReads = [];
    const initialErrors = seen.errors?.length ?? 0;
    const initialControls = [...controls, ...changePlans.filter(plan => plan.trigger).map(plan => plan.trigger)];
    for (const exp of initialControls) {
        const root=base+'/'+seen.device.friendly_name+(exp.endpoint?'/'+exp.endpoint:'');
        const sequence = stateEvidence.sequence;
        publish(root+'/get',{[exp.property]:''});
        const evidence = await stateEvidence.waitAfter(sequence, exp.property, expectedReads?.[exp.property]);
        const value = evidence?.actual;
        if (value === undefined || !evidence || (seen.errors?.length ?? 0) !== initialErrors)
            throw Error("Initial value could not be refreshed; no test writes started: " + exp.property);
        seen.initialReads.push({property: exp.property, value, evidence});
    }
    if (mode === 'read') {
        seen.completed = true;
        console.log('READ_CONTROLS', JSON.stringify(seen.initialReads));
        return;
    }
    const observeTraffic = process.env.RACOOL_REPORT_TRAFFIC === '1';
    const originalLogLevel = seen.logLevel;
    if (observeTraffic) seen.reportingOriginalLogLevel = originalLogLevel;
    if (observeTraffic) beginBridgeLogJournal(originalLogLevel);
    const original=Object.fromEntries(seen.initialReads.map(read=>[read.property,read.value]));
    if(controls.length===1 && controls[0].name.startsWith('report_interval_') && !observeTraffic && !observeChanges &&
        process.env.RACOOL_REPORT_RECONFIGURE!=='1') {
        const exp=controls[0];
        reportIntervalPair(exp.name,exp.property);
        const baseline=await readIntervalWire(exp.name);
        if(baseline!==original[exp.property]) throw Error('Initial reporting interval ZCL readback mismatch');
        recoveryJournal={control:exp.name,property:exp.property,baseline,confirmed:baseline,uncertain:false};
    }
    fs.writeFileSync('test/build/zigbee-before-tests.json',JSON.stringify(original,null,2));
    const ping=setInterval(()=>socket.write(Buffer.from([0xc0,0])),20000);
    try {
        if (observeTraffic) await applyBridgeLogLevel('debug');
        for(const exp of controls) {
            if(!(exp.access&2)||exp.name==='system_command'||!['enum','numeric'].includes(exp.type))continue;
            const value=original[exp.property];
            if(value===undefined)throw Error('Missing initial read for '+exp.property);
            try {
                if (exp.name.startsWith('report_interval_')) {
                    const kinds = ['radiator','ambient','processor','health','memory','uptime'];
                    const index = kinds.findIndex(kind => exp.name === 'report_interval_' + kind + '_s');
                    if (index < 0) throw Error('Unknown reporting control: ' + exp.name);
                    const endpoint = [1,4,5,6,7,8][index];
                    const cluster = index < 3 ? 'msTemperatureMeasurement' : 'genAnalogInput';
                    for (const interval of [0,1]) {
                        await setAndRead(exp, interval);
                        if (interval === 0 && process.env.RACOOL_REPORT_RECONFIGURE === '1') {
                            const configured = await bridgeRequest(events,publish,base,'device/configure',
                                {id:seen.device.ieee_address},90000);
                            seen.reportingReconfigurations ??= [];
                            seen.reportingReconfigurations.push({endpoint,...configured});
                        }
                        const reply = await bridgeRequest(events,publish,base,'device/reporting/read',
                            {id:seen.device.ieee_address,endpoint,cluster,configs:[{attribute:index<3?'measuredValue':'presentValue'}]});
                        seen.reportingReadbacks ??= [];
                        seen.reportingReadbacks.push({property:exp.property,interval,endpoint,...reply});
                        console.log('REPORTING_READBACK',JSON.stringify({endpoint,interval,...reply}));
                        validateReportingReadback(reply.data, endpoint, cluster, interval);
                        if (observeTraffic) {
                            await pause(2000);
                            const cursor = await captureLogCursor(configurationPath);
                            const readReporting = () => bridgeRequest(events,publish,base,'device/reporting/read',
                                {id:seen.device.ieee_address,endpoint,cluster,configs:[{attribute:index<3?'measuredValue':'presentValue'}]});
                            const startMarker = await readReporting();
                            const startedAt = Date.now();
                            const changePlan = changePlans.find(plan => plan.reportProperty === exp.property);
                            let change;
                            if (changePlan) {
                                const initial = changePlan.trigger ? original[changePlan.trigger.property] : undefined;
                                const requested = changePlan.trigger ? requestedChangeValue(changePlan, initial) : undefined;
                                const readMeasurement = async () => {
                                    const measurement = changePlan.measurement;
                                    const sequence = stateEvidence.sequence;
                                    const root = base+'/'+seen.device.friendly_name+(measurement.endpoint ? '/'+measurement.endpoint : '');
                                    publish(root+'/get', {[measurement.property]:''});
                                    const evidence = await stateEvidence.waitAfter(sequence, measurement.property, undefined, 30000);
                                    if (!evidence || !Number.isFinite(evidence.actual)) throw Error('No fresh measurement observation');
                                    return evidence;
                                };
                                let ramCapture, cpuCapture, healthCapture;
                                try {
                                    if (changePlan.kind === 'trace_ram') await pause(20000);
                                    const before = await readMeasurement();
                                    if (changePlan.kind === 'trace_ram') ramCapture = await beginRamCapture(process.env.RACOOL_SERIAL_PORT);
                                    if (changePlan.kind === 'cpu_calibration') cpuCapture = await beginCpuCapture(process.env.RACOOL_SERIAL_PORT);
                                    if (changePlan.kind === 'real_service_health') healthCapture = await beginHealthCapture(process.env.RACOOL_SERIAL_PORT);
                                    if (changePlan.trigger) await setAndRead(changePlan.trigger, requested);
                                    await pause(['elapsed','trace_ram','cpu_calibration','real_service_health'].includes(changePlan.kind) ? 20000 : 2000);
                                    const after = await readMeasurement();
                                    change = {endpoint,interval,trigger:changePlan.trigger?.property ?? changePlan.kind,initial,requested,before,after};
                                    await pause(15000);
                                } finally {
                                    if (healthCapture) {
                                        const capture = await healthCapture.finish();
                                        if (change) change.healthCapture = capture;
                                    }
                                    if (cpuCapture) {
                                        const capture = await cpuCapture.finish();
                                        if (change) change.cpuCapture = capture;
                                    }
                                    if (ramCapture) {
                                        const capture = await ramCapture.finish();
                                        if (change) change.ramCapture = capture;
                                    }
                                    if (changePlan.trigger) await setAndRead(changePlan.trigger, initial);
                                }
                            } else await pause(15000);
                            const endMarker = await readReporting();
                            await pause(1000);
                            const window = await readNewLog(cursor,startMarker.transaction,endMarker.transaction);
                            const reports = selectAttributeReports(window,seen.device.friendly_name,endpoint,cluster);
                            if (change) {
                                const values = measuredReadResponses(window,seen.device.friendly_name,endpoint,cluster,index < 3 ? 'measuredValue' : 'presentValue');
                                validateChangedMeasurement(values, changePlan?.kind === 'trace_ram' ? 5 : 0);
                                seen.reportingChanges ??= [];
                                seen.reportingChanges.push({...change, zclReadValues:values,restored:true});
                                console.log('REPORTING_CONTROLLED_CHANGE', JSON.stringify({endpoint,interval,values}));
                            }
                            const observation = {endpoint,interval,startedAt,finishedAt:Date.now(),startTransaction:startMarker.transaction,endTransaction:endMarker.transaction,reports};
                            seen.reportingTraffic ??= [];
                            seen.reportingTraffic.push(observation);
                            console.log('REPORTING_TRAFFIC',JSON.stringify({endpoint,interval,count:reports.length}));
                            if (interval === 0 ? reports.length !== 0 : reports.length === 0)
                                throw Error('Unexpected unsolicited report count for endpoint ' + endpoint);
                        }
                    }
                } else if(exp.name === 'processor_power_profile' || exp.name === 'radio_tx_profile' || exp.name === 'fan_operating_mode' || exp.name === 'thermal_led_startup_animation' || (exp.name.startsWith('status_led_')&&exp.name.includes('animation')) || exp.name.includes('color_preset')) {
                    for(const animation of exp.values)await setAndRead(exp,animation);
                } else if(exp.type==='numeric') {
                    const step=exp.value_step??0.1;
                    const candidate=exp.name.includes('upper_bound') ? value+step : value-step;
                    const opposite=exp.name.includes('upper_bound') ? value-step : value+step;
                    const next=Number((candidate>=exp.value_min && candidate<=exp.value_max ? candidate : opposite).toFixed(2));
                    await setAndRead(exp,next);
                } else {
                    const values=['fan_operating_mode','object_temperature_source','ambient_temperature_source'].includes(exp.name) ? exp.values.filter(v=>v!==value) : [exp.values.find(v=>v!==value)].filter(v=>v!==undefined);
                    for(const next of values)await setAndRead(exp,next);
                }
            } finally {await setAndRead(exp,value);}
        }
        if ((seen.errors?.length ?? 0) !== initialErrors) throw Error('Bridge reported errors during verification');
    } finally {
        clearInterval(ping);
        if (observeTraffic) await applyBridgeLogLevel(originalLogLevel);
    }
    seen.completed=true;
}

async function runColorChecks() {
    for (let i=0;i<100 && !seen.actions;++i) await pause(100);
    if (!Array.isArray(seen.actions) || !seen.actions.includes('raw')) throw Error('Bridge does not advertise its raw action');
    const request = coordinates => bridgeRequest(events,publish,base,'action',colorRequest(seen.device,coordinates),30000);
    const read = async () => {
        const reply=await request();
        const coordinates=colorResponse(reply.data,seen.device,'read');
        return {...reply,coordinates};
    };
    const initial=await read(); seen.colorInitial=initial;
    if (mode==='read-color') {seen.completed=true;console.log('COLOR_READ',JSON.stringify(initial.coordinates));return;}
    colorRequest(seen.device,initial.coordinates); // Ensure restoration is valid before the test write.
    if (!process.env.RACOOL_SERIAL_PORT) throw Error('Color actuation requires serial worker evidence');
    const exp=seen.device.definition.exposes.find(e=>e.name==='thermal_led_mode');
    if (!exp || (exp.access&6)!==6) throw Error('Missing readable/writable LED mode');
    let sequence=stateEvidence.sequence;
    const root=base+'/'+seen.device.friendly_name+(exp.endpoint?'/'+exp.endpoint:'');
    publish(root+'/get',{[exp.property]:''});
    const evidence=await stateEvidence.waitAfter(sequence,exp.property,undefined,30000);
    if (!evidence || evidence.actual!=='Otomatik') throw Error('Color test requires the existing automatic LED selection');
    const original=evidence.actual;
    const before=await observeRgbWorker(process.env.RACOOL_SERIAL_PORT);seen.colorWorkerBefore=before;
    if (before.activeAnimation!==0 || before.actual.mode!==2) throw Error('Refusing to interrupt a notification or manual LED selection');
    const errors=seen.errors?.length ?? 0;
    try {
        const reply=await request({x:45875,y:19661});
        colorResponse(reply.data,seen.device,'moveToColor'); seen.colorCommand=reply;
        await pause(1000);
        const readback=await read();seen.colorReadback=readback;
        if (readback.coordinates.x!==45875 || readback.coordinates.y!==19661) throw Error('XY ZCL readback mismatch');
        const worker=await observeRgbWorker(process.env.RACOOL_SERIAL_PORT);seen.colorWorker=worker;
        if (worker.actual.mode!==0 || worker.actual.color[0]!==255 || worker.actual.color[1]>2 || worker.actual.color[2]>2 ||
            worker.actual.brightness!==before.actual.brightness) throw Error('Color command did not reach the production RGB worker');
        if ((seen.errors?.length ?? 0)!==errors) throw Error('Bridge error during color test');
        seen.colorApplied=true;
    } finally {
        try {
            const restored=await request(initial.coordinates);
            colorResponse(restored.data,seen.device,'moveToColor');
            const readback=await read();
            if (readback.coordinates.x!==initial.coordinates.x || readback.coordinates.y!==initial.coordinates.y)
                throw Error('Initial XY attributes were not restored');
            seen.colorCoordinatesRestored=readback;
        } finally {
            await setAndRead(exp,original);
            const worker=await observeRgbWorker(process.env.RACOOL_SERIAL_PORT);
            if (worker.actual.mode!==2 || worker.actual.brightness!==before.actual.brightness)
                throw Error('Automatic LED mode or brightness restoration failed');
            seen.colorWorkerRestored=worker;
        }
    }
    seen.completed=seen.colorApplied && Boolean(seen.colorCoordinatesRestored) && Boolean(seen.colorWorkerRestored);
}

async function runZoneChecks() {
    for (let i=0;i<100 && !seen.actions;++i) await pause(100);
    if (!seen.actions?.includes('raw')) throw Error('Bridge raw action unavailable');
    const exposes=seen.device.definition.exposes.filter(exp=>/^(cold|warm|hot|danger|freeze)_region_color_xy_thermal_indicator$/.test(exp.property));
    if (exposes.length!==5) throw Error('Five free zone controls are not active');
    const names=['cold','warm','hot','danger','freeze'];
    const read=async zone=>{
        const reply=await bridgeRequest(events,publish,base,'action',zoneColorRequest(seen.device,zone),30000);
        return {...reply,value:zoneColorResponse(reply.data,seen.device,zone)};
    };
    const initial=[];
    for (let zone=0;zone<5;++zone) initial.push(await read(zone));
    seen.zoneOriginal=initial;seen.zoneChecks=[];seen.zoneRestoration=[];
    const errors=seen.errors?.length??0;
    let passed=false;
    try {
        for (let zone=0;zone<5;++zone) {
            const exp=exposes.find(exp=>exp.property===`${names[zone]}_region_color_xy_thermal_indicator`);
            if (!exp.property.endsWith('_thermal_indicator')||exp.access!==7) throw Error('Wrong zone route/access');
            const value={x:12000+zone*1000,y:21000+zone*1000};
            publish(base+'/'+seen.device.friendly_name+'/set',{[exp.property]:normalizedZoneColor(value)});
            await pause(1800);
            const actual=await read(zone);
            if (actual.value.x!==value.x||actual.value.y!==value.y) throw Error('Zone wire readback mismatch');
            seen.zoneChecks.push({property:exp.property,requested:value,actual});
        }
        if ((seen.errors?.length??0)!==errors) throw Error('Bridge error during zone writes');
        passed=true;
    } finally {
        const failures=[];
        for (let zone=0;zone<5;++zone) {
            try {
                const exp=exposes.find(exp=>exp.property===`${names[zone]}_region_color_xy_thermal_indicator`);
                publish(base+'/'+seen.device.friendly_name+'/set',{[exp.property]:normalizedZoneColor(initial[zone].value)});
                await pause(1800);
                const actual=await read(zone);
                if (actual.value.x!==initial[zone].value.x||actual.value.y!==initial[zone].value.y) throw Error('Zone restoration mismatch');
                seen.zoneRestoration.push(actual);
            } catch (error) {failures.push(error.message);}
        }
        if (failures.length) throw Error('Zone restoration failed: '+failures.join('; '));
    }
    if ((seen.errors?.length??0)!==errors) throw Error('Bridge error during zone restoration');
    seen.completed=passed&&seen.zoneRestoration.length===5;
    console.log('ZONE_XY_VERIFIED',seen.zoneChecks.length,'RESTORED',seen.zoneRestoration.length);
}

async function runRangeRead() {
    for (let i=0;i<100 && !seen.actions;++i) await pause(100);
    if (!seen.actions?.includes('raw')) throw Error('Bridge raw action unavailable');
    const request=zoneColorRequest(seen.device,0);
    request.params.zcl.payload=Array.from({length:6},(_,index)=>({attrId:0x8020+index}));
    const reply=await bridgeRequest(events,publish,base,'action',request,30000);
    seen.rangeReadback={...reply,ranges:rangeResponse(reply.data,seen.device)};
    seen.completed=true;console.log('RANGE_WIRE_READ',JSON.stringify(seen.rangeReadback.ranges));
}

async function runZoneRead() {
    if (process.argv.length!==4) throw Error('Read-zone requires exactly one zone');
    const zone=['cold','warm','hot','danger','freeze'].indexOf(process.argv[3]);
    const request=zoneColorRequest(seen.device,zone);
    for (let i=0;i<100 && !seen.actions;++i) await pause(100);
    if (!seen.actions?.includes('raw')) throw Error('Bridge raw action unavailable');
    const reply=await bridgeRequest(events,publish,base,'action',request,30000);
    seen.zoneReadback={...reply,value:zoneColorResponse(reply.data,seen.device,zone)};
    seen.completed=true;console.log('ZONE_WIRE_READ',process.argv[3],JSON.stringify(seen.zoneReadback.value));
}
