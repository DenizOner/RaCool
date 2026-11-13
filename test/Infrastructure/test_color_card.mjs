import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs/promises';
const source = await fs.readFile(new URL('../../tools/home-assistant/racool-color-card.js', import.meta.url), 'utf8');
class ElementFixture {
    constructor() {this.children=[]; this.attributes={}; this.listeners={}; this.textContent='';}
    append(...children) {this.children.push(...children);}
    replaceChildren(...children) {this.children=children;}
    setAttribute(key,value) {this.attributes[key]=value;}
    addEventListener(key,listener) {this.listeners[key]=listener;}
}
globalThis.HTMLElement = class extends ElementFixture {attachShadow() {this.shadowRoot=new ElementFixture();}};
const registry = new Map([['ha-hs-color-picker',class {}]]);
globalThis.customElements = {get: name=>registry.get(name),define:(name,value)=>registry.set(name,value)};
globalThis.document = {createElement:()=>new ElementFixture()};
globalThis.window = {};
const {validXY, parseXYState, hsToXY, xyToHS, validateColorConfig} = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));

test('unavailable or malformed device colors never become fabricated defaults', () => {
    for (const value of [null, false, 1, [], {}, {x: 1, y: 0}, {x: 1.2, y: 3}, {x: 65280, y: 1}, {x: 1, y: 2, z: 3}]) assert.equal(validXY(value), false);
    for (const value of ['unavailable', 'unknown', '{}', '{"x":1,"y":0}', "{'x': 1, 'x': 2}", 'globalThis.secret']) assert.equal(parseXYState(value), null);
    assert.deepEqual(parseXYState("{'y': 22000, 'x': 21000}"), {x: 21000, y: 22000});
    assert.deepEqual(parseXYState('{"x":21000,"y":22000}'), {x: 21000, y: 22000});
    assert.deepEqual(parseXYState(JSON.stringify({x:21000/65536,y:22000/65536})),{x:21000,y:22000});
    assert.deepEqual(parseXYState("{'x': 0.25, 'y': 0.5}"),{x:16384,y:32768});
});
test('HS wheel conversion preserves hue and saturation across the color wheel', () => {
    for (let hue = 0; hue < 360; hue += 5) for (const saturation of [0.1, 0.5, 1]) {
        const xy = hsToXY([hue, saturation]); assert.equal(validXY(xy), true);
        const actual = xyToHS(xy), error = Math.min(Math.abs(actual[0] - hue), 360 - Math.abs(actual[0] - hue));
        assert.ok(error < 0.2, `${hue}: hue error ${error}`);
        assert.ok(Math.abs(actual[1] - saturation) < 0.002);
    }
    assert.ok(xyToHS(hsToXY([0, 0]))[1] < 0.001);
    for (const value of [[-1, 1], [361, 1], [0, 2], [NaN, 1], [0], null]) assert.throws(() => hsToXY(value));
});
test('only explicit unique device color routes may be published', () => {
    const zone = {label: 'Soğuk', entity: 'sensor.device_cold_color', property: 'cold_region_color_xy'};
    const config = {command_topic: 'zigbee2mqtt/device/set', zones: [zone]};
    assert.deepEqual(validateColorConfig(config), config);
    for (const topic of ['zigbee2mqtt/+/set', 'zigbee2mqtt/#/set', 'zigbee2mqtt/device/get', 'bad\n/set']) assert.throws(() => validateColorConfig({...config, command_topic: topic}));
    assert.throws(() => validateColorConfig({...config, zones: [zone, zone]}));
    assert.throws(() => validateColorConfig({...config, zones: [{...zone, entity: 'light.fake'}]}));
    assert.throws(() => validateColorConfig({...config, zones: [{...zone, property: 'power'}]}));
});

function createCard(callService) {
    const Card=registry.get('racool-color-card'), card=new Card();
    card.setConfig({command_topic:'zigbee2mqtt/device/set',zones:[{label:'Soğuk',entity:'sensor.cold',property:'cold_region_color_xy'}]});
    card.hass={states:{'sensor.cold':{state:'{"x":9831,"y":3933}',last_updated:new Date().toISOString()}},callService};
    return card;
}
test('picker publishes one coherent color without fabricating a light or applied state', async () => {
    const calls=[], card=createCard(async (...args)=>calls.push(args));
    await card._send([0,0]);
    const desired=hsToXY([0,0]);
    assert.deepEqual(calls,[['mqtt','publish',{topic:'zigbee2mqtt/device/set',payload:JSON.stringify({cold_region_color_xy:{x:desired.x/65536,y:desired.y/65536}}),qos:0,retain:false}]]);
    assert.notDeepEqual(card._rows[0].actual,desired);
    assert.ok(card._rows[0].pending);
    card.hass={states:{'sensor.cold':{state:JSON.stringify(desired),last_updated:new Date(Date.now()+1000).toISOString()}},callService:()=>{throw Error('Unexpected duplicate');}};
    assert.equal(card._rows[0].pending,null); assert.match(card._status.textContent,/güncellendi/);
    card.disconnectedCallback();
});
test('unavailable source and malformed selection cannot submit commands', async () => {
    let calls=0; const card=createCard(async ()=>++calls);
    await card._send([NaN,1]); assert.equal(calls,0);
    card.hass={states:{'sensor.cold':{state:'unavailable'}},callService:async ()=>++calls};
    await card._send([100,1]); assert.equal(calls,0); assert.equal(card._picker.disabled,true);
    card.disconnectedCallback();
});
test('failed publish leaves actual device color intact', async () => {
    const card=createCard(async ()=>{throw Error('No MQTT service');});
    await card._send([0,0]);
    assert.deepEqual(card._rows[0].actual,{x:9831,y:3933});
    assert.equal(card._rows[0].pending,null); assert.match(card._status.textContent,/gönderilemedi/);
    card.disconnectedCallback();
});
test('disconnect invalidates late publish completion and cleans timers', async () => {
    let finish; const card=createCard(()=>new Promise(resolve=>{finish=resolve;}));
    const send=card._send([0,0]); card.disconnectedCallback(); finish(); await send;
    assert.equal(card._rows[0].pending,null); assert.equal(card._rows[0].timer,null);
});
