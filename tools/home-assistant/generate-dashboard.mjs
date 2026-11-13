import definition, {legacyDiscoveryKeys} from '../racool.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const exposeLabels=Object.fromEntries(definition.extend.flatMap(extension=>extension.exposes??[]).map(expose=>[expose.property,expose.label]));
Object.assign(exposeLabels,{temperature_climate:'Radyatör sıcaklığı',temperature_ambient_sensor:'Ortam sıcaklığı',temperature_processor_sensor:'CPU sıcaklığı',linkquality:'Zigbee Sinyal Kalitesi'});
const role = (base, endpoint) => endpoint ? `${base}_${endpoint}` : base;
const groups = [
    ['Anlık durum', ['temperature_climate','temperature_ambient_sensor','temperature_processor_sensor','fan_relay_state_climate','system_health','free_memory_percent','uptime_seconds']],
    ['Fan kontrolü', ['fan_operating_mode_climate']],
    ['Gelişmiş kaynak tanılaması', ['effective_object_temperature_climate','effective_ambient_temperature_climate','temperature_difference_climate','object_temperature_effective_source_climate','ambient_temperature_effective_source_climate','object_temperature_source_status_climate','ambient_temperature_source_status_climate','external_temperature_1_status_climate','external_temperature_2_status_climate','external_temperature_3_status_climate']],
    ['Sıcaklık kalibrasyonu', ['radiator_temperature_calibration_climate','ambient_temperature_calibration_ambient_sensor']],
    ['OLED ekran', ['display_mode_display','display_sleep_timeout_display']],
    ['Dahili LED', ['status_led_mode_status_indicator','status_led_brightness_status_indicator','status_led_startup_animation_status_indicator','status_led_idle_animation_status_indicator','status_led_connection_animation_status_indicator','status_led_fault_animation_status_indicator']],
    ['Harici RGB LED', ['thermal_led_mode_thermal_indicator','brightness_thermal_indicator','thermal_led_startup_animation_thermal_indicator']],
    ['RGB sıcaklık bölgeleri', []],
    ['Raporlama · 0 = kapalı', ['report_interval_radiator_s_climate','report_interval_ambient_s_ambient_sensor','report_interval_processor_s_processor_sensor','report_interval_health_s_system_health','report_interval_memory_s_memory','report_interval_uptime_s_uptime']],
    ['Sistem ve Zigbee', ['processor_power_profile_system','radio_tx_profile_system','system_command_system','linkquality']],
];
const zoneNames = ['cold','warm','hot','danger','freeze'];
const zoneLabels = ['Soğuk','Ilık','Sıcak','Tehlike','Donma'];
const required = [...groups.flatMap(([,keys])=>keys),'fan_on_threshold_climate','fan_off_threshold_climate',
    'freeze_threshold_thermal_indicator','danger_threshold_thermal_indicator',
    'object_temperature_source_climate','ambient_temperature_source_climate','fan_on_temperature_difference_climate','fan_off_temperature_difference_climate',
    ...[1,2,3].map(slot=>`external_temperature_${slot}_climate`),
    ...zoneNames.map(name=>role(`${name}_region_color_xy`,'thermal_indicator')),
    ...zoneNames.slice(0,3).flatMap(name=>['lower','upper'].map(bound=>role(`${name}_region_range_${bound}_bound`,'thermal_indicator')))];

export function resolveEntities(entries, ieee) {
    if (!/^0x[0-9a-f]{16}$/.test(ieee)) throw Error('Use a lowercase Zigbee IEEE address.');
    const result={}; let deviceId;
    for (const property of required) {
        const ids = [property,legacyDiscoveryKeys[property]].filter(Boolean).map(key=>`${ieee}_${key}_zigbee2mqtt`);
        const matches = entries.filter(entry=>entry.platform==='mqtt' && ids.includes(entry.unique_id) && !entry.disabled_by);
        if (matches.length!==1) throw Error(`Expected one enabled entity for ${property}; found ${matches.length}`);
        const entry=matches[0];
        if (!/^(sensor|binary_sensor|number|select)\.[a-z0-9_]+$/.test(entry.entity_id) || !entry.device_id) throw Error(`Invalid entity for ${property}`);
        if (deviceId && deviceId!==entry.device_id) throw Error('Registry identities cross device boundaries.');
        deviceId=entry.device_id; result[property]=entry.entity_id;
    }
    return result;
}

export function generateDashboard(config, entries) {
    if (!Array.isArray(config?.devices) || !config.devices.length) throw Error('Configure at least one device.');
    const used=new Set(), topics=new Set();
    const views=config.devices.map((device,index)=>{
        if (typeof device.name!=='string' || !device.name.trim() || device.name.length>80 || used.has(device.ieee) ||
            typeof device.command_topic!=='string' || !/^[^#+\x00-\x1f]+\/set$/.test(device.command_topic) || topics.has(device.command_topic))
            throw Error('Each device requires a name, unique IEEE and unique explicit MQTT set topic.');
        used.add(device.ieee);topics.add(device.command_topic);
        const entities=resolveEntities(entries,device.ieee);
        const sections=groups.map(([heading,keys])=>({type:'grid',cards:[{type:'heading',heading},
            ...(keys.length?[{type:'entities',show_header_toggle:false,entities:keys.map(key=>({entity:entities[key],name:exposeLabels[key]??key}))}]:[])]}));
        const modeEntity=entities.fan_operating_mode_climate;
        const conditional=(states,card)=>({type:'conditional',conditions:[{condition:'state',entity:modeEntity,state:states}],card});
        const entityCard=(title,keys)=>({type:'entities',title,show_header_toggle:false,entities:keys.map(key=>({entity:entities[key],name:exposeLabels[key]??key}))});
        const automaticModes=['Nesneye duyarlı','Ortama duyarlı'];
        sections[1].cards.push(
            conditional(['Nesneye duyarlı'],{type:'custom:racool-zone-card',title:'Nesne sıcaklığı eşikleri',zones:[{
                label:'Kapanma / Açılma',low:entities.fan_off_threshold_climate,high:entities.fan_on_threshold_climate,min:20,max:99,
            }],before:[entityCard('Nesne sıcaklığı kaynağı',['object_temperature_source_climate'])]}),
            conditional(['Ortama duyarlı'],{type:'custom:racool-zone-card',title:'Ortama duyarlı kontrol',zones:[{
                label:'Kapanma / Açılma farkı',low:entities.fan_off_temperature_difference_climate,high:entities.fan_on_temperature_difference_climate,
                min:-9,max:9,step:0.1,min_gap:0.1,
            }],before:[entityCard('Ortam sıcaklığı kaynağı',['ambient_temperature_source_climate'])]}),
            conditional(['Kapalı','Sürekli Açık'],{type:'markdown',content:'Bu modda otomatik sıcaklık eşikleri kullanılmaz. CPU sıcaklık koruması önceliğini korur.'}),
            {type:'conditional',conditions:[{condition:'state',entity:modeEntity,state_not:['Nesneye duyarlı','Ortama duyarlı','Kapalı','Sürekli Açık']}],card:{type:'markdown',content:'Fan modu henüz alınamadı. Kontrol ayarları güncel mod bilgisi gelene kadar gizlidir.'}},
        );
        for(const slot of [1,2,3]) {
            const objectSelected={condition:'state',entity:entities.object_temperature_source_climate,state:`Harici ${slot}`};
            const ambientSelected={condition:'state',entity:entities.ambient_temperature_source_climate,state:`Harici ${slot}`};
            const ambientMode={condition:'state',entity:modeEntity,state:'Ortama duyarlı'};
            const roleCard=(title,conditions)=>{
                const card=entityCard(title,[`external_temperature_${slot}_climate`,`external_temperature_${slot}_status_climate`]);
                card.entities[0].name=`${title} Ölçümü`;
                card.entities[1].name=`${title} Veri Güncelliği`;
                return {type:'conditional',conditions,card};
            };
            sections[1].cards.push({type:'conditional',conditions:[
            {condition:'state',entity:modeEntity,state:automaticModes},
            {condition:'or',conditions:[
                {condition:'state',entity:entities.object_temperature_source_climate,state:`Harici ${slot}`},
                {condition:'and',conditions:[{condition:'state',entity:modeEntity,state:'Ortama duyarlı'},{condition:'state',entity:entities.ambient_temperature_source_climate,state:`Harici ${slot}`}]},
            ]},
        ],card:{type:'vertical-stack',cards:[
            {type:'markdown',content:`**Harici Sıcaklık Kaynağı #${slot}**\nBu değer ayar değildir. Otomasyonunuz güncel ölçümü en az 5 dakikada bir göndermelidir; 15 dakika sonra dahili sensöre dönüş yapılır.`},
            roleCard(`Harici Nesne Sıcaklığı #${slot}`,[objectSelected,{condition:'or',conditions:[
                {condition:'state',entity:modeEntity,state:'Nesneye duyarlı'},
                {condition:'state',entity:entities.ambient_temperature_source_climate,state_not:`Harici ${slot}`},
            ]}]),
            roleCard(`Harici Ortam Sıcaklığı #${slot}`,[ambientMode,ambientSelected,
                {condition:'state',entity:entities.object_temperature_source_climate,state_not:`Harici ${slot}`}]),
            roleCard(`Harici Nesne ve Ortam Sıcaklığı #${slot}`,[ambientMode,objectSelected,ambientSelected]),
        ]}});
        }
        sections[groups.findIndex(([heading])=>heading==='RGB sıcaklık bölgeleri')].cards.push({type:'custom:racool-zone-card',title:'Bölge aralıkları',zones:zoneNames.slice(0,3).map((name,i)=>({
            label:zoneLabels[i],low:entities[role(`${name}_region_range_lower_bound`,'thermal_indicator')],
            high:entities[role(`${name}_region_range_upper_bound`,'thermal_indicator')],min:[7,26,45][i],max:[25,44,59][i],
        })),before:[entityCard('Donma sınırı',['freeze_threshold_thermal_indicator'])],
            after:[entityCard('Tehlike sınırı',['danger_threshold_thermal_indicator'])]},
        {type:'custom:racool-color-card',command_topic:device.command_topic,zones:zoneNames.map((name,i)=>({
            label:zoneLabels[i],entity:entities[role(`${name}_region_color_xy`,'thermal_indicator')],property:role(`${name}_region_color_xy`,'thermal_indicator'),
        }))});
        const diagnostics=sections.splice(groups.findIndex(([heading])=>heading==='Gelişmiş kaynak tanılaması'),1)[0];
        sections.push(diagnostics);
        // Keep mode selectors available while hiding controls that cannot affect the selected mode.
        for (const [heading, mode, active] of [
            ['Dahili LED','status_led_mode_status_indicator','Otomatik'],
            ['Harici RGB LED','thermal_led_mode_thermal_indicator','Otomatik'],
            ['OLED ekran','display_mode_display','Otomatik'],
        ]) {
            const section=sections.find(section=>section.cards[0].heading===heading);
            const rows=section.cards[1].entities;
            section.cards=[section.cards[0],{type:'entities',show_header_toggle:false,entities:[rows[0]]},
                {type:'conditional',conditions:[{condition:'state',entity:entities[mode],state:active}],
                 card:{type:'entities',show_header_toggle:false,entities:rows.slice(1)}},
                {type:'conditional',conditions:[{condition:'state',entity:entities[mode],state_not:active}],
                 card:{type:'markdown',content:heading==='OLED ekran'?'Uyku süresi Otomatik ekran modunda kullanılır.':
                    'Parlaklık ve animasyon ayarları Otomatik LED modunda gösterilir.'}}];
        }
        const regions=sections.find(section=>section.cards[0].heading==='RGB sıcaklık bölgeleri');
        regions.cards=regions.cards.map((card,index)=>index===0?card:{type:'conditional',
            conditions:[{condition:'state',entity:entities.thermal_led_mode_thermal_indicator,state:'Otomatik'}],card});
        regions.cards.push({type:'conditional',conditions:[{condition:'state',entity:entities.thermal_led_mode_thermal_indicator,state_not:'Otomatik'}],
            card:{type:'markdown',content:'Bölge ayarları harici RGB LED Otomatik modunda gösterilir.'}});
        const grouped = sections.map(section => ({
            title:section.cards[0].heading,
            overview:section.cards[0].heading==='Anlık durum',
            expanded:section.cards[0].heading==='Fan kontrolü',
            cards:section.cards.slice(1),
        }));
        return {title:device.name,path:index===0?'kontrol':`device-${index+1}`,type:'sections',max_columns:1,
            sections:[{type:'grid',cards:[{type:'custom:racool-settings-card',groups:grouped}]}]};
    });
    return {title:'RaCool',views};
}

if (process.argv[1] && import.meta.url===pathToFileURL(process.argv[1]).href) {
    const [, ,configPath,registryPath,outputPath]=process.argv;
    if (!configPath || !registryPath || !outputPath) throw Error('Usage: node generate-dashboard.mjs devices.json entity_registry.json output.json');
    const canonical = file => {
        const resolved = fs.existsSync(file) ? fs.realpathSync(file) : path.resolve(file);
        return process.platform==='win32' ? resolved.toLowerCase() : resolved;
    };
    if ([configPath,registryPath].some(file=>canonical(file)===canonical(outputPath))) throw Error('Output must not overwrite an input or registry file.');
    const registry=JSON.parse(fs.readFileSync(registryPath,'utf8'));
    const dashboard=generateDashboard(JSON.parse(fs.readFileSync(configPath,'utf8')),registry.data?.entities??registry);
    fs.writeFileSync(outputPath,JSON.stringify(dashboard,null,2)+'\n');
    console.log(`Generated ${dashboard.views.length} isolated device views.`);
}
