import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
class ElementFixture {
 constructor(){this.children=[];this.listeners={};this.attributes={};}
 append(...children){this.children.push(...children);}
 replaceChildren(...children){this.children=children;}
 setAttribute(key,value){this.attributes[key]=value;}
 addEventListener(key,callback){this.listeners[key]=callback;}
}
globalThis.HTMLElement=class extends ElementFixture {attachShadow(){this.shadowRoot=new ElementFixture();}};
globalThis.document={createElement:()=>new ElementFixture()};
globalThis.window={loadCardHelpers:async()=>({createCardElement:config=>({config})})};
const source=await fs.readFile(new URL('../../tools/home-assistant/racool-settings-card.js',import.meta.url),'utf8');
const {matchesSearch, RaCoolSettingsCard}=await import('data:text/javascript;base64,'+Buffer.from(source).toString('base64'));

test('search finds nested conditional controls and Turkish accents',()=>{
 const cards=[{type:'conditional',card:{title:'Ortama duyarlı kontrol',entities:[{entity:'number.private_id',name:'Sıcaklık farkı'}]}}];
 assert.equal(matchesSearch('Fan kontrolü',cards,'sicaklik farki'),true);
 assert.equal(matchesSearch('Fan kontrolü',cards,'ortama kontrol'),true);
 assert.equal(matchesSearch('Fan kontrolü',cards,'private_id'),false);
 assert.equal(matchesSearch('Fan kontrolü',cards,'renk'),false);
 assert.equal(matchesSearch('Fan kontrolü',cards,''),true);
});

test('invalid configuration rejects before loading embedded cards',()=>{
 const card=new RaCoolSettingsCard();
 for(const groups of [undefined,[],[{title:'Fan',cards:[]}],[{title:'',cards:[{}]}]])
  assert.throws(()=>card.setConfig({groups}));
});

test('search opens hidden matches, restores expansion and forwards fresh HA state',async()=>{
 const card=new RaCoolSettingsCard();
 card.setConfig({groups:[{title:'Durum',overview:true,cards:[{type:'entities'}]},
  {title:'Fan',expanded:true,cards:[{title:'Fan modu'}]},
  {title:'Gelişmiş',cards:[{title:'Kalibrasyon'}]}]});
 await Promise.resolve();
 const container=card.shadowRoot.children[1], [input,overview,fan,advanced]=container.children;
 assert.equal(advanced.open,false);assert.equal(fan.open,true);
 input.value='kalibrasyon';input.listeners.input();
 assert.equal(overview.hidden,false);assert.equal(fan.hidden,true);assert.equal(advanced.open,true);
 input.value='';input.listeners.input();
 assert.equal(fan.hidden,false);assert.equal(fan.open,true);assert.equal(advanced.open,false);
 const hass={states:{}};card.hass=hass;
 assert.equal(card._children.length,3);assert.ok(card._children.every(child=>child.hass===hass));
});
