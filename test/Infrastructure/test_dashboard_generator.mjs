import test from 'node:test';
import assert from 'node:assert/strict';
import {generateDashboard as generateGroupedDashboard,resolveEntities} from '../../tools/home-assistant/generate-dashboard.mjs';
import definition, {legacyDiscoveryKeys} from '../../tools/racool.mjs';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const device=(number)=>({name:`Device ${number}`,ieee:`0x${number.toString().padStart(16,'0')}`,command_topic:`zigbee2mqtt/device${number}/set`});
// Existing behavioral checks inspect embedded cards independently of their presentation wrapper.
function generateDashboard(config, entries) {
 const result=generateGroupedDashboard(config,entries);
 return {...result,views:result.views.map(view=>({...view,sections:view.sections[0].cards[0].groups.map(group=>({
  type:'grid',cards:[{type:'heading',heading:group.title},...group.cards],
 }))}))};
}
function registry(d,legacy=true){
 const keys=new Set(['linkquality',...Object.keys(legacyDiscoveryKeys),...definition.extend.flatMap(extension=>extension.exposes??[]).map(expose=>expose.property)]);
 return [...keys].map((key,i)=>({platform:'mqtt',unique_id:`${d.ieee}_${legacy?(legacyDiscoveryKeys[key]??key):key}_zigbee2mqtt`,device_id:d.ieee,
  entity_id:`${key.includes('color_xy')?'sensor':key.includes('threshold')||key.includes('bound')?'number':'select'}.device${d.name.slice(-1)}_${i}`,disabled_by:null}));
}
test('searchable groups retain overview and start diagnostics collapsed',()=>{
 const d=device(1),panel=generateGroupedDashboard({devices:[d]},registry(d));
 const card=panel.views[0].sections[0].cards[0];
 assert.equal(card.type,'custom:racool-settings-card');
 assert.equal(card.groups.filter(group=>group.overview).length,1);
 assert.equal(card.groups[0].title,'Anlık durum');
 assert.equal(card.groups.find(group=>group.title==='Fan kontrolü').expanded,true);
 assert.equal(card.groups.at(-1).expanded,false);
 assert.equal(card.groups.find(group=>group.title==='Sistem ve Zigbee').expanded,false);
});
test('two devices get isolated entity references and command topics',()=>{
 const first=device(1),second=device(2);const result=generateDashboard({devices:[first,second]},[...registry(first),...registry(second,false)]);
 assert.equal(result.views.length,2);
 const [a,b]=result.views.map(JSON.stringify);
 assert.ok(a.includes(first.command_topic));assert.ok(!a.includes(second.command_topic));assert.ok(!a.includes('.device2_'));
 assert.ok(b.includes(second.command_topic));assert.ok(!b.includes('.device1_'));
 const fan=result.views[0].sections[1].cards.find(c=>c.card?.title==='Nesne sıcaklığı eşikleri').card;assert.equal(fan.zones[0].min,20);assert.equal(fan.zones[0].max,99);
 assert.notEqual(fan.zones[0].low,fan.zones[0].high);
 const regions=result.views[0].sections.find(section=>section.cards[0].heading==='RGB sıcaklık bölgeleri');
 assert.equal(regions.cards[2].card.type,'custom:racool-color-card');
 assert.equal(regions.cards[1].card.before[0].title,'Donma sınırı');
 assert.equal(regions.cards[1].card.after[0].title,'Tehlike sınırı');
 assert.equal(result.views[0].sections.find(s=>s.cards[0].heading==='Harici RGB LED').cards[2].conditions[0].state,'Otomatik');
});
test('missing, disabled, duplicate and foreign identities reject incomplete panels',()=>{
 const d=device(1),entries=registry(d);
 assert.throws(()=>resolveEntities([],d.ieee));
 assert.throws(()=>resolveEntities([...entries,entries[0]],d.ieee));
 const disabled=entries.map(e=>({...e,disabled_by:'user'}));assert.throws(()=>resolveEntities(disabled,d.ieee));
 const foreign=entries.map(e=>({...e}));foreign[1].device_id='other';assert.throws(()=>resolveEntities(foreign,d.ieee));
 assert.throws(()=>generateDashboard({devices:[d,d]},entries));
 assert.throws(()=>generateDashboard({devices:[{...d,command_topic:'zigbee2mqtt/+/set'}]},entries));
});
test('CLI refuses output aliases of input files without modifying them',()=>{
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'racool-dashboard-'));
 const config=path.join(directory,'devices.json'),entries=path.join(directory,'registry.json');
 try {
  fs.writeFileSync(config,'{"devices":[]}');fs.writeFileSync(entries,'[]');
  for(const output of [config,entries]) {
   const run=spawnSync(process.execPath,['tools/home-assistant/generate-dashboard.mjs',config,entries,output],{encoding:'utf8'});
   assert.notEqual(run.status,0);assert.match(run.stderr,/must not overwrite/);
   assert.equal(fs.readFileSync(config,'utf8'),'{"devices":[]}');assert.equal(fs.readFileSync(entries,'utf8'),'[]');
  }
 } finally {
  fs.unlinkSync(config);fs.unlinkSync(entries);fs.rmdirSync(directory);
 }
});

test('fan controls follow the mode and selected device sources',()=>{
 const d=device(1),entries=registry(d);const panel=generateDashboard({devices:[d]},entries).views[0];
 const fan=panel.sections.find(s=>s.cards[0].heading==='Fan kontrolü');
 const absolute=fan.cards.find(c=>c.card?.title==='Nesne sıcaklığı eşikleri');
 assert.deepEqual(absolute.conditions[0].state,['Nesneye duyarlı']);
 const difference=fan.cards.find(c=>c.card?.title==='Ortama duyarlı kontrol');
 assert.deepEqual(difference.conditions[0].state,['Ortama duyarlı']);
 assert.equal(difference.card.zones.length,1);
 assert.equal(difference.card.zones[0].step,0.1);
 assert.equal(difference.card.before[0].entities.length,1);
 assert.equal(absolute.card.before[0].entities.length,1);
 assert.equal(fan.cards.some(c=>c.card?.title==='Nesne sıcaklığı kaynağı'),false);
 assert.equal(JSON.stringify(difference.card).includes(entitiesForDifference(entries,d.ieee)),false);
 for(const slot of [1,2,3]) {
  const input=fan.cards.find(c=>c.card?.type==='vertical-stack'&&c.card.cards[1].card.title===`Harici Nesne Sıcaklığı #${slot}`);
  assert.deepEqual(input.conditions[0].state,['Nesneye duyarlı','Ortama duyarlı']);
  assert.equal(input.conditions[1].conditions[0].state,`Harici ${slot}`);
  assert.equal(input.conditions[1].conditions[1].conditions[0].state,'Ortama duyarlı');
  const roles=input.card.cards.slice(1);
  assert.deepEqual(roles.map(c=>c.card.title),[`Harici Nesne Sıcaklığı #${slot}`,`Harici Ortam Sıcaklığı #${slot}`,`Harici Nesne ve Ortam Sıcaklığı #${slot}`]);
  assert.equal(roles[1].conditions[0].state,'Ortama duyarlı');
  assert.equal(roles[1].conditions[2].state_not,`Harici ${slot}`);
  assert.equal(roles[2].conditions[1].state,`Harici ${slot}`);
  assert.equal(roles[2].conditions[2].state,`Harici ${slot}`);
 }
 assert.equal(panel.sections.at(-1).cards[0].heading,'Gelişmiş kaynak tanılaması');
 assert.equal(JSON.stringify(panel).includes('temperature_display_unit'),false);
});
function entitiesForDifference(entries,ieee){return resolveEntities(entries,ieee).temperature_difference_climate;}

test('external slot roles render exactly one appropriate card for each fan/source combination',()=>{
 const d=device(1),entries=registry(d),panel=generateDashboard({devices:[d]},entries).views[0];
 const fan=panel.sections.find(s=>s.cards[0].heading==='Fan kontrolü');
 const entities=resolveEntities(entries,d.ieee);
 const evaluate=(condition,state)=>{
  if(condition.condition==='or')return condition.conditions.some(c=>evaluate(c,state));
  if(condition.condition==='and')return condition.conditions.every(c=>evaluate(c,state));
  const expected=condition.state??condition.state_not;
  const matches=Array.isArray(expected)?expected.includes(state[condition.entity]):expected===state[condition.entity];
  return condition.state_not===undefined?matches:!matches;
 };
 for(const slot of [1,2,3]) {
  const input=fan.cards.find(c=>c.card?.type==='vertical-stack'&&c.card.cards[1].card.title===`Harici Nesne Sıcaklığı #${slot}`);
  for(const mode of ['Kapalı','Sürekli Açık','Nesneye duyarlı','Ortama duyarlı'])
   for(const object of ['Dahili',`Harici ${slot}`])for(const ambient of ['Dahili',`Harici ${slot}`]) {
    const state={[entities.fan_operating_mode_climate]:mode,[entities.object_temperature_source_climate]:object,[entities.ambient_temperature_source_climate]:ambient};
    const shown=input.conditions.every(c=>evaluate(c,state))?input.card.cards.slice(1).filter(c=>c.conditions.every(x=>evaluate(x,state))):[];
    const objectUsed=['Nesneye duyarlı','Ortama duyarlı'].includes(mode)&&object===`Harici ${slot}`;
    const ambientUsed=mode==='Ortama duyarlı'&&ambient===`Harici ${slot}`;
    assert.equal(shown.length,objectUsed||ambientUsed?1:0,`${slot}/${mode}/${object}/${ambient}`);
    if(shown.length)assert.equal(shown[0].card.title,`Harici ${objectUsed&&ambientUsed?'Nesne ve Ortam':objectUsed?'Nesne':'Ortam'} Sıcaklığı #${slot}`);
   }
 }
});
