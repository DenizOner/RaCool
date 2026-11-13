import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs/promises';
class ElementFixture {
    constructor() { this.children = []; this.style = {}; this.attributes = {}; this.listeners = {}; this.textContent = ''; }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = children; }
    setAttribute(name, value) { this.attributes[name] = value; }
    addEventListener(name, listener) { this.listeners[name] = listener; }
    emit(name) { return this.listeners[name]?.(); }
}
globalThis.HTMLElement = class extends ElementFixture {
    attachShadow() { this.shadowRoot = new ElementFixture(); }
};
const registry = new Map();
globalThis.customElements = {get: name => registry.get(name), define: (name, constructor) => registry.set(name, constructor)};
globalThis.document = {createElement: () => new ElementFixture()};
globalThis.window = {};
const source = await fs.readFile(new URL('../../tools/home-assistant/racool-zone-card.js', import.meta.url), 'utf8');
const {validateZones, readZone, nextBoundary} = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
const zone = {label: 'Soğuk', low: 'number.cold_low', high: 'number.cold_high', min: 7, max: 25};
function findElement(element, predicate) {
    if (predicate(element)) return element;
    for (const child of element.children) { const found = findElement(child, predicate); if (found) return found; }
}
function createCard(callService) {
    const Card = registry.get('racool-zone-card'), card = new Card();
    card.setConfig({zones: [zone]});
    card.hass = {states: {[zone.low]: {state: '17'}, [zone.high]: {state: '19'}}, callService};
    const input = findElement(card.shadowRoot, element => element.attributes['aria-label'] === 'Soğuk alt sınır');
    const status = findElement(card.shadowRoot, element => element.attributes.role === 'status');
    return {card, input, status};
}
test('card validates unique number entities and rejects unusable device states', () => {
    assert.deepEqual(validateZones({zones: [zone]}), [zone]);
    assert.throws(() => validateZones({zones: [zone, zone]}));
    assert.throws(() => validateZones({zones: [{...zone, low: 'switch.test'}]}));
    for (const pair of [['unknown', '19'], ['17', 'unavailable'], ['', '19'], ['19', '17'], ['NaN', '19'], ['6', '19']]) {
        assert.equal(readZone({states: {[zone.low]: {state: pair[0]}, [zone.high]: {state: pair[1]}}}, zone), null);
    }
    assert.deepEqual(readZone({states: {[zone.low]: {state: '17'}, [zone.high]: {state: '19'}}}, zone), [17, 19]);
});
test('either thumb preserves a strict interval including keyboard boundary requests', () => {
    assert.equal(nextBoundary(zone, [17, 19], 0, 25), 18);
    assert.equal(nextBoundary(zone, [17, 19], 0, 0), 7);
    assert.equal(nextBoundary(zone, [17, 19], 1, 0), 18);
    assert.equal(nextBoundary(zone, [17, 19], 1, 99), 25);
    assert.throws(() => nextBoundary(zone, [17, 19], 1, NaN));
});
test('pending frontend change prevents duplicate writes and waits for matching state', async () => {
    let complete;
    const calls = [];
    const {card, input, status} = createCard((...args) => {calls.push(args); return new Promise(resolve => {complete = resolve;});});
    input.value = '18'; input.emit('input');
    const write = input.emit('change');
    assert.equal(input.disabled, true);
    assert.equal(status.textContent, 'Cihaz değeri bekleniyor…');
    input.value = '17'; await input.emit('change');
    assert.equal(calls.length, 1);
    assert.deepEqual(calls[0], ['number', 'set_value', {entity_id: zone.low, value: 18}]);
    card.hass = {states: {[zone.low]: {state: '18'}, [zone.high]: {state: '19'}}, callService: () => {throw Error('Unexpected repeat');}};
    assert.equal(input.disabled, false);
    assert.equal(status.textContent, 'Güncel değer alındı.');
    complete(); await write; card.disconnectedCallback();
});
test('service failure restores displayed state and unavailable state never writes', async () => {
    let calls = 0;
    const {card, input, status} = createCard(async () => {++calls; throw Error('Rejected service');});
    input.value = '18'; await input.emit('change');
    assert.equal(status.textContent, 'Değişiklik gönderilemedi.');
    assert.equal(Number(input.value), 17);
    assert.equal(input.disabled, false);
    card.hass = {states: {[zone.low]: {state: 'unavailable'}, [zone.high]: {state: '19'}}, callService: async () => {++calls;}};
    input.value = '18'; await input.emit('change');
    assert.equal(input.disabled, true); assert.equal(calls, 1);
    card.disconnectedCallback();
});

test('fan range clamps both handles and preserves positive hysteresis',()=>{
 const fan={label:'Fan',low:'number.fan_off',high:'number.fan_on',min:20,max:99};
 assert.deepEqual(validateZones({zones:[fan]}),[fan]);
 assert.equal(nextBoundary(fan,[28,30],0,99),29);
 assert.equal(nextBoundary(fan,[28,30],1,20),29);
 assert.equal(nextBoundary(fan,[28,30],0,-1),20);
 assert.equal(nextBoundary(fan,[28,30],1,999),99);
 assert.equal(readZone({states:{'number.fan_off':{state:'30'},'number.fan_on':{state:'30'}}},fan),null);
});

test('ambient difference range keeps decimal resolution and strict hysteresis',()=>{
 const difference={label:'Fark',low:'number.off',high:'number.on',min:-9,max:9,step:0.1,min_gap:0.1};
 validateZones({zones:[difference]});
 assert.equal(nextBoundary(difference,[1,2],0,1.4),1.4);
 assert.equal(nextBoundary(difference,[1,2],1,-9),1.1);
 assert.equal(nextBoundary(difference,[1,2],0,9),1.9);
 assert.equal(nextBoundary(difference,[-2,-1],1,-1.7),-1.7);
 for(const step of [0,NaN,-1])assert.throws(()=>validateZones({zones:[{...difference,step}]}));
});

test('controls surround ranges and receive current HA state',async()=>{
 window.loadCardHelpers=async()=>({createCardElement:config=>({config})});
 const Card=registry.get('racool-zone-card'),card=new Card();
 card.setConfig({zones:[zone],before:[{type:'entities',title:'Donma'}],after:[{type:'entities',title:'Tehlike'}]});
 await Promise.resolve();
 const hass={states:{}};card.hass=hass;
 assert.deepEqual(card._children.map(child=>child.config.title),['Donma','Tehlike']);
 assert.ok(card._children.every(child=>child.hass===hass));
 card.disconnectedCallback();
});
