import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const helper=fileURLToPath(new URL('./diagnostic/live_zigbee.mjs',import.meta.url));

function field(text){const value=Buffer.from(text),length=Buffer.alloc(2);length.writeUInt16BE(value.length);return Buffer.concat([length,value]);}
function publish(socket,topic,value,retained=false){
    const body=Buffer.concat([field(topic),Buffer.from(JSON.stringify(value))]);let size=body.length,bytes=[];
    do{let b=size%128;size=Math.floor(size/128);bytes.push(b|(size?128:0));}while(size);
    socket.write(Buffer.concat([Buffer.from([retained?0x31:0x30,...bytes]),body]));
}
for (const scenario of ['confirmed','controlled_cut','foreign','uncertain','ack_only','retained_update',
    'restore_disconnect','post_restore_foreign','version_drift','missing_schema',
    'retained_baseline','already_debug','invalid_cut_flags',
    'retained_recovery','stale_retained_foreign','retained_restore_child',
    'retained_final','stale_retained_final_foreign',
    'normal_foreign','normal_stale_retained']) test(
    'bridge logging recovery after '+scenario+' MQTT disconnect', async()=>{
    const directory=await fs.mkdtemp(path.join(os.tmpdir(),'racool-log-recovery-'));
    const control='report_interval_radiator_s',property=control+'_climate';
    const device={friendly_name:'fixture',ieee_address:'0x0000000000000123',
        network_address:123,model_id:'RaCool-ESP32C6',definition:{exposes:[{
            name:control,property,type:'numeric',access:7,value_min:0,value_max:600,value_step:1}]}};
    let level=scenario==='already_debug'?'debug':'info',value=3,first=true,connections=0;
    const schema={properties:{advanced:{properties:{log_level:{
        enum:['debug','info','warning','error']}}}}};
    const bridgeInfo=()=>({version:scenario==='version_drift'&&connections>1?'2.1.0':'2.0.0',
        config:{advanced:{log_level:level}},
        ...(scenario==='missing_schema'?{}:{config_schema:schema})});
    const writes=[],clients=new Set();
    const server=net.createServer(socket=>{
        const initial=first;first=false;connections++;clients.add(socket);
        socket.on('close',()=>clients.delete(socket));
        socket.on('error',e=>{if(e.code!=='ECONNRESET') throw e;});
        let buffer=Buffer.alloc(0);
        socket.on('data',chunk=>{buffer=Buffer.concat([buffer,chunk]);while(buffer.length>1){
            let off=1,len=0,mul=1,b;
            do{if(off>=buffer.length)return;b=buffer[off++];len+=(b&127)*mul;mul*=128;}while(b&128);
            if(buffer.length<off+len)return;
            const type=buffer[0]>>4,body=buffer.subarray(off,off+len);buffer=buffer.subarray(off+len);
            if(type===1)socket.write(Buffer.from([0x20,2,0,0]));
            if(type===8){
                const n=body.readUInt16BE(2),topic=body.subarray(4,4+n).toString();
                if(topic==='zigbee2mqtt/bridge/info') {
                    if(scenario==='post_restore_foreign' && connections>=4) level='warning';
                    if(scenario==='stale_retained_final_foreign' && connections>=4) {
                        level='warning';
                        const stale={...bridgeInfo(),config:{advanced:{log_level:'info'}}};
                        publish(socket,topic,stale,true);
                    } else if((scenario==='stale_retained_foreign' && connections===2) ||
                        (scenario==='retained_restore_child' && connections===3)) {
                        // The real bridge changed externally, but the retained
                        // broker snapshot still advertises our old 'debug'.
                        level='warning';
                        const stale={...bridgeInfo(),config:{advanced:{log_level:'debug'}}};
                        publish(socket,topic,stale,true);
                    } else {
                        publish(socket,topic,bridgeInfo(),true);
                        // Fresh non-retained updates are available on the
                        // original connection and normal recovery read/write
                        // children, but never on stale/retained-only fixtures.
                        if((initial && scenario!=='retained_baseline') ||
                            (!initial && scenario!=='retained_recovery' &&
                                (connections<=3 || (connections===4 && scenario!=='retained_final'))))
                            publish(socket,topic,bridgeInfo(),false);
                    }
                }
                if(topic==='zigbee2mqtt/bridge/devices'){
                    publish(socket,topic,[device]);
                    publish(socket,'zigbee2mqtt/fixture',{[property]:value});
                }
            }
            if(type===3){
                const n=body.readUInt16BE(),topic=body.subarray(2,2+n).toString();
                const payload=JSON.parse(body.subarray(2+n).toString());
                if(topic.endsWith('/get'))publish(socket,'zigbee2mqtt/fixture',{[property]:value});
                if(topic.endsWith('/set'))value=payload[property];
                if(topic==='zigbee2mqtt/bridge/request/options'){
                    const desired=payload.options?.advanced?.log_level;
                    if(!['debug','info','warning','error'].includes(desired))
                        throw Error('Invalid log level');
                    writes.push(desired);level=desired;
                    if(initial&&scenario==='uncertain'){socket.destroy();continue;}
                    publish(socket,'zigbee2mqtt/bridge/response/options',
                        {status:'ok',transaction:payload.transaction,data:{restart_required:false}});
                    if(initial && ['ack_only','retained_update'].includes(scenario)) {
                        if(scenario==='retained_update')
                            publish(socket,'zigbee2mqtt/bridge/info',bridgeInfo(),true);
                        setTimeout(()=>socket.end(),100);
                    } else if(!initial && scenario==='restore_disconnect' && desired==='info') {
                        setTimeout(()=>socket.end(),100);
                    } else {
                        publish(socket,'zigbee2mqtt/bridge/info',bridgeInfo());
                    }
                }
                if(initial&&topic==='zigbee2mqtt/bridge/request/device/reporting/read'){
                    if(scenario==='foreign')level='warning';
                    if(['normal_foreign','normal_stale_retained'].includes(scenario)) {
                        // The first log change was applied; the normal finally
                        // must not overwrite an externally changed log level.
                        level='warning';
                        if(scenario==='normal_foreign')
                            publish(socket,'zigbee2mqtt/bridge/info',bridgeInfo(),false);
                        else {
                            const stale={...bridgeInfo(),config:{advanced:{log_level:'debug'}}};
                            publish(socket,'zigbee2mqtt/bridge/info',stale,true);
                        }
                        publish(socket,'zigbee2mqtt/bridge/response/device/reporting/read',
                            {status:'error',transaction:payload.transaction,error:'synthetic fixture rejection'});
                    } else socket.destroy();
                }
            }
        }});
    });
    let child;
    try{
        await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
        await fs.mkdir(path.join(directory,'test/build'),{recursive:true});
        await fs.writeFile(path.join(directory,'config.yaml'),'mqtt:\n  base_topic: zigbee2mqtt\n');
        await fs.writeFile(path.join(directory,'local.json'),
            JSON.stringify({mqttHost:'127.0.0.1',mqttPort:server.address().port,
                zigbeeConfiguration:path.join(directory,'config.yaml'),
                liveConverter:path.join(directory,'unused.mjs')}));
        const env={...process.env,RACOOL_LOCAL_CONFIG:path.join(directory,'local.json'),
            RACOOL_MQTT_HOST:'127.0.0.1',RACOOL_MQTT_PORT:String(server.address().port),
            RACOOL_ZIGBEE_CONFIG:path.join(directory,'config.yaml'),
            RACOOL_LIVE_CONVERTER:path.join(directory,'unused.mjs'),
            RACOOL_DEVICE_IEEE:device.ieee_address,RACOOL_REPORT_TRAFFIC:'1',
            RACOOL_ALLOW_BRIDGE_LOG_MUTATION:'1'};
        for(const key of ['RACOOL_REPORT_CHANGES','RACOOL_REPORT_RECONFIGURE',
            'RACOOL_RESULT_ID','RACOOL_TEST_CUT_CLIENT_AFTER_WIRE',
            'RACOOL_TEST_CUT_CLIENT_AFTER_BRIDGE_LOG'])delete env[key];
        if(['controlled_cut','already_debug','retained_baseline','invalid_cut_flags'].includes(scenario))
            env.RACOOL_TEST_CUT_CLIENT_AFTER_BRIDGE_LOG='1';
        if(scenario==='invalid_cut_flags')env.RACOOL_TEST_CUT_CLIENT_AFTER_WIRE='1';
        child=spawn(process.execPath,[helper,'verify',control],{cwd:directory,env});
        let output='';child.stdout.on('data',c=>output+=c);child.stderr.on('data',c=>output+=c);
        const exit=await new Promise((resolve,reject)=>{
            const timer=setTimeout(()=>{child.kill();reject(Error('Bridge logging recovery test timed out'));},40000);
            child.once('error',reject);
            child.once('close',code=>{clearTimeout(timer);resolve(code);});
        });
        assert.equal(exit,1,output);
        const result=JSON.parse(await fs.readFile(
            path.join(directory,'test/build/zigbee-live-verify.json'),'utf8'));
        assert.equal(result.completed,false,output);
        if(['missing_schema','retained_baseline','already_debug','invalid_cut_flags'].includes(scenario)) {
            // Reject before any global write; a retained-only snapshot is not fresh.
            const expectedMessage={
                missing_schema:/Missing authoritative bridge log-level schema/,
                retained_baseline:/Fresh non-retained bridge\/info baseline required/,
                already_debug:/baseline is already debug/,
                invalid_cut_flags:/Choose only one controlled client disconnect phase/
            }[scenario];
            assert.match(result.operationError,expectedMessage,output);
            assert.equal(result.transportClosedBeforeCompletion,undefined,output);
            assert.equal(result.testClientDisconnect,undefined,output);
        } else if(['normal_foreign','normal_stale_retained'].includes(scenario)) {
            assert.equal(result.transportClosedBeforeCompletion,undefined,output);
            assert.match(result.operationError,
                /Bridge log level changed or fresh live evidence lost; no global writes/,output);
        } else {
            assert.equal(result.transportClosedBeforeCompletion,true,output);
        }
        const expected={
            confirmed:'restored',controlled_cut:'restored',
            foreign:'foreign_change',uncertain:'delivery_unknown',
            ack_only:'delivery_unknown',retained_update:'delivery_unknown',
            restore_disconnect:'failed',post_restore_foreign:'failed',
            version_drift:'failed',missing_schema:undefined,
            retained_baseline:undefined,already_debug:undefined,invalid_cut_flags:undefined,
            retained_recovery:'snapshot_only',stale_retained_foreign:'snapshot_only',
            retained_restore_child:'failed',
            retained_final:'final_snapshot_unverified',
            stale_retained_final_foreign:'final_snapshot_unverified',
            normal_foreign:'foreign_change',normal_stale_retained:'foreign_change'
        }[scenario];
        assert.equal(result.bridgeLogRecovery?.status,expected,output);
        if(['retained_recovery','stale_retained_foreign'].includes(scenario)) {
            assert.equal(result.bridgeLogRecovery.before.retained,true,output);
            assert.equal(result.bridgeLogRecovery.before.value,'debug',output);
        }
        if(scenario==='retained_restore_child')
            assert.match(result.bridgeLogRecovery.error,/Recovery session failed/,output);
        if(['confirmed','controlled_cut','retained_final','stale_retained_final_foreign'].includes(scenario)) {
            assert.equal(result.bridgeLogRecovery.verifiedLive,true,output);
            assert.equal(result.bridgeLogRecovery.after.value,'info',output);
            assert.equal(result.bridgeLogRecovery.after.retained,
                !['confirmed','controlled_cut'].includes(scenario),output);
        }
        if(scenario==='controlled_cut')
            assert.equal(result.testClientDisconnect?.phase,'verified_live_bridge_info',output);
        assert.deepEqual(writes,
            ['missing_schema','retained_baseline','already_debug','invalid_cut_flags'].includes(scenario)?[]:
            ['confirmed','controlled_cut','restore_disconnect','post_restore_foreign',
                'retained_final','stale_retained_final_foreign'].includes(scenario)
                ?['debug','info']:['debug']);
        assert.equal(level,
            scenario==='already_debug'?'debug':
            ['missing_schema','retained_baseline','invalid_cut_flags'].includes(scenario)?'info':
            ['foreign','post_restore_foreign','stale_retained_foreign','retained_restore_child',
                'stale_retained_final_foreign','normal_foreign','normal_stale_retained'].includes(scenario)?'warning':
            ['confirmed','controlled_cut','restore_disconnect','retained_final'].includes(scenario)?'info':'debug');
    } finally{
        child?.kill();for(const socket of clients)socket.destroy();
        await new Promise(resolve=>server.close(resolve));
        await fs.rm(directory,{recursive:true,force:true});
    }
});
