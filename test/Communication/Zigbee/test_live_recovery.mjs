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
function publish(socket,topic,value){
    const body=Buffer.concat([field(topic),Buffer.from(JSON.stringify(value))]);let size=body.length,bytes=[];
    do{let b=size%128;size=Math.floor(size/128);bytes.push(b|(size?128:0));}while(size);
    socket.write(Buffer.concat([Buffer.from([0x30,...bytes]),body]));
}

for(const [scenario,kind,endpoint,attribute,suffix] of [
    ...['confirmed','foreign','uncertain','identity','reconfigure'].map(s=>[s,'radiator',1,0x8030,'climate']),
    ...[
        ['ambient',4,0x8031,'ambient_sensor'],
        ['processor',5,0x8032,'processor_sensor'],
        ['health',6,0x8033,'system_health'],
        ['memory',7,0x8034,'memory'],
        ['uptime',8,0x8035,'uptime']
    ].flatMap(([kind,endpoint,attribute,suffix]) =>
        ['confirmed','foreign'].map(scenario=>[scenario,kind,endpoint,attribute,suffix]))
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
                    socket.destroy();
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
        for(const key of ['RACOOL_REPORT_TRAFFIC','RACOOL_REPORT_CHANGES','RACOOL_REPORT_RECONFIGURE','RACOOL_RESULT_ID']) delete env[key];
        if(scenario==='reconfigure')env.RACOOL_REPORT_RECONFIGURE='1';
        child=spawn(process.execPath,[helper,'verify',control],{cwd:directory,env});
        let output='';child.stdout.on('data',c=>output+=c);child.stderr.on('data',c=>output+=c);
        const exit=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{child.kill();reject(Error('Recovery test timed out'));},40000);
            child.on('error',reject);child.on('close',code=>{clearTimeout(timer);resolve(code);});});
        assert.equal(exit,1,output);
        const result=JSON.parse(await fs.readFile(path.join(directory,'test/build/zigbee-live-verify.json'),'utf8'));
        assert.equal(result.completed,false);assert.equal(result.transportClosedBeforeCompletion,true);
        assert.equal(result.recovery?.status,{confirmed:'restored',foreign:'foreign_change',uncertain:'delivery_unknown',identity:'failed',reconfigure:undefined}[scenario],output);
        assert.equal(result.recovery?.artifacts?.length ?? 0,scenario==='confirmed'?3:scenario==='foreign'?1:0);
        assert.deepEqual(writes,scenario==='confirmed'?[0,3]:[0]);
        assert.equal(value,scenario==='confirmed'?3:scenario==='foreign'?9:0);
    } finally {
        child?.kill();for(const socket of clients)socket.destroy();await new Promise(resolve=>server.close(resolve));
        await fs.rm(directory,{recursive:true,force:true});
    }
});
