import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const helper = fileURLToPath(new URL('./diagnostic/live_zigbee.mjs', import.meta.url));
function field(text) {
    const value = Buffer.from(text);
    const length = Buffer.alloc(2);
    length.writeUInt16BE(value.length);
    return Buffer.concat([length, value]);
}

for (const mode of ['verify','configure']) test('disconnect during '+mode+' records failure before writing the result', async () => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'racool-disconnect-'));
    const publications = [];
    const clients = new Set();
    const server = net.createServer(socket => {
        clients.add(socket);
        socket.on('close', () => clients.delete(socket));
        let buffer = Buffer.alloc(0);
        socket.on('data', chunk => {
            buffer = Buffer.concat([buffer, chunk]);
            while (buffer.length > 1) {
                let offset=1, length=0, multiplier=1, byte;
                do { if(offset>=buffer.length)return; byte=buffer[offset++];length+=(byte&127)*multiplier;multiplier*=128; } while(byte&128);
                if(buffer.length<offset+length)return;
                const type=buffer[0]>>4, body=buffer.subarray(offset,offset+length);
                buffer=buffer.subarray(offset+length);
                if(type===1) socket.write(Buffer.from([0x20,2,0,0]));
                if(type===8) {
                    const size=body.readUInt16BE(2);
                    if(body.subarray(4,4+size).toString()==='zigbee2mqtt/bridge/devices') {
                        publish(socket,'zigbee2mqtt/bridge/devices',[{friendly_name:'fixture',ieee_address:'0x0000000000000123',
                            model_id:'RaCool-ESP32C6',definition:{exposes:[{name:'period',property:'period_sensor',
                                type:'numeric',access:7,value_min:0,value_max:10,value_step:1,endpoint:'sensor'}]}}]);
                        publish(socket,'zigbee2mqtt/fixture',{period_sensor:5});
                    }
                }
                if(type===3) {
                    const size=body.readUInt16BE(), topic=body.subarray(2,2+size).toString();
                    publications.push(topic);
                    if(topic.endsWith('/get')) publish(socket,'zigbee2mqtt/fixture',{period_sensor:5});
                    if(topic.endsWith('/set') || topic==='zigbee2mqtt/bridge/request/device/configure') socket.destroy();
                }
            }
        });
    });
    let child;
    try {
        await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
        await fs.mkdir(path.join(directory,'test/build'),{recursive:true});
        await fs.writeFile(path.join(directory,'config.yaml'),'mqtt:\n  base_topic: zigbee2mqtt\n');
        await fs.writeFile(path.join(directory,'local.json'),JSON.stringify({mqttHost:'127.0.0.1',
            mqttPort:server.address().port,zigbeeConfiguration:path.join(directory,'config.yaml'),
            liveConverter:path.join(directory,'unused.mjs')}));
        child=spawn(process.execPath,mode==='verify' ? [helper,mode,'period_sensor'] : [helper,mode],{cwd:directory,
            env:{...process.env,RACOOL_LOCAL_CONFIG:path.join(directory,'local.json'),RACOOL_MQTT_HOST:'127.0.0.1',
                RACOOL_MQTT_PORT:String(server.address().port),RACOOL_ZIGBEE_CONFIG:path.join(directory,'config.yaml'),
                RACOOL_LIVE_CONVERTER:path.join(directory,'unused.mjs'),RACOOL_DEVICE_IEEE:'0x0000000000000123'}});
        let output='';child.stdout.on('data',chunk=>output+=chunk);child.stderr.on('data',chunk=>output+=chunk);
        const exit=await new Promise((resolve,reject)=>{
            const timer=setTimeout(()=>{child.kill();reject(Error('Disconnect helper timed out'));},12000);
            child.on('error',reject);child.on('close',code=>{clearTimeout(timer);resolve(code);});
        });
        assert.equal(exit,1,output);
        const result=JSON.parse(await fs.readFile(path.join(directory,`test/build/zigbee-live-${mode}.json`),'utf8'));
        assert.notEqual(result.completed,true);
        assert.equal(result.transportClosedBeforeCompletion,true);
        assert.match(result.operationError ?? result.configureError,/connection closed/i);
        assert.equal(publications.filter(topic=>topic.endsWith('/set')).length,mode==='verify'?1:0);
    } finally {
        child?.kill();for(const client of clients)client.destroy();
        await new Promise(resolve=>server.close(resolve));
        await fs.rm(directory,{recursive:true,force:true});
    }
});
function publish(socket, topic, value) {
    const body = Buffer.concat([field(topic), Buffer.from(JSON.stringify(value))]);
    let length = body.length;
    const bytes = [];
    do { const byte = length % 128; length = Math.floor(length / 128); bytes.push(byte | (length ? 128 : 0)); } while (length);
    socket.write(Buffer.concat([Buffer.from([0x30, ...bytes]), body]));
}

for (const expectation of [{period_sensor:0}, null, {wrong_property:0}]) {
test('read mode safely validates expected values: ' + JSON.stringify(expectation), async () => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'racool-read-'));
    const publications = [];
    const clients = new Set();
    const server = net.createServer(socket => {
        clients.add(socket);
        socket.on('close', () => clients.delete(socket));
        let buffer = Buffer.alloc(0);
        socket.on('data', chunk => {
            buffer = Buffer.concat([buffer, chunk]);
            while (buffer.length > 1) {
                let offset = 1, length = 0, multiplier = 1, byte;
                do {
                    if (offset >= buffer.length) return;
                    byte = buffer[offset++]; length += (byte & 127) * multiplier; multiplier *= 128;
                } while (byte & 128);
                if (buffer.length < offset + length) return;
                const type = buffer[0] >> 4;
                const body = buffer.subarray(offset, offset + length);
                buffer = buffer.subarray(offset + length);
                if (type === 1) socket.write(Buffer.from([0x20, 2, 0, 0]));
                if (type === 8) {
                    const size = body.readUInt16BE(2);
                    if (body.subarray(4, 4 + size).toString() === 'zigbee2mqtt/bridge/devices') {
                        publish(socket, 'zigbee2mqtt/bridge/devices', [{friendly_name:'fixture', ieee_address:'0x0000000000000123',
                            model_id:'RaCool-ESP32C6', definition:{exposes:[{name:'period', property:'period_sensor',
                                type:'numeric', access:5, endpoint:'sensor'}]}}]);
                        publish(socket, 'zigbee2mqtt/fixture', {period_sensor:99});
                    }
                }
                if (type === 3) {
                    const size = body.readUInt16BE();
                    const topic = body.subarray(2, 2 + size).toString();
                    publications.push(topic);
                    if (topic.endsWith('/get')) {
                        publish(socket, 'zigbee2mqtt/fixture', {period_sensor:99});
                        setTimeout(() => {if (!socket.destroyed) publish(socket, 'zigbee2mqtt/fixture', {period_sensor:0});}, 50);
                    }
                }
            }
        });
    });
    let child;
    try {
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
        await fs.mkdir(path.join(directory, 'test/build'), {recursive:true});
        await fs.writeFile(path.join(directory, 'config.yaml'), 'mqtt:\n  base_topic: zigbee2mqtt\n');
        await fs.writeFile(path.join(directory, 'local.json'), JSON.stringify({mqttHost:'127.0.0.1',
            mqttPort:server.address().port, zigbeeConfiguration:path.join(directory, 'config.yaml'),
            liveConverter:path.join(directory, 'unused.mjs')}));
        child = spawn(process.execPath, [helper, 'read', 'period_sensor'], {cwd:directory,
            env:{...process.env, RACOOL_LOCAL_CONFIG:path.join(directory, 'local.json'),
                RACOOL_MQTT_HOST:'127.0.0.1', RACOOL_MQTT_PORT:String(server.address().port),
                RACOOL_ZIGBEE_CONFIG:path.join(directory, 'config.yaml'), RACOOL_LIVE_CONVERTER:path.join(directory, 'unused.mjs'),
                RACOOL_DEVICE_IEEE:'0x0000000000000123', RACOOL_READ_EXPECTED:JSON.stringify(expectation)}});
        let output = '';
        child.stdout.on('data', chunk => output += chunk);
        child.stderr.on('data', chunk => output += chunk);
        const exit = await new Promise((resolve, reject) => {
            const timer = setTimeout(() => {child.kill(); reject(Error('Read helper timed out'));}, 12000);
            child.on('error', reject);
            child.on('close', code => {clearTimeout(timer); resolve(code);});
        });
        const valid = expectation?.period_sensor === 0;
        assert.equal(exit, valid ? 0 : 1, output);
        const document = JSON.parse(await fs.readFile(path.join(directory, 'test/build/zigbee-live-read.json'), 'utf8'));
        if (valid) {
            assert.equal(document.completed, true);
            assert.equal(document.initialReads[0].value, 0);
            assert.equal(document.initialReads[0].evidence.kind, 'mqtt_state_after_get');
            assert.ok(publications.includes('zigbee2mqtt/fixture/sensor/get'));
        } else {
            assert.notEqual(document.completed, true);
            assert.ok(!publications.includes('zigbee2mqtt/fixture/sensor/get'));
        }
        assert.ok(publications.every(topic => topic.endsWith('/get')));
    } finally {
        child?.kill();
        for (const client of clients) client.destroy();
        await new Promise(resolve => server.close(resolve));
        await fs.rm(directory, {recursive:true, force:true});
    }
});
}
