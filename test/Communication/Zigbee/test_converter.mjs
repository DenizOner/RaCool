import assert from 'node:assert/strict';
import test from 'node:test';
import definition from '../../../tools/racool.mjs';
const extensions = definition.extend;
const endpoints = extensions.find(x => x.endpoint).endpoint;
const model = {...definition, endpoint: endpoints, meta: {multiEndpoint: true}};
const all = extensions.flatMap(x => x.exposes ?? []);
const device = {endpoints: [], getEndpoint(id) {return this.endpoints.find(x => x.ID === id);}};
const writes = [];
test('active endpoint and property identifiers are English and preserve existing HA identities', () => {
    assert.deepEqual(endpoints(device), {climate:1,status_indicator:2,thermal_indicator:3,
        ambient_sensor:4,processor_sensor:5,system_health:6,memory:7,uptime:8,display:9,system:10});
    const oldTokens = /(?:soguk|ilik|sicak|tehlike|donma|dahili|harici|sinir|bolge|sikligi|parlakligi|sistem_yonetimi)/;
    for (const expose of all) {
        assert.equal(oldTokens.test(expose.name), false, expose.name);
        assert.equal(oldTokens.test(expose.property), false, expose.property);
    }
    const payload = {unique_id:'0x1234567890abcdef_fan_operating_mode_climate_zigbee2mqtt',
        object_id:'device_fan_operating_mode_climate', default_entity_id:'select.device_fan_operating_mode_climate',
        command_topic:'zigbee2mqtt/device/climate/set/fan_operating_mode',
        value_template:'{{ value_json.fan_operating_mode_climate }}'};
    definition.meta.overrideHaDiscoveryPayload(payload);
    assert.equal(payload.unique_id,'0x1234567890abcdef_fan_calisma_modu_isi_dagitma_fani_zigbee2mqtt');
    assert.equal(payload.default_entity_id,'select.device_fan_calisma_modu_isi_dagitma_fani');
    assert.equal(payload.command_topic,'zigbee2mqtt/device/climate/set/fan_operating_mode');
    assert.equal(payload.value_template,'{{ value_json.fan_operating_mode_climate }}');
    const signal={unique_id:'device_linkquality_zigbee2mqtt'};
    definition.meta.overrideHaDiscoveryPayload(signal);
    assert.deepEqual(signal,{unique_id:'device_linkquality_zigbee2mqtt',name:'Zigbee Sinyal Kalitesi',icon:'mdi:signal'});
    const unknown={unique_id:'unknown_unrecognized_zigbee2mqtt'};
    definition.meta.overrideHaDiscoveryPayload(unknown);
    assert.deepEqual(unknown,{unique_id:'unknown_unrecognized_zigbee2mqtt'});
    const nested={unique_id:'0x1234567890abcdef_future_temperature_climate_zigbee2mqtt'};
    definition.meta.overrideHaDiscoveryPayload(nested);
    assert.equal(nested.unique_id,'0x1234567890abcdef_future_temperature_climate_zigbee2mqtt');
    for (const kind of ['object','ambient']) {
        const effective={unique_id:`0x1234567890abcdef_effective_${kind}_temperature_climate_zigbee2mqtt`};
        definition.meta.overrideHaDiscoveryPayload(effective);
        assert.equal(effective.unique_id,`0x1234567890abcdef_effective_${kind}_temperature_isi_dagitma_fani_zigbee2mqtt`);
    }
});
for (const ID of Object.values(endpoints(device))) device.endpoints.push({ID,
    supportsInputCluster: () => true,
    async command(cluster, command, payload, options) {writes.push({ID, cluster, command, payload, options});},
    async write(cluster, payload, options) {writes.push({ID, cluster, payload, options});},
    async read(cluster, attributes, options) {writes.push({ID, cluster, attributes, options});},
});

test('five free zone colors write coherent pairs and only accept valid actual reads', async () => {
    const ext = extensions.find(extension => extension.exposes?.some(expose => expose.property === 'cold_region_color_xy_thermal_indicator'));
    assert.equal(all.filter(expose=>expose.name==='color_xy').length, 5);
    for (const [index, expose] of all.filter(expose=>expose.name==='color_xy').entries()) {
        assert.equal(expose.type, 'composite');
        assert.deepEqual(expose.features.map(feature => feature.name), ['x', 'y']);
        assert.deepEqual(expose.features.map(feature => feature.property), ['x', 'y']);
        const meta = {device, endpoint_name: expose.endpoint, mapped: model};
        const before = writes.length;
        for (const invalid of [null, [], {x: 1}, {x: 1, y: 0}, {x: 65535, y: 1}, {x: 1.5, y: 1}, {x: 1, y: 1, extra: 1}]) {
            await assert.rejects(ext.toZigbee[0].convertSet(device.getEndpoint(3), expose.property.replace(/_thermal_indicator$/,''), invalid, meta));
        }
        assert.equal(writes.length, before);
        const value = {x: 1234, y: 4321};
        assert.equal(await ext.toZigbee[0].convertSet(device.getEndpoint(3), expose.property.replace(/_thermal_indicator$/,''), value, meta), undefined);
        const attr = 0x8030 + index;
        assert.deepEqual(writes.at(-2), {ID: 3, cluster: 'genOnOff', payload: {[attr]: {value: 1234 + 4321 * 65536, type: 0x23}}, options: {manufacturerCode: null}});
        assert.deepEqual(writes.at(-1).attributes, [attr]);
        const message = {endpoint: {ID: 3}, data: {[attr]: 1234 + 4321 * 65536}};
        assert.deepEqual(ext.fromZigbee[0].convert(model, message), {[expose.property]: {x:value.x / 65536,y:value.y / 65536}});
        message.endpoint.ID = 2;
        assert.equal(ext.fromZigbee[0].convert(model, message), undefined);
        message.endpoint.ID = 3; message.data[attr] = 1234;
        assert.equal(ext.fromZigbee[0].convert(model, message), undefined);
    }
});

test('fan modes use canonical custom attribute values and endpoint routing', async () => {
    const ext = extensions.find(x => x.exposes?.some(e => e.name === 'fan_operating_mode'));
    const exp = ext.exposes[0];
    const expected = [2, 0, 3, 1];
    assert.deepEqual(exp.values, ['Kapalı', 'Nesneye duyarlı', 'Ortama duyarlı', 'Sürekli Açık']);
    for (const [i, value] of exp.values.entries()) {
        await ext.toZigbee[0].convertSet(device.getEndpoint(1), exp.name, value,
            {device, endpoint_name: exp.endpoint, mapped: model});
        assert.equal(writes.at(-1).ID, 1);
        assert.equal(writes.at(-1).cluster, 'hvacUserInterfaceCfg');
        assert.equal(writes.at(-1).payload[0x8050].value, expected[i]);
    }
    await ext.toZigbee[0].convertGet(device.getEndpoint(1), exp.name,
        {device, endpoint_name: exp.endpoint, mapped: model});
    assert.deepEqual(writes.at(-1).attributes, [0x8050]);
});

test('OLED offers only automatic and always-on modes with stable protocol values', async () => {
    const extension = extensions.find(item => item.exposes?.some(expose => expose.name === 'display_mode'));
    const expose = extension.exposes.find(item => item.name === 'display_mode');
    assert.deepEqual(expose.values, ['Otomatik', 'Daima Açık']);
    const meta = {device, endpoint_name: expose.endpoint, mapped: model};
    await assert.rejects(extension.toZigbee[0].convertSet(device.getEndpoint(9), expose.name, 'Kapalı', meta));
    for (const [name, value] of [['Otomatik', 1], ['Daima Açık', 2]]) {
        await extension.toZigbee[0].convertSet(device.getEndpoint(9), expose.name, name, meta);
        assert.equal(writes.at(-1).payload[0x8020].value, value);
    }
});

test('fan relay exposes real binary state read-only and configures change-only initial read', async () => {
    const extension = extensions.find(item => item.exposes?.some(expose => expose.name === 'fan_relay_state'));
    const expose = extension.exposes[0];
    assert.equal(expose.access, 5);
    assert.equal(extension.toZigbee[0].convertSet, undefined);
    const convert = value => extension.fromZigbee[0].convert(model, {endpoint: {ID: 1}, data: {onOff: value}});
    assert.deepEqual(convert(0), {[expose.property]: 'OFF'});
    assert.deepEqual(convert(1), {[expose.property]: 'ON'});
    for (const value of [undefined, null, 2, 'ON']) assert.equal(convert(value), undefined);
    const calls = [];
    const endpoint = {bind: async cluster => calls.push(['bind', cluster]),
        configureReporting: async (cluster, records) => calls.push(['report', cluster, records]),
        read: async (cluster, attributes) => calls.push(['read', cluster, attributes])};
    await extension.configure[0]({getEndpoint: ID => {assert.equal(ID, 1); return endpoint;}}, {});
    assert.deepEqual(calls, [['bind', 'genOnOff'], ['report', 'genOnOff',
        [{attribute: 'onOff', minimumReportInterval: 0, maximumReportInterval: 0}]],
        ['read', 'genOnOff', ['onOff']]]);
});

test('every control preserves endpoint routing and every expose has an icon', () => {
    const properties = new Set();
    for (const exp of all) {
        assert.match(exp.homeassistant.icon, /^mdi:/);
        if (exp.category !== 'diagnostic') assert.ok(exp.endpoint, exp.name);
        if (exp.property) {assert.ok(!properties.has(exp.property), exp.property); properties.add(exp.property);}
    }
});
test('thermal ranges are six discoverable numeric controls with get support', async () => {
    const ranges = all.filter(x => x.name?.includes('region_range'));
    assert.equal(ranges.length, 6);
    for (const exp of ranges) {
        assert.equal(exp.type, 'numeric'); assert.equal(exp.value_step, 1); assert.equal(exp.category, 'config');
        const ext = extensions.find(x => x.exposes?.includes(exp));
        const converter = ext.toZigbee[0];
        const meta = {device, endpoint_name: exp.endpoint, mapped: model};
        const entity = device.getEndpoint(3);
        await converter.convertSet(entity, exp.name, exp.value_min + 1, meta);
        const write = writes.at(-1);
        assert.equal(write.ID, 3); assert.equal(write.cluster, 'genOnOff');
        const value = Object.values(write.payload)[0];
        assert.equal(value.type, 0x29); assert.equal(value.value, (exp.value_min + 1) * 100);
        assert.equal(write.options?.manufacturerCode, undefined);
        await converter.convertGet(entity, exp.name, meta);
        assert.equal(writes.at(-1).ID, 3);
    }
});
test('numeric calibration reports cannot overwrite the other sensor', () => {
    const meta = {device, mapped: model};
    for (const name of ['radiator_temperature_calibration', 'ambient_temperature_calibration']) {
        const ext = extensions.find(x => x.exposes?.some(e => e.name === name));
        const exp = ext.exposes[0];
        const ID = endpoints(device)[exp.endpoint];
        const msg = {endpoint: {ID}, data: {localTemperatureCalibration: 12}};
        assert.deepEqual(ext.fromZigbee[0].convert(model, msg, null, {}, meta), {[exp.property]: 1.2});
        msg.endpoint.ID = ID === 1 ? 4 : 1;
        assert.equal(ext.fromZigbee[0].convert(model, msg, null, {}, meta), undefined);
    }
});
test('all twenty-one internal animations and off write to EP2 with standard framing', async () => {
    for (const name of ['status_led_idle_animation','status_led_connection_animation','status_led_fault_animation']) {
        const ext = extensions.find(x => x.exposes?.some(e => e.name === name));
        const exp = ext.exposes[0];
        assert.equal(exp.values.length, 22);
        const values = [];
        for (const value of exp.values) {
            await ext.toZigbee[0].convertSet(device.getEndpoint(2), name, value,
                {device, endpoint_name: exp.endpoint, mapped: model});
            const write = writes.at(-1); assert.equal(write.ID, 2);
            assert.equal(write.options?.manufacturerCode, undefined);
            values.push(Object.values(write.payload)[0].value);
        }
        assert.deepEqual(values.sort((a,b)=>a-b), [...Array(21).keys(),255]);
    }
});

test('source status labels are Turkish while numeric routes and properties remain stable', () => {
    const routes = [
        ['object_temperature_source_status',0x805d,['Normal','Dahili Kaynağa Dönüldü','Geçersiz Ölçüm']],
        ['ambient_temperature_source_status',0x805e,['Normal','Dahili Kaynağa Dönüldü','Geçersiz Ölçüm']],
        ...[1,2,3].map(slot=>[`external_temperature_${slot}_status`,0x805e+slot,['İlk Ölçüm Bekleniyor','Güncel','Güncelliğini Yitirdi']]),
    ];
    for (const [name,attribute,labels] of routes) {
        const extension=extensions.find(item=>item.exposes?.some(expose=>expose.name===name));
        assert.ok(extension);
        const expose=extension.exposes.find(item=>item.name===name);
        assert.equal(expose.property,`${name}_climate`);
        assert.deepEqual(expose.values,labels);
        labels.forEach((label,value)=>assert.deepEqual(extension.fromZigbee[0].convert(model,
            {endpoint:{ID:1},data:{[attribute]:value}},null,{}, {device}),{[expose.property]:label}));
    }
    const boot=all.find(item=>item.name==='thermal_led_startup_animation');
    assert.match(boot.description,/Fabrika varsayılanı: Gökkuşağı/);
});

test('external and internal animation controls share every option and order while retaining old IDs', async () => {
    const internal = all.find(exp => exp.name === 'status_led_idle_animation');
    const extension = extensions.find(ext => ext.exposes?.some(exp => exp.name === 'thermal_led_startup_animation'));
    assert.ok(extension, 'external startup animation control');
    const external = extension.exposes[0];
    assert.deepEqual(external.values, internal.values);
    const oldIds = new Map([['Kapalı',0],['Nefes Alma',1],['Gökkuşağı',2],['Hızlı Nabız',3],
        ['Mum Işığı',4],['Uyarı Flaşörü',5],['Açılıp Kapanma',6],['Renk Döngüsü',7]]);
    const ids = [];
    for (const value of external.values) {
        await extension.toZigbee[0].convertSet(device.getEndpoint(3), external.name, value,
            {device, endpoint_name: external.endpoint, mapped: model});
        const write = writes.at(-1);
        assert.equal(write.ID, 3);
        const id = Object.values(write.payload)[0].value;
        ids.push(id);
        if (oldIds.has(value)) assert.equal(id, oldIds.get(value));
        assert.equal(write.options?.manufacturerCode, undefined);
    }
    assert.equal(new Set(ids).size, 22);
    assert.deepEqual(ids.toSorted((a,b)=>a-b), [...Array(23).keys()].filter(id=>id!==8));
});

test('telemetry configuration uses persisted intervals for all six endpoints', async () => {
    const ext = extensions.find(x => x.exposes?.some(e => e.name === 'system_health'));
    const configured = [];
    const intervals = [7, 21, 31, 181, 361, 599];
    const hardware = {getEndpoint(ID) {return {
        async read(cluster) {return cluster === 'hvacUserInterfaceCfg'
            ? Object.fromEntries(intervals.map((n,i)=>[0x8030+i,n])) : {};},
        async bind() {},
        async write(cluster, attributes, options) {
            assert.equal(ID,1); assert.equal(cluster,"hvacUserInterfaceCfg");
            assert.deepEqual(attributes,{0x8036:{value:0,type:0x20}});
            assert.equal(options.manufacturerCode,null);
            assert.equal(configured.length,6);
        },
        async configureReporting(cluster, records) {configured.push({ID,cluster,...records[0]});},
    };}};
    await ext.configure[0](hardware, {});
    assert.deepEqual(configured.map(c=>c.ID), [1,4,5,6,7,8]);
    assert.deepEqual(configured.map(c=>c.maximumReportInterval), intervals);
    assert.ok(configured.every(c=>c.minimumReportInterval===1));
});

test('every custom scalar writes with standard framing to its declared endpoint', async () => {
    for (const ext of extensions) {
        for (const exp of ext.exposes ?? []) {
            if (!(exp.access & 2) || !['enum','numeric'].includes(exp.type) || !exp.endpoint) continue;
            const converter = ext.toZigbee?.find(c=>c.key?.includes(exp.name === 'color_xy' ? exp.property.replace(/_thermal_indicator$/, '') : exp.name));
            if (!converter) continue;
            const value = exp.type === 'enum' ? exp.values[0] : exp.value_min;
            const ID = endpoints(device)[exp.endpoint];
            const start = writes.length;
            await converter.convertSet(device.getEndpoint(ID), exp.name, value,
                {device,endpoint_name:exp.endpoint,mapped:model});
            const emitted = writes.slice(start);
            assert.ok(emitted.length, exp.name);
            for (const write of emitted) {
                assert.equal(write.ID, ID, exp.name);
                assert.equal(write.options?.manufacturerCode, undefined, exp.name);
            }
        }
    }
});

test('LED modes are the only power controls and brightness remains independently writable', async () => {
    assert.equal(all.filter(e=>['light','switch'].includes(e.type)).length, 0);
    const exp=all.find(e=>e.name==='brightness' && e.endpoint==='thermal_indicator');
    assert.ok(exp); assert.equal(exp.type,'numeric');
    assert.equal(exp.unit,'%'); assert.equal(exp.value_min,0); assert.equal(exp.value_max,100);
    const ext=extensions.find(x=>x.exposes?.includes(exp));
    for (const percent of [0, 1, 50, 80, 100]) {
        await ext.toZigbee[0].convertSet(device.getEndpoint(3),'brightness',percent,
            {device,endpoint_name:exp.endpoint,mapped:model});
        const write = writes.at(-1);
        assert.equal(write.ID,3); assert.equal(write.cluster,'genLevelCtrl');
        assert.equal(write.command,'moveToLevel');
        assert.equal(write.payload.level,Math.round(percent * 254 / 100));
        assert.equal(write.payload.optionsMask,1); assert.equal(write.payload.optionsOverride,1);
        const converted = ext.fromZigbee[0].convert(model,
            {endpoint: {ID: 3}, data: {currentLevel: write.payload.level}}, null, {}, {device});
        assert.deepEqual(converted, {[exp.property]: percent});
    }
    await assert.rejects(ext.toZigbee[0].convertSet(device.getEndpoint(3),'brightness',101,
        {device,endpoint_name:exp.endpoint,mapped:model}));
});
test('fan thresholds expose the requested 20 to 99 Celsius range', () => {
    for(const name of ['fan_on_threshold','fan_off_threshold']) {
        const exp=all.find(e=>e.name===name);
        assert.equal(exp.value_min,20);assert.equal(exp.value_max,99);
    }
});

test('both calibration sliders use one tenth of a Celsius degree', () => {
    for (const name of ['radiator_temperature_calibration','ambient_temperature_calibration']) {
        assert.equal(all.find(exp=>exp.name===name).value_step,0.1);
    }
});
test('zero reporting uses the ZCL disable sentinel on every endpoint', async () => {
    const ext = extensions.find(x => x.exposes?.some(e => e.name === 'system_health'));
    const configured = [];
    const hardware = {getEndpoint(ID) {return {
        async read(cluster) {return cluster === 'hvacUserInterfaceCfg'
            ? Object.fromEntries(Array.from({length:6},(_,i)=>[0x8030+i,0])) : {};},
        async bind() {},
        async write(cluster, attributes, options) {
            assert.equal(ID,1); assert.equal(cluster,"hvacUserInterfaceCfg");
            assert.deepEqual(attributes,{0x8036:{value:0,type:0x20}});
            assert.equal(options.manufacturerCode,null);
            assert.equal(configured.length,6);
        },
        async configureReporting(cluster, records) {configured.push({ID,...records[0]});},
    };}};
    await ext.configure[0](hardware, {});
    assert.deepEqual(configured.map(c=>c.ID), [1,4,5,6,7,8]);
    assert.ok(configured.every(c=>c.maximumReportInterval===0xffff));
    const controls = all.filter(exp=>exp.name?.startsWith('report_interval_'));
    assert.equal(controls.length,6);
    assert.ok(controls.every(exp=>exp.value_min===0 && exp.value_step===1));
});
test('internal brightness exposes percent and scales to EP2 uint8', async () => {
    const name = 'status_led_brightness';
    const ext = extensions.find(x => x.exposes?.some(e => e.name === name));
    const exp = ext.exposes[0];
    assert.equal(exp.endpoint, 'status_indicator');
    assert.equal(exp.unit, '%'); assert.equal(exp.value_min, 0);
    assert.equal(exp.value_max, 100); assert.equal(exp.value_step, 1);
    for (const percent of [0, 1, 50, 80, 100]) {
        await ext.toZigbee[0].convertSet(device.getEndpoint(2), name, percent,
            {device, endpoint_name: exp.endpoint, mapped: model});
        const write = writes.at(-1);
        assert.equal(write.ID, 2); assert.equal(write.cluster, 'genOnOff');
        assert.deepEqual(write.payload[0x8004], {value: Math.round(percent * 255 / 100), type: 0x20});
        const converted = ext.fromZigbee[0].convert(model,
            {endpoint: {ID: 2}, data: {0x8004: write.payload[0x8004].value}}, null, {}, {device});
        assert.deepEqual(converted, {[exp.property]: percent});
    }
    await assert.rejects(ext.toZigbee[0].convertSet(device.getEndpoint(2), name, 101,
        {device, endpoint_name: exp.endpoint, mapped: model}));
});
test('OLED controls route to EP9 and preserve setting names', async () => {
    for (const name of ['display_mode', 'display_sleep_timeout']) {
        const ext = extensions.find(x => x.exposes?.some(e => e.name === name));
        const exp = ext.exposes[0];
        assert.equal(exp.endpoint, 'display');
        assert.equal(exp.name, name);
        const value = name === 'display_mode' ? 'Otomatik' : 20;
        await ext.toZigbee[0].convertSet(device.getEndpoint(9), name, value,
            {device, endpoint_name: exp.endpoint, mapped: model});
        assert.equal(writes.at(-1).ID, 9);
        assert.equal(writes.at(-1).cluster, 'hvacUserInterfaceCfg');
        await ext.toZigbee[0].convertGet(device.getEndpoint(9), name,
            {device, endpoint_name: exp.endpoint, mapped: model});
        assert.equal(writes.at(-1).ID, 9);
    }
});

test('CPU profiles use the system endpoint and preserve OTA endpoint identity', async () => {
    assert.equal(endpoints(device).system, 10);
    const ext = extensions.find(x => x.exposes?.some(e => e.name === 'processor_power_profile'));
    const exp = ext.exposes[0];
    assert.equal(exp.endpoint, 'system');
    assert.deepEqual(exp.values, ['Tasarruf', 'Dengeli', 'Performans']);
    for (const [value, mode] of [['Tasarruf', 2], ['Dengeli', 1], ['Performans', 0]]) {
        await ext.toZigbee[0].convertSet(device.getEndpoint(10), exp.name, value,
            {device, endpoint_name: exp.endpoint, mapped: model});
        const write = writes.at(-1);
        assert.equal(write.ID, 10); assert.equal(write.cluster, 'hvacUserInterfaceCfg');
        assert.equal(write.payload[0x8041].value, mode);
        assert.equal(write.payload[0x8041].type, 0x20);
    }
    await ext.toZigbee[0].convertGet(device.getEndpoint(10), exp.name,
        {device, endpoint_name: exp.endpoint, mapped: model});
    assert.equal(writes.at(-1).ID, 10);
    assert.deepEqual(writes.at(-1).attributes, [0x8041]);
});

test('radio TX profiles expose all presets on EP10 and roundtrip uint8', async () => {
    const ext = extensions.find(x => x.exposes?.some(e => e.name === 'radio_tx_profile'));
    const exp = ext.exposes[0];
    assert.equal(exp.endpoint, 'system');
    assert.deepEqual(exp.values, ['Eco', 'Normal', 'Boost']);
    for (const [mode, value] of exp.values.entries()) {
        await ext.toZigbee[0].convertSet(device.getEndpoint(10), exp.name, value,
            {device, endpoint_name: exp.endpoint, mapped: model});
        const write = writes.at(-1);
        assert.equal(write.ID, 10);
        assert.equal(write.payload[0x8042].value, mode);
        assert.equal(write.payload[0x8042].type, 0x20);
    }
    await ext.toZigbee[0].convertGet(device.getEndpoint(10), exp.name,
        {device, endpoint_name: exp.endpoint, mapped: model});
    assert.equal(writes.at(-1).ID, 10);
    assert.deepEqual(writes.at(-1).attributes, [0x8042]);
});

test('temperature display preference is not exposed or writable through HA', () => {
    assert.equal(all.some(e => e.name === 'temperature_display_unit'), false);
    assert.equal(extensions.some(e => e.toZigbee?.some(c => c.key?.includes('temperature_display_unit'))), false);
});

test('system commands route to system endpoint without moving OTA', async () => {
    const extension = extensions.find(x => x.exposes?.some(e => e.name === 'system_command'));
    const expose = extension.exposes[0];
    assert.equal(expose.endpoint, 'system');
    await extension.toZigbee[0].convertSet(device.getEndpoint(10), expose.name, expose.values[1],
        {device, endpoint_name: expose.endpoint, mapped: model});
    const write = writes.at(-1);
    assert.equal(write.ID, 10);
    assert.equal(write.payload[0x8040].value, 1);
    assert.equal(write.payload[0x8040].type, 0x20);
});

test('each report interval belongs to its telemetry endpoint and writes zero', async () => {
    const names = ['radiator', 'ambient', 'processor', 'health', 'memory', 'uptime'];
    for (const [index, name] of names.entries()) {
        const extension = extensions.find(x => x.exposes?.some(e => e.name === `report_interval_${name}_s`));
        const expose = extension.exposes[0];
        const endpoint = [1,4,5,6,7,8][index];
        assert.equal(endpoints(device)[expose.endpoint], endpoint);
        await extension.toZigbee[0].convertSet(device.getEndpoint(endpoint), expose.name, 0,
            {device, endpoint_name: expose.endpoint, mapped: model});
        assert.equal(writes.at(-1).ID, endpoint);
        assert.deepEqual(writes.at(-1).payload[0x8030 + index], {value:0, type:0x21});
    }
});

test('presentation hides endpoint labels without changing route properties',()=>{
    const inspect = expose => {assert.equal(JSON.parse(JSON.stringify(expose)).endpoint,undefined); for (const child of expose.features??[]) inspect(child);};
    all.forEach(inspect);
    const colors=all.filter(expose=>expose.name==='color_xy');
    assert.equal(colors.length,5);
    assert.equal(new Set(colors.map(expose=>expose.property)).size,5);
});

test('frontend normalized XY selection maps to checked wire coordinates',async()=>{
 const ext=extensions.find(x=>x.exposes?.some(e=>e.property==='cold_region_color_xy_thermal_indicator'));
 await ext.toZigbee[0].convertSet(device.getEndpoint(3),'cold_region_color_xy',{x:0.25,y:0.5},{device});
 assert.equal(writes.at(-2).payload[0x8030].value,16384+32768*65536);
 for(const value of [{x:NaN,y:0.5},{x:0.25,y:0},{x:0.5,y:Infinity},{x:0.1,y:0.00000001}])
  await assert.rejects(ext.toZigbee[0].convertSet(device.getEndpoint(3),'cold_region_color_xy',value,{device}));
});

test('region pickers replace presets in their original order',()=>{
 assert.equal(all.some(e=>e.name.endsWith('_region_color_preset')),false);
 const names=all.map(e=>e.property);
 for(const zone of ['cold','warm','hot']) {
  const upper=names.indexOf(`${zone}_region_range_upper_bound_thermal_indicator`);
  assert.equal(names[upper+1],`${zone}_region_color_xy_thermal_indicator`);
 }
 const hot=names.indexOf('hot_region_color_xy_thermal_indicator');
 assert.deepEqual(names.slice(hot+1,hot+5),['danger_threshold_thermal_indicator','danger_region_color_xy_thermal_indicator','freeze_threshold_thermal_indicator','freeze_region_color_xy_thermal_indicator']);
 assert.ok(hot>10);
 for(const expose of all.filter(e=>e.name==='color_xy')) assert.ok(!expose.label.includes('Serbest'));
});

test('fan sources and signed deltas use checked integers and invalid measurements remain null', async()=>{
    const get = name => extensions.find(x=>x.exposes?.some(e=>e.name===name));
    const delta=get('fan_on_temperature_difference');
    assert.equal(delta.exposes[0].value_min,-9);assert.equal(delta.exposes[0].value_max,9);assert.equal(delta.exposes[0].value_step,.1);
    await delta.toZigbee[0].convertSet(device.getEndpoint(1),'fan_on_temperature_difference',-1,{device});
    assert.deepEqual(writes.at(-1).payload[0x8053],{value:-10,type:0x29});
    const before=writes.length;
    for(const value of [NaN,Infinity,9.1,-9.1,1.05]) await assert.rejects(delta.toZigbee[0].convertSet(device.getEndpoint(1),'fan_on_temperature_difference',value,{device}));
    assert.equal(writes.length,before);
    const input=get('external_temperature_1');
    assert.equal(input.exposes[0].category,undefined);
    await input.toZigbee[0].convertSet(device.getEndpoint(1),'external_temperature_1',21.7,{device});
    assert.deepEqual(writes.at(-1).payload[0x8055],{value:2170,type:0x29});
    await input.toZigbee[0].convertSet(device.getEndpoint(1),'external_temperature_1',null,{device});
    assert.deepEqual(writes.at(-1).payload[0x8055],{value:-32768,type:0x29});
    assert.deepEqual(input.fromZigbee[0].convert(model,{endpoint:{ID:1},data:{[0x8055]:-32768}}),{external_temperature_1_climate:null});
    const effective=get('temperature_difference');
    assert.deepEqual(effective.fromZigbee[0].convert(model,{endpoint:{ID:1},data:{[0x805a]:-200}}),{temperature_difference_climate:-2});
});

test('presentation groups cover every expose once and match the displayed order',async()=>{
 const source=await import('../../../tools/racool.mjs');
 const properties=source.presentationGroups.flatMap(([,keys])=>keys);
 const actual=source.default.extend.flatMap(e=>e.exposes??[]).map(e=>e.property);
 assert.equal(new Set(properties).size,properties.length);
 assert.deepEqual(actual,properties);
});


test('unavailable startup telemetry clears stale values without replacing real zeros', () => {
    for (const [id, role] of [[1,'climate'],[4,'ambient_sensor'],[5,'processor_sensor']]) {
        const extension=extensions.find(item=>item.exposes?.some(expose=>expose.property===`temperature_${role}`));
        const convert=extension.fromZigbee[0].convert;
        for (const invalid of [-32768,32768,NaN,Infinity,'0']) {
            assert.deepEqual(convert(model,{endpoint:{ID:id},data:{measuredValue:invalid}}),{[`temperature_${role}`]:null});
        }
        for (const valid of [0,2750,-100,-32767,32767]) {
            assert.deepEqual(convert(model,{endpoint:{ID:id},data:{measuredValue:valid}}),{[`temperature_${role}`]:valid/100});
        }
        assert.equal(convert(model,{endpoint:{ID:99},data:{measuredValue:0}}),undefined);
        assert.equal(convert(model,{endpoint:{ID:id},data:{}}),undefined);
    }
    const analog=extensions.find(item=>item.exposes?.some(expose=>expose.name==='system_health')).fromZigbee[0].convert;
    for (const invalid of [-1,101,NaN,Infinity,'0']) assert.deepEqual(analog(model,{endpoint:{ID:6},data:{presentValue:invalid}}),{system_health:null});
    for (const valid of [0,50,100]) assert.deepEqual(analog(model,{endpoint:{ID:6},data:{presentValue:valid}}),{system_health:valid});
});

test('external startup description matches the firmware factory default', () => {
    const expose = all.find(item => item.name === 'thermal_led_startup_animation');
    assert.ok(expose);
    assert.match(expose.description, /Fabrika varsayılanı: Gökkuşağı/);
    assert.doesNotMatch(expose.description, /Varsayılan: Kapalı/);
});

test('CLI and OLED configuration reports have coordinator bindings on every setting route', async () => {
    const configure = extensions.flatMap(extension => extension.configure ?? [])
        .find(callback => callback.name === 'configureConfigurationBindings');
    assert.equal(typeof configure, 'function');
    const bound = [];
    const coordinator = {};
    await configure({getEndpoint(ID) {return {async bind(cluster, target) {
        assert.equal(target, coordinator);
        bound.push([ID, cluster]);
    }};}}, coordinator);
    assert.deepEqual(bound, [[1, 'hvacThermostat'], [1, 'hvacUserInterfaceCfg'],
        [2, 'genOnOff'], [3, 'genOnOff'], [3, 'genLevelCtrl'], [4, 'hvacThermostat'],
        ...[4, 5, 6, 7, 8, 9, 10].map(endpoint => [endpoint, 'hvacUserInterfaceCfg'])]);
    await assert.rejects(configure({getEndpoint() {return {async bind() {
        throw Error('binding rejected');
    }};}}, coordinator), /binding rejected/);
});
