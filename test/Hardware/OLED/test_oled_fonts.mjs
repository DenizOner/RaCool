import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const lvglRoot=process.env.RACOOL_LVGL_DIR || fileURLToPath(new URL('../../../.pio/libdeps/esp32-c6-devkitc-1/lvgl',import.meta.url));
test('font checks use the pinned LVGL 9.6.0 release',()=>{
    assert.equal(JSON.parse(fs.readFileSync(path.join(lvglRoot,'library.json'),'utf8')).version,'9.6.0');
});
const config=fs.readFileSync(new URL('../../../lib/lv_conf.h',import.meta.url),'utf8');
test('application configuration does not override library version metadata',()=>{
    assert.doesNotMatch(config,/^\s*#\s*define\s+LVGL_VERSION_/m);
});
const threshold=Number(config.match(/^#define LV_DRAW_SW_I1_LUM_THRESHOLD\s+(\d+)/m)?.[1]);
test('fan units have glyphs in every configured font',()=>{
    const widget=fs.readFileSync(new URL('../../../src/Controllers/UIController/Widgets/WidgetFan.c',import.meta.url),'utf8');
    const units=[...widget.matchAll(/snprintf\(unit, unitLen, "([^"%]+)"\)/g)].map(m=>m[1]);
    assert.ok(units.length>0);
    for(const size of [10,14,20]) {
        const source=fs.readFileSync(path.join(lvglRoot,`src/font/lv_font_montserrat_${size}.c`),'utf8');
        const list=[...source.match(/unicode_list_1\[\] = \{([\s\S]*?)\};/)[1].matchAll(/0x([\da-f]+)/g)].map(m=>parseInt(m[1],16)+176);
        for(const character of units.join('')) {
            const code=character.codePointAt(0);
            assert.ok((code>=32 && code<=126)||list.includes(code),`${size}: missing ${character}`);
        }
    }
});
function glyphs(size) {
    const source=fs.readFileSync(path.join(lvglRoot,`src/font/lv_font_montserrat_${size}.c`),'utf8');
    const descriptors=[...source.matchAll(/\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}/g)].map(m=>m.slice(1).map(Number));
    const raw=source.match(/glyph_bitmap\[\] = \{([\s\S]*?)\};/)[1].replace(/\/\*[\s\S]*?\*\//g,'');
    const bytes=[...raw.matchAll(/0x([\da-f]+)/g)].map(m=>parseInt(m[1],16));
    return code=>{
        const [index,advance,width,height]=descriptors[code-31];
        assert.ok(index+Math.ceil(width*height/2)<=bytes.length,'Glyph bitmap stays inside font storage');
        return {advance,width,height,pixels:Array.from({length:width*height},(_,i)=>
            (((bytes[index+(i>>1)]>>((i%2)?0:4))&15)*17)>threshold)};
    };
}
test('monochrome M retains both vertical stems at the smallest UI font size',()=>{
    const g=glyphs(10)('M'.charCodeAt(0));
    for(let y=3;y<g.height;y++) {
        assert.ok(g.pixels[y*g.width],'Left stem');
        assert.ok(g.pixels[y*g.width+g.width-2],'Right stem');
    }
});
test('monochrome m retains three distinct stems',()=>{
    const g=glyphs(10)('m'.charCodeAt(0));
    for(let y=1;y<g.height;y++) {
        const row=g.pixels.slice(y*g.width,(y+1)*g.width);
        assert.equal(row.filter((on,x)=>on && !row[x-1]).length,3,'Three separated stems');
    }
});
test('every printable ASCII glyph in every configured font has valid visible pixels',()=>{
    for(const size of [10,14,20]) {
        const get=glyphs(size);
        for(let code=33;code<=126;code++) {
            const g=get(code);
            assert.ok(g.advance>0 && g.width>0 && g.height>0,`${size}/${code} dimensions`);
            assert.ok(g.pixels.some(Boolean),`${size}/${code} visible`);
        }
    }
});

test('Wi-Fi crop keeps exactly dot, inner arc plus dot, or the full glyph',()=>{
    const source=fs.readFileSync(path.join(lvglRoot,'src/font/lv_font_montserrat_10.c'),'utf8');
    const list=[...source.match(/unicode_list_1\[\] = \{([\s\S]*?)\};/)[1].matchAll(/0x([\da-f]+)/g)].map(m=>parseInt(m[1],16));
    const index=96+list.indexOf(0xf1eb-176);
    assert.ok(index>=96);
    const descriptors=[...source.matchAll(/\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}/g)].map(m=>m.slice(1).map(Number));
    const [start,,width,height,,offsetY]=descriptors[index];
    const raw=source.match(/glyph_bitmap\[\] = \{([\s\S]*?)\};/)[1].replace(/\/\*[\s\S]*?\*\//g,'');
    const bytes=[...raw.matchAll(/0x([\da-f]+)/g)].map(m=>parseInt(m[1],16));
    const lineHeight=Number(source.match(/\.line_height = (\d+)/)[1]);
    const baseline=Number(source.match(/\.base_line = (\d+)/)[1]);
    const top=lineHeight-baseline-height-offsetY;
    function visibleRows(crop){return Array.from({length:height},(_,y)=>{
        if(top+y<lineHeight-crop)return 0;
        return Array.from({length:width},(_,x)=>{const i=y*width+x;return (((bytes[start+(i>>1)]>>((i%2)?0:4))&15)*17)>threshold;}).filter(Boolean).length;
    });}
    assert.equal(width,13);assert.equal(lineHeight,11);
    assert.deepEqual(visibleRows(4).slice(0,7),Array(7).fill(0));
    assert.ok(visibleRows(4).slice(7).some(Boolean));
    assert.equal(visibleRows(7).slice(0,4).some(Boolean),false);
    assert.ok(visibleRows(7).slice(4,7).some(Boolean));
    assert.ok(visibleRows(11).slice(0,4).some(Boolean));
});

test('status centering bearings match the real monochrome symbol bitmaps',()=>{
    const source=fs.readFileSync(path.join(lvglRoot,'src/font/lv_font_montserrat_10.c'),'utf8');
    const list=[...source.match(/unicode_list_1\[\] = \{([\s\S]*?)\};/)[1].matchAll(/0x([\da-f]+)/g)].map(m=>parseInt(m[1],16));
    const descriptors=[...source.matchAll(/\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}/g)].map(m=>m.slice(1).map(Number));
    const raw=source.match(/glyph_bitmap\[\] = \{([\s\S]*?)\};/)[1].replace(/\/\*[\s\S]*?\*\//g,'');
    const bytes=[...raw.matchAll(/0x([\da-f]+)/g)].map(m=>parseInt(m[1],16));
    const lineHeight=Number(source.match(/\.line_height = (\d+)/)[1]);
    const baseline=Number(source.match(/\.base_line = (\d+)/)[1]);
    for(const [code,crop,expected] of [[0xf1eb,11,[0,1,12,9]], [0xf1eb,7,[2,0,8,6]],
        [0xf1eb,4,[5,0,3,3]], [0xf011,11,[0,0,10,10]]]) {
        const index=96+list.indexOf(code-176);
        assert.ok(index>=96);
        const [start,,width,height,offsetX,offsetY]=descriptors[index];
        const points=[];
        for(let y=0;y<height;y++) for(let x=0;x<width;x++) {
            const i=y*width+x, alpha=((bytes[start+(i>>1)]>>((i%2)?0:4))&15)*17;
            const localY=lineHeight-baseline-height-offsetY+y-(lineHeight-crop);
            if(alpha>threshold && localY>=0) points.push([offsetX+x,localY]);
        }
        const left=Math.min(...points.map(p=>p[0])), top=Math.min(...points.map(p=>p[1]));
        assert.deepEqual([left,top,Math.max(...points.map(p=>p[0]))-left+1,
            Math.max(...points.map(p=>p[1]))-top+1],expected);
    }
});
