import * as m from 'zigbee-herdsman-converters/lib/modernExtend';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';

const e = exposes.presets;
const ea = exposes.access;

// Unknown samples clear stale state; a real zero remains a valid measurement.
function temperatureReadback(endpointId, endpointName) {
    return (_model, message) => {
        if (message.endpoint.ID !== endpointId || message.data.measuredValue === undefined) return;
        const raw = message.data.measuredValue;
        const valid = typeof raw === 'number' && Number.isInteger(raw) && raw >= -32767 && raw <= 32767;
        return {[`temperature_${endpointName}`]: valid ? raw / 100 : null};
    };
}

// Presentation order is shared; persisted identifiers remain driver-specific.
const animationCatalog = [
    ['Kapalı', 255, 0],
    ['Kalp Atışı Kırmızı', 0, 9],
    ['Beyaz Yanıp Sönme', 1, 10],
    ['SOS Kırmızı', 2, 11],
    ['Yeşil Nefes', 3, 12],
    ['Mavi Geçiş', 4, 13],
    ['Beyaz Flaş', 5, 14],
    ['Gökkuşağı', 6, 2],
    ['Polis Flaşörü', 7, 15],
    ['Sabit Kırmızı', 8, 16],
    ['Sabit Yeşil', 9, 17],
    ['Sabit Mavi', 10, 18],
    ['Sabit Beyaz', 11, 19],
    ['Kırmızı Yanıp Sönme', 12, 20],
    ['Kırmızı Nefes', 13, 21],
    ['Çift Beyaz Yanıp Sönme', 14, 22],
    ['Nefes Alma', 15, 1],
    ['Hızlı Nabız', 16, 3],
    ['Mum Işığı', 17, 4],
    ['Uyarı Flaşörü', 18, 5],
    ['Açılıp Kapanma', 19, 6],
    ['Renk Döngüsü', 20, 7],
];
const internalAnimLookup = Object.fromEntries(animationCatalog.map(([label, internal]) => [label, internal]));

const internalLedModeLookup = {
    'Otomatik': 1,
    'Kapalı': 0,
};

const extBootAnimLookup = Object.fromEntries(animationCatalog.map(([label, , external]) => [label, external]));

const extLedModeLookup = {
    'Otomatik': 0,
    'Kapalı': 2,
};

const fanModeLookup = {
    'Kapalı': 2,
    'Nesneye duyarlı': 0,
    'Ortama duyarlı': 3,
    'Sürekli Açık': 1,
};
const temperatureSourceLookup = {'Dahili': 0, 'Harici 1': 1, 'Harici 2': 2, 'Harici 3': 3};
const sourceHealthLookup = {'Normal': 0, 'Dahili Kaynağa Dönüldü': 1, 'Geçersiz Ölçüm': 2};
const externalStateLookup = {'İlk Ölçüm Bekleniyor': 0, 'Güncel': 1, 'Güncelliğini Yitirdi': 2};
function fanSourceNumeric(args) {
    const extension = m.numeric(args);
    const expose = extension.exposes[0];
    const measurement = args.attribute.ID >= 0x8055;
    if (measurement) extension.fromZigbee[0].convert = (model, msg) => {
        if (msg.endpoint.ID !== 1) return;
        const raw = msg.data[args.attribute.ID];
        if (!Number.isInteger(raw) || raw < -32768 || raw > 32767) return;
        return {[expose.property]: raw === -32768 ? null : raw / 100};
    };
    const setter = extension.toZigbee[0];
    if (setter.convertSet) setter.convertSet = async (entity, key, value, meta) => {
        const external = args.attribute.ID >= 0x8055 && args.attribute.ID <= 0x8057;
        if (external && value === null) {
            await meta.device.getEndpoint(1).write(args.cluster, {[args.attribute.ID]: {value: -32768, type: 0x29}}, {});
            return {state: {[key]: null}};
        }
        if (typeof value !== 'number' || !Number.isFinite(value) || value < args.valueMin || value > args.valueMax)
            throw Error('Temperature must be finite and within its declared bounds');
        const scale = args.scale;
        if (!measurement && Math.abs(value * scale - Math.round(value * scale)) > 1e-6)
            throw Error('Temperature difference requires 0.1 Celsius resolution');
        const raw = Math.round(value * scale);
        await meta.device.getEndpoint(1).write(args.cluster, {[args.attribute.ID]: {value: raw, type: 0x29}}, {});
        return {state: {[key]: raw / scale}};
    };
    return extension;
}
function fanTemperatureSources() {
    const endpointName = 'climate';
    const cluster = 'hvacUserInterfaceCfg';
    return [
        ...[['object_temperature_source', 'Nesne Sıcaklığı Kaynağı', 0x8051],
            ['ambient_temperature_source', 'Ortam Sıcaklığı Kaynağı', 0x8052]].map(([name, label, ID]) =>
            m.enumLookup({name, label, lookup: temperatureSourceLookup, cluster, attribute: {ID, type: 0x20}, endpointName,
                description: 'Harici kaynak 5 dakika güncellenmezse ilgili dahili MLX ölçümüne dönülür; kaynak seçimi korunur.'})),
        ...[['fan_on_temperature_difference', 'Fan Açılma Sıcaklık Farkı', 0x8053],
            ['fan_off_temperature_difference', 'Fan Kapanma Sıcaklık Farkı', 0x8054]].map(([name, label, ID]) =>
            fanSourceNumeric({name, label, cluster, attribute: {ID, type: 0x29}, endpointNames: [endpointName],
                unit: '°C', valueMin: -9, valueMax: 9, valueStep: 0.1, scale: 10, precision: 1, entityCategory: 'config',
                description: 'Efektif nesne eksi ortam sıcaklık farkı; açılma farkı kapanma farkından büyük olmalıdır.'})),
        ...[1, 2, 3].map(slot => fanSourceNumeric({name: `external_temperature_${slot}`, label: `Harici Sıcaklık Kaynağı #${slot} Ölçümü`,
            cluster, attribute: {ID: 0x8054 + slot, type: 0x29}, endpointNames: [endpointName], unit: '°C',
            valueMin: -50, valueMax: 200, valueStep: 0.1, scale: 100, precision: 2,
            description: 'Ortak harici ölçüm kaynağı: nesne, ortam veya iki rol için seçilebilir. RAM ölçüm girişidir; en az 5 dakikada bir güncellenir, restart sonrası boş başlar. null veriyi geçersiz kılar.'})),
        ...[['effective_object_temperature', 'Fan Kontrolü Nesne Sıcaklığı', 0x8058],
            ['effective_ambient_temperature', 'Fan Kontrolü Ortam Sıcaklığı', 0x8059],
            ['temperature_difference', 'Fan Kontrolü Nesne-Ortam Farkı', 0x805a]].map(([name, label, ID]) =>
            fanSourceNumeric({name, label, cluster, attribute: {ID, type: 0x29}, endpointNames: [endpointName],
                unit: '°C', scale: 100, precision: 2, access: 'STATE_GET', entityCategory: 'diagnostic',
                description: 'Fanın karar verirken kullandığı seçili/dönüş sensörü ölçümü. Fark, kullanılan nesne eksi ortam sıcaklığıdır. Geçersiz veri null gösterilir.'})),
        ...[['object_temperature_effective_source', 'Fanın Kullandığı Nesne Kaynağı', 0x805b, temperatureSourceLookup],
            ['ambient_temperature_effective_source', 'Fanın Kullandığı Ortam Kaynağı', 0x805c, temperatureSourceLookup],
            ['object_temperature_source_status', 'Nesne Kaynak Durumu', 0x805d, sourceHealthLookup],
            ['ambient_temperature_source_status', 'Ortam Kaynak Durumu', 0x805e, sourceHealthLookup],
            ...[1, 2, 3].map(slot => [`external_temperature_${slot}_status`, `Harici Sıcaklık Kaynağı #${slot} Veri Güncelliği`, 0x805e+slot, externalStateLookup])]
            .map(([name, label, ID, lookup]) => m.enumLookup({name, label, lookup, cluster,
                attribute: {ID, type: 0x20}, endpointName, access: 'STATE_GET', entityCategory: 'diagnostic'})),
        {isModernExtend: true, configure: [async (device, coordinatorEndpoint) => {
            // Derived decision diagnostics use this cluster's binding, independently
            // of the six configurable raw telemetry schedules.
            await device.getEndpoint(1).bind(cluster, coordinatorEndpoint);
            await device.getEndpoint(1).read(cluster, Array.from({length:13},(_,index)=>0x8055+index));
        }]},
    ];
}


const displayModeLookup = {
    'Otomatik': 1,
    'Daima Açık': 2,
};

const systemCmdLookup = {
    'Boşta': 0,
    'Yeniden Başlat': 1,
    'Kapat': 2,
    'Derin Uyku': 3,
};

// A single UINT32 keeps each persisted XY pair coherent across one ZCL write.
function zoneColors() {
    const zones = ['cold', 'warm', 'hot', 'danger', 'freeze'];
    const labels = ['Soğuk', 'Ilık', 'Sıcak', 'Tehlike', 'Donma'];
    const names = zones.map(zone => `${zone}_region_color_xy`);
    const endpointName = 'thermal_indicator';
    const property = name => `${name}_${endpointName}`;
    const attribute = index => 0x8030 + index;
    const validate = value => {
        if (!value || typeof value !== 'object' || Array.isArray(value) ||
            Object.keys(value).length !== 2 || !Number.isInteger(value.x) || !Number.isInteger(value.y) ||
            value.x < 0 || value.x > 65279 || value.y < 1 || value.y > 65279) {
            throw new Error('Zone color requires integer x (0–65279) and y (1–65279)');
        }
    };
    return {
        isModernExtend: true,
        exposes: names.map((name, index) => {
            const expose = e.composite('color_xy', name, ea.ALL)
            .withLabel(`RGB ${labels[index]} Bölge Rengi`)
            .withFeature(e.numeric('x', ea.ALL).withValueMin(0).withValueMax(65279 / 65536).withValueStep(1 / 65536))
            .withFeature(e.numeric('y', ea.ALL).withValueMin(1 / 65536).withValueMax(65279 / 65536).withValueStep(1 / 65536))
            .withDescription('Renk seçicisinden kalıcı bölge rengini seçin. Parlaklık ayrı ayardır.')
            .withEndpoint(endpointName).withCategory('config');
            // Nested coordinates belong to the composite object. Only the
            // outer property carries the route suffix.
            for (const feature of expose.features) feature.withProperty(feature.name);
            return expose;
        }),
        fromZigbee: [{cluster: 'genOnOff', type: ['attributeReport', 'readResponse'], convert(model, msg) {
            if (msg.endpoint.ID !== 3) return;
            const result = {};
            names.forEach((name, index) => {
                const packed = msg.data[attribute(index)];
                if (!Number.isInteger(packed) || packed < 0 || packed > 0xffffffff) return;
                const value = {x: packed & 0xffff, y: packed >>> 16};
                if (value.x <= 65279 && value.y > 0 && value.y <= 65279) result[property(name)] = {x:value.x / 65536,y:value.y / 65536};
            });
            if (Object.keys(result).length) return result;
        }}],
        toZigbee: [{key: names, async convertSet(entity, key, value, meta) {
            const index = names.indexOf(key);
            if (index < 0) throw new Error('Unknown color zone');
            // Standard frontend XY is normalized. Legacy integer requests remain valid.
            if (value && typeof value === 'object' && !Array.isArray(value) &&
                Object.keys(value).length === 2 && Number.isFinite(value.x) && Number.isFinite(value.y) &&
                value.x >= 0 && value.x <= 65279 / 65536 && value.y > 0 && value.y <= 65279 / 65536) {
                value = {x:Math.round(value.x * 65536),y:Math.round(value.y * 65536)};
            }
            validate(value);
            const endpoint = meta.device.getEndpoint(3);
            if (!endpoint) throw new Error('RGB endpoint missing');
            await endpoint.write('genOnOff', {[attribute(index)]: {value: (value.x | (value.y << 16)) >>> 0, type: 0x23}}, {manufacturerCode: null});
            await endpoint.read('genOnOff', [attribute(index)], {manufacturerCode: null});
            // State is supplied by the actual read response, not a speculative echo.
        }, async convertGet(entity, key, meta) {
            const index = names.indexOf(key);
            const endpoint = meta.device.getEndpoint(3);
            if (index < 0 || !endpoint) throw new Error('Color zone endpoint missing');
            await endpoint.read('genOnOff', [attribute(index)], {manufacturerCode: null});
        }}],
        configure: [async device => {
            const endpoint = device.getEndpoint(3);
            if (!endpoint) throw new Error('RGB endpoint missing');
            await endpoint.read('genOnOff', names.map((name, index) => attribute(index)), {manufacturerCode: null});
        }],
    };
}

function withCustomLabel(extendObj, label) {
    if (extendObj && extendObj.exposes) {
        extendObj.exposes.forEach((exp) => {
            if (typeof exp.withLabel === 'function') {
                exp.withLabel(label);
            }

            if (exp.features && Array.isArray(exp.features)) {
                exp.features.forEach((feat) => {
                    if (feat.name === 'state') {
                        if (typeof feat.withLabel === 'function') feat.withLabel(exp.endpoint === 'status_indicator' ? 'Dahili LED Durumu' : 'Harici LED Durumu');
                        if (typeof feat.withDescription === 'function') feat.withDescription(exp.endpoint === 'status_indicator' ? 'Dahili durum LED’inin açık veya kapalı olduğunu gösterir.' : 'Harici RGB LED’in açık veya kapalı olduğunu gösterir.');
                    } else if (feat.name === 'brightness') {
                        if (typeof feat.withLabel === 'function') feat.withLabel('RGB Işık Parlaklığı');
                        if (typeof feat.withDescription === 'function') feat.withDescription('Harici RGB bildirim ışığının parlaklık seviyesi.');
                    } else if (feat.name === 'color_xy') {
                        if (typeof feat.withLabel === 'function') feat.withLabel('RGB Işık Rengi');
                        if (typeof feat.withDescription === 'function') feat.withDescription('Harici RGB LED rengi (CIE 1931 XY renk uzayı).');
                    }
                });
            }
        });
    }
    return extendObj;
}

// HA discovers two number entities; generic composites are not range controls.
const regionColors = zoneColors();
function regionColor(index) {
    return {isModernExtend:true, exposes:[regionColors.exposes[index]],
        ...(index === 0 ? {fromZigbee:regionColors.fromZigbee,toZigbee:regionColors.toZigbee,configure:regionColors.configure} : {})};
}

function thermalRange({name, label, description, lowAttribute, highAttribute, valueMin, valueMax, endpointName}) {
    return ['lower_bound', 'upper_bound'].map((suffix, index) => m.numeric({
        name: name + '_' + suffix,
        label: label + (index ? ' Üst Sınır' : ' Alt Sınır'),
        description, cluster: 'genOnOff',
        attribute: {ID: index ? highAttribute : lowAttribute, type: 0x29},
        endpointNames: [endpointName], entityCategory: 'config',
        unit: '°C', valueMin, valueMax, valueStep: 1, scale: 100,
    }));
}

const brightnessScale = maximum => (value, direction) =>
    direction === 'from' ? value * 100 / maximum : Math.round(value * maximum / 100);

function racoolDiagnostics() {
    return {
        isModernExtend: true,
        configure: [async (device, coordinatorEndpoint) => {
            // Apply persisted intervals after the standard temperature extensions.
            const attributes = [0x8030, 0x8031, 0x8032, 0x8033, 0x8034, 0x8035];
            const intervals = await device.getEndpoint(1).read('hvacUserInterfaceCfg', attributes);
            for (const [index, endpointId] of [1, 4, 5, 6, 7, 8].entries()) {
                const endpoint = device.getEndpoint(endpointId);
                const interval = intervals[attributes[index]];
                if (!Number.isInteger(interval) || interval < 0 || interval > 3600) {
                    throw new Error('Invalid reporting interval on endpoint ' + endpointId);
                }
                const cluster = index < 3 ? 'msTemperatureMeasurement' : 'genAnalogInput';
                const attribute = index < 3 ? 'measuredValue' : 'presentValue';
                await endpoint.bind(cluster, coordinatorEndpoint);
                await endpoint.configureReporting(cluster, [{attribute,
                    minimumReportInterval: 1, maximumReportInterval: interval === 0 ? 0xffff : interval,
                    reportableChange: index < 3 ? 50 : 0}]);
                await endpoint.read(cluster, [attribute]);
            }
            // Reapply the device-owned policy after the SDK processes coordinator reporting commands.
            // This refresh command does not overwrite any persisted interval.
            await device.getEndpoint(1).write('hvacUserInterfaceCfg', {0x8036:{value:0,type:0x20}}, {manufacturerCode:null});
        }],
        fromZigbee: [
            {
                cluster: 'genAnalogInput',
                type: ['attributeReport', 'readResponse'],
                convert: (model, msg, publish, options, meta) => {
                    if (msg.data.presentValue !== undefined) {
                        const ep = msg.endpoint.ID;
                        const val = msg.data.presentValue;
                        if (ep === 6) {
                            return {system_health: typeof val === 'number' && Number.isFinite(val) && val >= 0 && val <= 100 ? Math.round(val) : null};
                        } else if (ep === 7) {
                            return { free_memory_percent: parseFloat(Math.max(0, Math.min(100, val)).toFixed(1)) };
                        } else if (ep === 8) {
                            return { uptime_seconds: Math.max(0, Math.round(val)) };
                        }
                    }
                },
            },
        ],
        toZigbee: [
            {
                key: ['system_health'],
                convertGet: async (entity, key, meta) => {
                    const ep = meta.device.getEndpoint(6);
                    if (ep) await ep.read('genAnalogInput', ['presentValue']);
                },
            },
            {
                key: ['free_memory_percent'],
                convertGet: async (entity, key, meta) => {
                    const ep = meta.device.getEndpoint(7);
                    if (ep) await ep.read('genAnalogInput', ['presentValue']);
                },
            },
            {
                key: ['uptime_seconds'],
                convertGet: async (entity, key, meta) => {
                    const ep = meta.device.getEndpoint(8);
                    if (ep) await ep.read('genAnalogInput', ['presentValue']);
                },
            },
        ],
        exposes: [
            e.numeric('system_health', ea.STATE_GET)
                .withLabel('Sistem Sağlık Skoru')
                .withUnit('%')
                .withDescription('Genel sistem sağlık skoru (0 - 100 %). Sensör, bellek ve haberleşme metriklerine dayanır.')
                .withCategory('diagnostic'),
            e.numeric('free_memory_percent', ea.STATE_GET)
                .withLabel('Boş RAM Oranı')
                .withUnit('%')
                .withDescription('Kullanılabilir serbest heap belleği oranı (%). Toplam heap kapasitesi: ~266 KB.')
                .withCategory('diagnostic'),
            e.numeric('uptime_seconds', ea.STATE_GET)
                .withLabel('Çalışma Süresi')
                .withUnit('s')
                .withDescription('Son yeniden başlatmadan bu yana kesintisiz çalışma süresi (Saniye).')
                .withCategory('diagnostic'),
        ],
    };
}

function fanRelayState() {
    const expose = e.binary('fan_relay_state', ea.STATE_GET, 'ON', 'OFF')
        .withEndpoint('climate').withLabel('Fan Durumu')
        .withDescription('Fan için uygulanan aç/kapat durumu. Her değişimde bildirilir; sıcaklık rapor aralığından bağımsızdır. Fanın mekanik dönüşü ölçülmez.')
        .withHomeAssistant({icon: 'mdi:fan'});
    return {
        isModernExtend: true,
        exposes: [expose],
        fromZigbee: [{cluster: 'genOnOff', type: ['attributeReport', 'readResponse'],
            convert: (model, msg) => {
                if (msg.endpoint.ID !== 1) return;
                const value = msg.data.onOff;
                if (value !== 0 && value !== 1 && value !== false && value !== true) return;
                return {[expose.property]: value === 1 || value === true ? 'ON' : 'OFF'};
            }}],
        toZigbee: [{key: [expose.name], convertGet: async (entity, key, meta) => {
            await meta.device.getEndpoint(1).read('genOnOff', ['onOff']);
        }}],
        configure: [async (device, coordinatorEndpoint) => {
            const endpoint = device.getEndpoint(1);
            await endpoint.bind('genOnOff', coordinatorEndpoint);
            await endpoint.configureReporting('genOnOff', [{attribute: 'onOff',
                minimumReportInterval: 0, maximumReportInterval: 0}]);
            // Initial discovery must use an actual attribute read, including OFF.
            await endpoint.read('genOnOff', ['onOff']);
        }],
    };
}

const definition = {
    zigbeeModel: ['RaCool-ESP32C6'],
    model: 'RaCool-ESP32C6',
    vendor: 'RaCool',
    description: 'RaCool ESP32-C6 Akıllı Radyatör Kontrolörü',
    extend: [
        fanRelayState(),
        ...fanTemperatureSources(),
        m.deviceEndpoints({
            endpoints: {
                climate: 1,
                status_indicator: 2,
                thermal_indicator: 3,
                ambient_sensor: 4,
                processor_sensor: 5,
                system_health: 6,
                memory: 7,
                uptime: 8,
                display: 9,
                system: 10,
            }
        }),

        m.enumLookup({
            name: 'fan_operating_mode',
            label: 'Fan Çalışma Modu',
            lookup: fanModeLookup,
            cluster: 'hvacUserInterfaceCfg',
            attribute: {ID: 0x8050, type: 0x20},
            description: 'Nesneye duyarlı: mutlak nesne sıcaklığı; Ortama duyarlı: nesne eksi ortam farkı. Geçersiz otomatik ölçümde fan fail-safe açılır; CPU koruması önceliklidir.',
            endpointName: 'climate',
        }),
        withCustomLabel(
            m.temperature({
                endpointNames: ['climate'],
                fzConvert: temperatureReadback(1, 'climate'),
                reporting: {min: 1, max: 3, change: 50},
                description: 'Radyatör yüzeyinden MLX90614 IR sensörü ile temassız ölçülen radyatör sıcaklığı.',
            }),
            'Radyatör Yüzey Sıcaklığı'
        ),
        withCustomLabel(
            m.temperature({
                endpointNames: ['ambient_sensor'],
                fzConvert: temperatureReadback(4, 'ambient_sensor'),
                reporting: {min: 1, max: 20, change: 50},
                description: 'MLX90614 sensörü ile ölçülen ortam hava sıcaklığı.',
            }),
            'Ortam Sıcaklığı'
        ),

        m.numeric({
            name: 'fan_on_threshold',
            label: 'Fan Açılma Sıcaklığı',
            cluster: 'hvacThermostat',
            attribute: 'occupiedCoolingSetpoint',
            description: 'Fanın radyatörden ısı dağıtımı için devreye gireceği radyatör sıcaklığı eşiği (20 - 99 °C).',
            endpointNames: ['climate'],
            entityCategory: 'config',
            unit: '°C',
            valueMin: 20,
            valueMax: 99,
            scale: 100,
            homeassistant: { icon: 'mdi:thermometer-chevron-up' },
        }),
        m.numeric({
            name: 'fan_off_threshold',
            label: 'Fan Kapanma Sıcaklığı',
            cluster: 'hvacThermostat',
            attribute: 'occupiedHeatingSetpoint',
            description: 'Fanın ısı dağıtımını durduracağı radyatör sıcaklığı eşiği (20 - 99 °C).',
            endpointNames: ['climate'],
            entityCategory: 'config',
            unit: '°C',
            valueMin: 20,
            valueMax: 99,
            scale: 100,
            homeassistant: { icon: 'mdi:thermometer-chevron-down' },
        }),

        m.numeric({
            name: 'radiator_temperature_calibration',
            label: 'Radyatör Sıcaklık Kalibrasyonu',
            cluster: 'hvacThermostat',
            attribute: 'localTemperatureCalibration',
            description: 'MLX90614 temassız radyatör yüzey sıcaklığı sensörü kalibrasyon ofseti (-9.9 ile +9.9 °C; varsayılan 0 °C).',
            endpointNames: ['climate'],
            entityCategory: 'config',
            unit: '°C',
            valueMin: -9.9,
            valueMax: 9.9,
            valueStep: 0.1,
            scale: 10,
            homeassistant: { icon: 'mdi:thermometer-edit' },
        }),
        m.numeric({
            name: 'ambient_temperature_calibration',
            label: 'Ortam Sıcaklık Kalibrasyonu',
            cluster: 'hvacThermostat',
            attribute: 'localTemperatureCalibration',
            description: 'MLX90614 ortam hava sıcaklığı sensörü kalibrasyon ofseti (-9.9 ile +9.9 °C; varsayılan 0 °C).',
            endpointNames: ['ambient_sensor'],
            entityCategory: 'config',
            unit: '°C',
            valueMin: -9.9,
            valueMax: 9.9,
            valueStep: 0.1,
            scale: 10,
            homeassistant: { icon: 'mdi:thermometer-edit' },
        }),

        m.numeric({
            name: 'brightness', label: 'Harici RGB LED Parlaklığı',
            cluster: 'genLevelCtrl', attribute: 'currentLevel',
            endpointNames: ['thermal_indicator'], entityCategory: 'config',
            description: 'Harici LED parlaklığı. Açma ve kapatma çalışma modu seçicisinden yönetilir.',
            unit: '%', valueMin: 0, valueMax: 100, valueStep: 1,
            scale: brightnessScale(254), precision: 0,
            homeassistant: {icon: 'mdi:brightness-6'},
        }),
        m.enumLookup({
            name: 'thermal_led_mode',
            label: 'Harici RGB LED Çalışma Modu',
            lookup: { 'Otomatik': 0, 'Kapalı': 2 },
            cluster: 'genOnOff',
            attribute: { ID: 0x8013, type: 0x20 },
            description: 'Harici RGB LED modu: Otomatik, radyatör yüzey sıcaklığına göre renk gösterir; Kapalı, LED’i kapatır.',
            endpointName: 'thermal_indicator',
            entityCategory: 'config',
            
        }),
        m.numeric({
            name: 'status_led_brightness',
            label: 'Dahili LED Parlaklığı',
            description: 'Dahili LED için kalıcı parlaklık; 0 ışık çıkışını kapatır, çalışma modu korunur.',
            cluster: 'genOnOff', attribute: {ID: 0x8004, type: 0x20},
            endpointNames: ['status_indicator'], entityCategory: 'config',
            unit: '%', valueMin: 0, valueMax: 100, valueStep: 1,
            scale: brightnessScale(255), precision: 0,
        }),
        m.enumLookup({
            name: 'status_led_mode',
            label: 'Dahili LED Çalışma Modu',
            lookup: internalLedModeLookup,
            cluster: 'genOnOff',
            attribute: { ID: 0x8001, type: 0x20 },
            description: 'Dahili LED modu: Otomatik, sistem durumuna göre seçili animasyonu çalıştırır; Kapalı LED’i susturur.',
            endpointName: 'status_indicator',
            entityCategory: 'config',
            
        }),
        m.enumLookup({
            name: 'thermal_led_startup_animation',
            label: 'Harici RGB LED Başlangıç Animasyonu',
            lookup: extBootAnimLookup,
            cluster: 'genOnOff',
            attribute: { ID: 0x8014, type: 0x20 },
            description: 'Harici RGB LED açılış sırasında oynatılacak başlangıç animasyonu (Fabrika varsayılanı: Kapalı). Açılış tamamlandıktan sonra LED kesintisiz olarak MLX90614 sıcaklık görselleştirmesine geçer.',
            endpointName: 'thermal_indicator',
            entityCategory: 'config',
            
        }),
        ...thermalRange({ name: 'cold_region_range', label: 'RGB Soğuk Bölge Aralığı', description: 'Radyatör soğuk bölgesinin alt ve üst sıcaklık sınırları.', lowAttribute: 0x8020, highAttribute: 0x8021, valueMin: 7, valueMax: 25, endpointName: 'thermal_indicator' }),
        regionColor(0),
        ...thermalRange({ name: 'warm_region_range', label: 'RGB Ilık Bölge Aralığı', description: 'Radyatör ılık bölgesinin alt ve üst sıcaklık sınırları.', lowAttribute: 0x8022, highAttribute: 0x8023, valueMin: 26, valueMax: 44, endpointName: 'thermal_indicator' }),
        regionColor(1),
        ...thermalRange({ name: 'hot_region_range', label: 'RGB Sıcak Bölge Aralığı', description: 'Radyatör sıcak bölgesinin alt ve üst sıcaklık sınırları.', lowAttribute: 0x8024, highAttribute: 0x8025, valueMin: 45, valueMax: 59, endpointName: 'thermal_indicator' }),
        regionColor(2),
        m.numeric({ name: 'danger_threshold', label: 'RGB Tehlike Sınırı', cluster: 'genOnOff', attribute: { ID: 0x8026, type: 0x29 }, description: 'Radyatör yüzeyinde tehlikenin başladığı sıcaklık.', endpointNames: ['thermal_indicator'], entityCategory: 'config', unit: '°C', valueMin: 60, valueMax: 99, valueStep: 1, scale: 100 }),
        regionColor(3),
        m.numeric({ name: 'freeze_threshold', label: 'RGB Donma Sınırı', cluster: 'genOnOff', attribute: { ID: 0x8027, type: 0x29 }, description: 'Radyatör suyunun donma tehlikesinin başladığı sıcaklık.', endpointNames: ['thermal_indicator'], entityCategory: 'config', unit: '°C', valueMin: -18, valueMax: 6, valueStep: 1, scale: 100 }),
        regionColor(4),

        m.enumLookup({
            name: 'status_led_idle_animation',
            label: 'Dahili LED Boşta Animasyonu',
            lookup: internalAnimLookup,
            cluster: 'genOnOff',
            attribute: { ID: 0x8000, type: 0x20 },
            description: 'Cihaz normal işletimde ve boşta (IDLE / HEATING) iken dahili durum LED davranışı (Donanım varsayılanı: Double Blink in White).',
            endpointName: 'status_indicator',
            entityCategory: 'config',
            
        }),
        m.enumLookup({
            name: 'status_led_startup_animation',
            label: 'Dahili LED Başlangıç Animasyonu',
            lookup: internalAnimLookup,
            cluster: 'genOnOff', attribute: {ID: 0x8005, type: 0x20},
            endpointName: 'status_indicator', entityCategory: 'config',
            description: 'Dahili LED açılış efekti; harici LED seçimi ve oynatıcısından bağımsızdır. Global Kapalı modu korunur.',
            homeassistant: {icon: 'mdi:animation-play'},
        }),
        m.enumLookup({
            name: 'status_led_connection_animation',
            label: 'Dahili LED Bağlantı Animasyonu',
            lookup: internalAnimLookup,
            cluster: 'genOnOff',
            attribute: { ID: 0x8002, type: 0x20 },
            description: 'Cihaz Zigbee ağına bağlanırken veya yeniden bağlantı ararken (CONNECTING / REJOIN) dahili durum LED davranışı (Donanım varsayılanı: Heartbeat Red).',
            endpointName: 'status_indicator',
            entityCategory: 'config',
            
        }),
        m.enumLookup({
            name: 'status_led_fault_animation',
            label: 'Dahili LED Arıza Animasyonu',
            lookup: internalAnimLookup,
            cluster: 'genOnOff',
            attribute: { ID: 0x8003, type: 0x20 },
            description: 'Sistem hatası, sensör arızası veya alarm durumlarında (FAULT / ERROR / ALARM) dahili durum LED uyarısı (Donanım varsayılanı: Police Strobe).',
            endpointName: 'status_indicator',
            entityCategory: 'config',
            
        }),

        m.enumLookup({
            name: 'display_mode',
            label: 'OLED Ekran Modu',
            lookup: displayModeLookup,
            cluster: 'hvacUserInterfaceCfg',
            attribute: { ID: 0x8020, type: 0x20 },
            description: 'OLED ekran modu: Otomatik=Hareketsizlik sonrası uyku modu, Daima Açık=Ekran sürekli açık kalır.',
            endpointName: 'display',
            entityCategory: 'config',
            
        }),
        m.numeric({
            name: 'display_sleep_timeout',
            label: 'Ekran Uyku Süresi',
            cluster: 'hvacUserInterfaceCfg',
            attribute: { ID: 0x8021, type: 0x21 },
            description: 'Otomatik modda ekranın kapanacağı hareketsizlik süresi (5 - 60 saniye).',
            endpointNames: ['display'],
            entityCategory: 'config',
            unit: 's',
            valueMin: 5,
            valueMax: 60,
            valueStep: 1,
            
        }),

        m.numeric({ name: 'report_interval_radiator_s', label: 'Radyatör Sıcaklığı Rapor Sıklığı', cluster: 'hvacUserInterfaceCfg', attribute: { ID: 0x8030, type: 0x21 }, description: 'Radyatör yüzey sıcaklığı telemetri gönderim sıklığı (0 - 600 saniye; 0 raporlamayı kapatır).', endpointNames: ['climate'], entityCategory: 'config', unit: 's', valueMin: 0, valueMax: 600, valueStep: 1 }),
        m.numeric({ name: 'report_interval_ambient_s', label: 'Ortam Sıcaklığı Rapor Sıklığı', cluster: 'hvacUserInterfaceCfg', attribute: { ID: 0x8031, type: 0x21 }, description: 'Ortam sıcaklığı telemetri gönderim sıklığı (0 - 600 saniye; 0 raporlamayı kapatır).', endpointNames: ['ambient_sensor'], entityCategory: 'config', unit: 's', valueMin: 0, valueMax: 600, valueStep: 1 }),
        m.numeric({ name: 'report_interval_processor_s', label: 'CPU Sıcaklığı Rapor Sıklığı', cluster: 'hvacUserInterfaceCfg', attribute: { ID: 0x8032, type: 0x21 }, description: 'ESP32-C6 CPU sıcaklığı telemetri gönderim sıklığı (0 - 600 saniye; 0 raporlamayı kapatır).', endpointNames: ['processor_sensor'], entityCategory: 'config', unit: 's', valueMin: 0, valueMax: 600, valueStep: 1 }),
        m.numeric({ name: 'report_interval_health_s', label: 'Sistem Sağlığı Rapor Sıklığı', cluster: 'hvacUserInterfaceCfg', attribute: { ID: 0x8033, type: 0x21 }, description: 'Sistem sağlık skoru telemetri gönderim sıklığı (0 - 600 saniye; 0 raporlamayı kapatır).', endpointNames: ['system_health'], entityCategory: 'config', unit: 's', valueMin: 0, valueMax: 600, valueStep: 1 }),
        m.numeric({ name: 'report_interval_memory_s', label: 'Bellek Telemetri Rapor Sıklığı', cluster: 'hvacUserInterfaceCfg', attribute: { ID: 0x8034, type: 0x21 }, description: 'Serbest RAM bellek oranı telemetri gönderim sıklığı (0 - 600 saniye; 0 raporlamayı kapatır).', endpointNames: ['memory'], entityCategory: 'config', unit: 's', valueMin: 0, valueMax: 600, valueStep: 1 }),
        m.numeric({ name: 'report_interval_uptime_s', label: 'Çalışma Süresi Rapor Sıklığı', cluster: 'hvacUserInterfaceCfg', attribute: { ID: 0x8035, type: 0x21 }, description: 'Çalışma süresi telemetri gönderim sıklığı (0 - 600 saniye; 0 raporlamayı kapatır).', endpointNames: ['uptime'], entityCategory: 'config', unit: 's', valueMin: 0, valueMax: 600, valueStep: 1 }),

        withCustomLabel(
            m.temperature({
                endpointNames: ['processor_sensor'],
                fzConvert: temperatureReadback(5, 'processor_sensor'),
                reporting: {min: 1, max: 30, change: 50},
                description: 'ESP32-C6 SOC dahili işlemci (CPU) çip sıcaklığı.',
            }),
            'İşlemci (CPU) Sıcaklığı'
        ),
        racoolDiagnostics(),

        m.enumLookup({
            name: 'processor_power_profile',
            label: 'CPU Güç Profili',
            lookup: {'Tasarruf': 2, 'Dengeli': 1, 'Performans': 0},
            cluster: 'hvacUserInterfaceCfg',
            attribute: {ID: 0x8041, type: 0x20},
            endpointName: 'system',
            entityCategory: 'config',
            description: 'Varsayılan Dengeli. CPU frekans sınırları: Performans 160 MHz, Dengeli 80–160 MHz, Tasarruf 40–80 MHz. Zigbee yönlendirici bağlantısı açık kalır.',
        }),

        m.enumLookup({
            name: 'radio_tx_profile',
            label: 'Zigbee İletim Gücü',
            lookup: {'Eco': 0, 'Normal': 1, 'Boost': 2},
            cluster: 'hvacUserInterfaceCfg',
            attribute: {ID: 0x8042, type: 0x20},
            endpointName: 'system',
            entityCategory: 'config',
            description: 'Varsayılan Normal. Eco 0 dBm, Normal 10 dBm, Boost 20 dBm. Yalnız iletim gücü değişir; Zigbee yönlendirici ve alıcı açık kalır. Menzil ortam koşullarına bağlıdır.',
        }),

        m.enumLookup({
            name: 'system_command',
            label: 'Sistem Yönetim Komutu',
            lookup: systemCmdLookup,
            cluster: 'hvacUserInterfaceCfg',
            attribute: { ID: 0x8040, type: 0x20 },
            description: 'Cihaz donanım komutları: Yeniden Başlat cihazı yeniden başlatır, Kapat ve Derin Uyku cihazı uykuya alır; rotary encoder düğmesiyle uyandırılabilir. Zigbee, derin uykuda radyo kapalı olduğu için uyandırma kaynağı değildir.',
            endpointName: 'system',
            entityCategory: 'config',
            
        }),
    ],
};

// Local CLI/OLED changes publish configuration edges through these bindings.
definition.extend.push({isModernExtend: true, configure: [async function configureConfigurationBindings(device, coordinatorEndpoint) {
    const routes = [[1, 'hvacThermostat'], [1, 'hvacUserInterfaceCfg'],
        [2, 'genOnOff'], [3, 'genOnOff'], [3, 'genLevelCtrl'], [4, 'hvacThermostat'],
        ...[4, 5, 6, 7, 8, 9, 10].map(endpoint => [endpoint, 'hvacUserInterfaceCfg'])];
    for (const [endpoint, cluster] of routes) {
        await device.getEndpoint(endpoint).bind(cluster, coordinatorEndpoint);
    }
}]});

// Keep product presentation separate from unchanged protocol ownership.
export const presentationGroups = [
    ['status', ['temperature_climate', 'temperature_ambient_sensor', 'temperature_processor_sensor', 'fan_relay_state_climate', 'system_health', 'free_memory_percent', 'uptime_seconds']],
    ['fan', ['fan_operating_mode_climate', 'object_temperature_source_climate', 'fan_off_threshold_climate', 'fan_on_threshold_climate', 'ambient_temperature_source_climate', 'fan_off_temperature_difference_climate', 'fan_on_temperature_difference_climate', 'radiator_temperature_calibration_climate', 'ambient_temperature_calibration_ambient_sensor']],
    ['display', ['display_mode_display', 'display_sleep_timeout_display']],
    ['status_led', ['status_led_mode_status_indicator', 'status_led_brightness_status_indicator', 'status_led_startup_animation_status_indicator', 'status_led_idle_animation_status_indicator', 'status_led_connection_animation_status_indicator', 'status_led_fault_animation_status_indicator']],
    ['thermal_led', ['thermal_led_mode_thermal_indicator', 'brightness_thermal_indicator', 'thermal_led_startup_animation_thermal_indicator', ...['cold', 'warm', 'hot'].flatMap(zone => [`${zone}_region_range_lower_bound_thermal_indicator`, `${zone}_region_range_upper_bound_thermal_indicator`, `${zone}_region_color_xy_thermal_indicator`]), 'danger_threshold_thermal_indicator', 'danger_region_color_xy_thermal_indicator', 'freeze_threshold_thermal_indicator', 'freeze_region_color_xy_thermal_indicator']],
    ['reports', ['report_interval_radiator_s_climate', 'report_interval_ambient_s_ambient_sensor', 'report_interval_processor_s_processor_sensor', 'report_interval_health_s_system_health', 'report_interval_memory_s_memory', 'report_interval_uptime_s_uptime']],
    ['system', ['processor_power_profile_system', 'radio_tx_profile_system', 'system_command_system']],
    ['diagnostics', ['effective_object_temperature_climate', 'object_temperature_effective_source_climate', 'object_temperature_source_status_climate', 'effective_ambient_temperature_climate', 'ambient_temperature_effective_source_climate', 'ambient_temperature_source_status_climate', 'temperature_difference_climate', ...[1, 2, 3].flatMap(slot => [`external_temperature_${slot}_climate`, `external_temperature_${slot}_status_climate`])]],
];
const presentationRank = new Map(presentationGroups.flatMap(([, properties]) => properties).map((property, index) => [property, index]));
for (const extension of definition.extend) extension.exposes?.sort((a, b) => presentationRank.get(a.property) - presentationRank.get(b.property));
definition.extend.sort((a, b) => (a.exposes?.length ? presentationRank.get(a.exposes[0].property) : -1) - (b.exposes?.length ? presentationRank.get(b.exposes[0].property) : -1));

// CurrentLevel is read-only in standard ZCL. Use the level command without
// OnOff side effects so brightness never overrides the selected operating mode.
const brightnessExtension = definition.extend.find(extension =>
    extension.exposes?.some(expose => expose.name === 'brightness'));
brightnessExtension.toZigbee[0].convertSet = async (entity, key, value, meta) => {
    if (!Number.isInteger(value) || value < 0 || value > 100) throw new Error('Brightness must be an integer percentage from 0 to 100');
    const level = Math.round(value * 254 / 100);
    await meta.device.getEndpoint(3).command('genLevelCtrl', 'moveToLevel', {level, transtime: 0, optionsMask: 1, optionsOverride: 1}, {});
    return {state: {[key]: value}};
};

const internalBrightnessExtension = definition.extend.find(extension =>
    extension.exposes?.some(expose => expose.name === 'status_led_brightness'));
const setInternalBrightness = internalBrightnessExtension.toZigbee[0].convertSet;
internalBrightnessExtension.toZigbee[0].convertSet = async (entity, key, value, meta) => {
    if (!Number.isInteger(value) || value < 0 || value > 100) throw new Error('Brightness must be an integer percentage from 0 to 100');
    return setInternalBrightness(entity, key, value, meta);
};

// Keep endpoint metadata: discovery command topics depend on it.
function decorateExpose(exp) {
    const name = exp.name ?? exp.type;
    let icon = 'mdi:cog';
    if (exp.unit === '°C' || name.includes('temperature') || name.includes('threshold')) icon = 'mdi:thermometer';
    if (name.includes('fan_')) icon = 'mdi:fan';
    if (name.includes('led') || exp.type === 'light') icon = 'mdi:led-on';
    if (exp.type === 'switch' || name === 'state') icon = 'mdi:power';
    if (name === 'brightness') icon = 'mdi:brightness-6';
    if (name.includes('animation')) icon = 'mdi:animation-play';
    if (name.includes('color') || name === 'color_xy') icon = 'mdi:palette';
    if (name.includes('display')) icon = 'mdi:monitor';
    if (name.includes('report_interval') || name.includes('timeout')) icon = 'mdi:timer-outline';
    if (name === 'system_health') icon = 'mdi:heart-pulse';
    if (name === 'free_memory_percent') icon = 'mdi:memory';
    if (name === 'system_command') icon = 'mdi:power';
    exp.withHomeAssistant({...exp.homeassistant, icon: exp.homeassistant?.icon ?? icon});
    for (const feature of exp.features ?? []) decorateExpose(feature);
}
for (const extension of definition.extend) {
    for (const exp of extension.exposes ?? []) {
        decorateExpose(exp);
        // Routing uses the immutable property suffix and endpoint map.
        // Presentation does not need to expose implementation endpoint names.
        const hideEndpoint = feature => {
            if (feature.endpoint) Object.defineProperty(feature, 'endpoint', {value:feature.endpoint, enumerable:false, writable:true, configurable:true});
            for (const child of feature.features ?? []) hideEndpoint(child);
        };
        hideEndpoint(exp);
    }
}

// Immutable pre-migration discovery identities preserve HA entity references.
// These strings are compatibility data, never active expose or endpoint names.
export const legacyDiscoveryKeys = {
    "effective_object_temperature_climate": "effective_object_temperature_isi_dagitma_fani",
    "effective_ambient_temperature_climate": "effective_ambient_temperature_isi_dagitma_fani",
    "cold_region_color_xy_thermal_indicator": "rgb_soguk_bolge_rengi_xy_rgb_bildirim_isigi",
    "warm_region_color_xy_thermal_indicator": "rgb_ilik_bolge_rengi_xy_rgb_bildirim_isigi",
    "hot_region_color_xy_thermal_indicator": "rgb_sicak_bolge_rengi_xy_rgb_bildirim_isigi",
    "danger_region_color_xy_thermal_indicator": "rgb_tehlike_bolge_rengi_xy_rgb_bildirim_isigi",
    "freeze_region_color_xy_thermal_indicator": "rgb_donma_bolge_rengi_xy_rgb_bildirim_isigi",
    "fan_relay_state_climate": "fan_relay_state_isi_dagitma_fani",
    "fan_operating_mode_climate": "fan_calisma_modu_isi_dagitma_fani",
    "temperature_climate": "temperature_isi_dagitma_fani",
    "temperature_ambient_sensor": "temperature_ortam_sicakligi",
    "fan_on_threshold_climate": "fan_acilma_esigi_isi_dagitma_fani",
    "fan_off_threshold_climate": "fan_kapanma_esigi_isi_dagitma_fani",
    "radiator_temperature_calibration_climate": "nesne_sicaklik_kalibrasyonu_isi_dagitma_fani",
    "ambient_temperature_calibration_ambient_sensor": "ortam_sicaklik_kalibrasyonu_ortam_sicakligi",
    "brightness_thermal_indicator": "brightness_rgb_bildirim_isigi",
    "thermal_led_mode_thermal_indicator": "harici_led_modu_rgb_bildirim_isigi",
    "status_led_brightness_status_indicator": "dahili_led_parlakligi_dahili_durum_ledi",
    "status_led_mode_status_indicator": "dahili_led_modu_dahili_durum_ledi",
    "thermal_led_startup_animation_thermal_indicator": "harici_led_baslangic_animasyonu_rgb_bildirim_isigi",
    "cold_region_range_lower_bound_thermal_indicator": "rgb_soguk_bolge_araligi_alt_sinir_rgb_bildirim_isigi",
    "cold_region_range_upper_bound_thermal_indicator": "rgb_soguk_bolge_araligi_ust_sinir_rgb_bildirim_isigi",
    "cold_region_color_preset_thermal_indicator": "rgb_soguk_durum_rengi_rgb_bildirim_isigi",
    "warm_region_range_lower_bound_thermal_indicator": "rgb_ilik_bolge_araligi_alt_sinir_rgb_bildirim_isigi",
    "warm_region_range_upper_bound_thermal_indicator": "rgb_ilik_bolge_araligi_ust_sinir_rgb_bildirim_isigi",
    "warm_region_color_preset_thermal_indicator": "rgb_ilik_durum_rengi_rgb_bildirim_isigi",
    "hot_region_range_lower_bound_thermal_indicator": "rgb_sicak_bolge_araligi_alt_sinir_rgb_bildirim_isigi",
    "hot_region_range_upper_bound_thermal_indicator": "rgb_sicak_bolge_araligi_ust_sinir_rgb_bildirim_isigi",
    "hot_region_color_preset_thermal_indicator": "rgb_sicak_durum_rengi_rgb_bildirim_isigi",
    "danger_region_color_preset_thermal_indicator": "rgb_tehlike_durum_rengi_rgb_bildirim_isigi",
    "freeze_region_color_preset_thermal_indicator": "rgb_donma_durum_rengi_rgb_bildirim_isigi",
    "danger_threshold_thermal_indicator": "rgb_tehlike_siniri_rgb_bildirim_isigi",
    "freeze_threshold_thermal_indicator": "rgb_donma_siniri_rgb_bildirim_isigi",
    "status_led_idle_animation_status_indicator": "dahili_led_bosta_animasyonu_dahili_durum_ledi",
    "status_led_connection_animation_status_indicator": "dahili_led_baglanti_animasyonu_dahili_durum_ledi",
    "status_led_fault_animation_status_indicator": "dahili_led_ariza_animasyonu_dahili_durum_ledi",
    "temperature_display_unit_display": "sicaklik_birimi_oled_ekran",
    "display_mode_display": "ekran_modu_oled_ekran",
    "display_sleep_timeout_display": "ekran_uyku_suresi_oled_ekran",
    "report_interval_radiator_s_climate": "rapor_sikligi_fan_s_isi_dagitma_fani",
    "report_interval_ambient_s_ambient_sensor": "rapor_sikligi_ortam_s_ortam_sicakligi",
    "report_interval_processor_s_processor_sensor": "rapor_sikligi_cpu_s_cpu_sicakligi",
    "report_interval_health_s_system_health": "rapor_sikligi_saglik_s_sistem_sagligi",
    "report_interval_memory_s_memory": "rapor_sikligi_bellek_s_bos_ram_bellegi",
    "report_interval_uptime_s_uptime": "rapor_sikligi_uptime_s_calisma_suresi",
    "temperature_processor_sensor": "temperature_cpu_sicakligi",
    "system_health": "sistem_sagligi",
    "free_memory_percent": "bos_ram_orani",
    "uptime_seconds": "calisma_suresi_saniye",
    "processor_power_profile_system": "guc_profili_sistem_yonetimi",
    "radio_tx_profile_system": "zigbee_iletim_gucu_sistem_yonetimi",
    "system_command_system": "sistem_komutu_sistem_yonetimi"
};
definition.meta = {...definition.meta, overrideHaDiscoveryPayload(payload) {
    // Z2M supplies this diagnostic expose outside modernExtend. Keep its
    // immutable identity and explicitly localize its presentation as well.
    if (typeof payload.unique_id === 'string' && payload.unique_id.endsWith('_linkquality_zigbee2mqtt')) {
        payload.name = 'Zigbee Sinyal Kalitesi';
        payload.icon = 'mdi:signal';
        return;
    }
    for (const [property, legacy] of Object.entries(legacyDiscoveryKeys)) {
        const token = '_' + property + '_';
        if (typeof payload.unique_id !== 'string') continue;
        const suffix = token + 'zigbee2mqtt';
        if (!payload.unique_id.endsWith(suffix) || !/^0x[0-9a-f]{16}$/i.test(payload.unique_id.slice(0, -suffix.length))) continue;
        payload.unique_id = payload.unique_id.replace(token, '_' + legacy + '_');
        if (typeof payload.object_id === 'string' && payload.object_id.endsWith(property))
            payload.object_id = payload.object_id.slice(0, -property.length) + legacy;
        if (typeof payload.default_entity_id === 'string' && payload.default_entity_id.endsWith(property))
            payload.default_entity_id = payload.default_entity_id.slice(0, -property.length) + legacy;
        break;
    }
}};

export default definition;
