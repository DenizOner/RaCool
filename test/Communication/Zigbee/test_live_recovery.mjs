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

for(const [scenario,kind,endpoint,attribute,suffix] of [
    ...['confirmed','controlled_cut','foreign','uncertain','identity','reconfigure'].map(s=>[s,'radiator',1,0x8030,'climate']),
    ...[
        ['ambient',4,0x8031,'ambient_sensor'],
        ['processor',5,0x8032,'processor_sensor'],
        ['health',6,0x8033,'system_health'],
        ['memory',7,0x8034,'memory'],
        ['uptime',8,0x8035,'uptime']
    ].flatMap(([kind,endpoint,attribute,suffix]) =>
        ['confirmed','controlled_cut','foreign'].map(scenario=>[scenario,kind,endpoint,attribute,suffix]))
]) test('recovery after '+scenario+' disconnect: '+kind,async()=>{
    const directory=await fs.mkdtemp(path.join(os.tmpdir(),'racool-recovery-'));
    let value=3, first=true, writes=[], clients=new Set();
    const control='report_interval_'+kind+'_s', property=control+'_'+suffix;
    const device={friendly_name:'fixture',ieee_address:'0x0000000000000123',network_address:123,
        model_id:'RaCool-ESP32C6',definition:{exposes:[{name:control,property,
            type:'numeric',access:7,value_min:0,value_max:600,value_step:1}]}};
    const server=net.createServer(socket=>{
        const initial=first;first=false;clients.add(socket);socket.on('close',()=>clients.delete(socket));
        socket.on('error',error=>{if(error.code!=='ECONNRESET') throw error;});let buffer=Buffer.alloc(0);
        socket.on('data',chunk=>{buffer=Buffer.concat([buffer,chunk]);while(buffer.length>1){
            let off=1,len=0,mul=1,b;do{if(off>=buffer.length)return;b=buffer[off++];len+=(b&127)*mul;mul*=128;}while(b&128);
            if(buffer.length<off+len)return;const type=buffer[0]>>4,body=buffer.subarray(off,off+len);buffer=buffer.subarray(off+len);
            if(type===1)socket.write(Buffer.from([0x20,2,0,0]));
            if(type===8){const n=body.readUInt16BE(2),topic=body.subarray(4,4+n).toString();
                if(topic==='zigbee2mqtt/bridge/devices'){
                    const listed=scenario==='identity'&&!initial?{...device,ieee_address:'0x0000000000000124'}:device;
                    publish(socket,topic,[listed]);publish(socket,'zigbee2mqtt/fixture',{[property]:value});}}
            if(type===3){const n=body.readUInt16BE(),topic=body.subarray(2,2+n).toString(),payload=JSON.parse(body.subarray(2+n).toString());
                if(topic.endsWith('/get'))publish(socket,'zigbee2mqtt/fixture',{[property]:value});
                if(topic.endsWith('/set')){writes.push(payload[property]);value=payload[property];if(initial&&scenario==='uncertain')socket.destroy();}
                if(topic==='zigbee2mqtt/bridge/request/action'){
                    assert.equal(payload.params.dst_endpoint,endpoint);
                    assert.equal(payload.params.zcl.payload[0].attrId,attribute);
                    const data={clusterID:0x0204,endpoint,address:123,data:{type:'Buffer',data:[0x18,9,1,attribute&255,attribute>>8,0,0x21,value&255,value>>8]}};
                    publish(socket,'zigbee2mqtt/bridge/response/action',{status:'ok',transaction:payload.transaction,data});
                }
                if(initial&&topic==='zigbee2mqtt/bridge/request/device/reporting/read'){
                    if(scenario==='foreign')value=9;
                    if(scenario!=='controlled_cut')socket.destroy();
                }
                if(initial&&scenario==='reconfigure'&&topic==='zigbee2mqtt/bridge/request/device/configure') socket.destroy();
            }
        }});
    });
    let child;
    try{
        await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
        await fs.mkdir(path.join(directory,'test/build'),{recursive:true});
        await fs.writeFile(path.join(directory,'config.yaml'),'mqtt:\n  base_topic: zigbee2mqtt\n');
        await fs.writeFile(path.join(directory,'local.json'),JSON.stringify({mqttHost:'127.0.0.1',mqttPort:server.address().port,
            zigbeeConfiguration:path.join(directory,'config.yaml'),liveConverter:path.join(directory,'unused.mjs')}));
        const env={...process.env,RACOOL_LOCAL_CONFIG:path.join(directory,'local.json'),RACOOL_MQTT_HOST:'127.0.0.1',
            RACOOL_MQTT_PORT:String(server.address().port),RACOOL_ZIGBEE_CONFIG:path.join(directory,'config.yaml'),
            RACOOL_LIVE_CONVERTER:path.join(directory,'unused.mjs'),RACOOL_DEVICE_IEEE:device.ieee_address};
        for(const key of ['RACOOL_REPORT_TRAFFIC','RACOOL_REPORT_CHANGES',
            'RACOOL_REPORT_RECONFIGURE','RACOOL_RESULT_ID',
            'RACOOL_TEST_CUT_CLIENT_AFTER_WIRE']) delete env[key];
        if(scenario==='reconfigure')env.RACOOL_REPORT_RECONFIGURE='1';
        if(scenario==='controlled_cut')env.RACOOL_TEST_CUT_CLIENT_AFTER_WIRE='1';
        child=spawn(process.execPath,[helper,'verify',control],{cwd:directory,env});
        let output='';child.stdout.on('data',c=>output+=c);child.stderr.on('data',c=>output+=c);
        const exit=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{child.kill();reject(Error('Recovery test timed out'));},40000);
            child.on('error',reject);child.on('close',code=>{clearTimeout(timer);resolve(code);});});
        assert.equal(exit,1,output);
        const result=JSON.parse(await fs.readFile(path.join(directory,'test/build/zigbee-live-verify.json'),'utf8'));
        assert.equal(result.completed,false);assert.equal(result.transportClosedBeforeCompletion,true);
        assert.equal(result.recovery?.status,{confirmed:'restored',controlled_cut:'restored',foreign:'foreign_change',
            uncertain:'delivery_unknown',identity:'failed',reconfigure:undefined}[scenario],output);
        assert.equal(result.recovery?.artifacts?.length ?? 0,['confirmed','controlled_cut'].includes(scenario)?3:scenario==='foreign'?1:0);
        assert.deepEqual(writes,['confirmed','controlled_cut'].includes(scenario)?[0,3]:[0]);
        assert.equal(value,['confirmed','controlled_cut'].includes(scenario)?3:scenario==='foreign'?9:0);
        if(scenario==='controlled_cut') {
            assert.equal(result.testClientDisconnect?.phase,'verified_wire',output);
            assert.equal(result.testClientDisconnect?.control,control,output);
        }
    } finally {
        child?.kill();for(const socket of clients)socket.destroy();await new Promise(resolve=>server.close(resolve));
        await fs.rm(directory,{recursive:true,force:true});
    }
});


for (const scenario of ['confirmed','foreign','uncertain','ack_only','retained_update',
    'restore_disconnect','post_restore_foreign','version_drift','missing_schema']) test(
    'bridge logging recovery after '+scenario+' MQTT disconnect', async()=>{
    const directory=await fs.mkdtemp(path.join(os.tmpdir(),'racool-log-recovery-'));
    const control='report_interval_radiator_s',property=control+'_climate';
    const device={friendly_name:'fixture',ieee_address:'0x0000000000000123',
        network_address:123,model_id:'RaCool-ESP32C6',definition:{exposes:[{
            name:control,property,type:'numeric',access:7,value_min:0,value_max:600,value_step:1}]}};
    let level='info',value=3,first=true,connections=0;
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
                    publish(socket,topic,bridgeInfo(),true);
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
                    socket.destroy();
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
            RACOOL_DEVICE_IEEE:device.ieee_address,RACOOL_REPORT_TRAFFIC:'1'};
        for(const key of ['RACOOL_REPORT_CHANGES','RACOOL_REPORT_RECONFIGURE','RACOOL_RESULT_ID'])delete env[key];
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
        if(scenario==='missing_schema') {
            // Reject before even starting an asynchronous MQTT mutation.
            assert.match(result.operationError,/Missing authoritative bridge log-level schema/);
            assert.equal(result.transportClosedBeforeCompletion,undefined,output);
        } else {
            assert.equal(result.transportClosedBeforeCompletion,true,output);
        }
        const expected={
            confirmed:'restored',foreign:'foreign_change',uncertain:'delivery_unknown',
            ack_only:'delivery_unknown',retained_update:'delivery_unknown',
            restore_disconnect:'failed',post_restore_foreign:'failed',
            version_drift:'failed',missing_schema:undefined
        }[scenario];
        assert.equal(result.bridgeLogRecovery?.status,expected,output);
        if(scenario==='confirmed') {
            assert.equal(result.bridgeLogRecovery.verifiedLive,true,output);
            assert.equal(result.bridgeLogRecovery.after.value,'info',output);
            assert.equal(result.bridgeLogRecovery.after.retained,true,output);
        }
        assert.deepEqual(writes,
            scenario==='missing_schema'?[]:
            ['confirmed','restore_disconnect','post_restore_foreign'].includes(scenario)
                ?['debug','info']:['debug']);
        assert.equal(level,
            scenario==='missing_schema'?'info':
            scenario==='foreign'||scenario==='post_restore_foreign'?'warning':
            ['confirmed','restore_disconnect'].includes(scenario)?'info':'debug');
    } finally{
        child?.kill();for(const socket of clients)socket.destroy();
        await new Promise(resolve=>server.close(resolve));
        await fs.rm(directory,{recursive:true,force:true});
    }
});
