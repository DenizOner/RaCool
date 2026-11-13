import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';
import {fontCoverage, generateFonts} from '../../../tools/generate_oled_fonts.mjs';

const outputRoot = fileURLToPath(new URL('../../build/', import.meta.url));
fs.mkdirSync(outputRoot, {recursive: true});

test('baseline includes European Latin, Greek and modern Cyrillic before catalog additions', () => {
    const codes = new Set(fontCoverage());
    for (const character of 'AymMÇĞİÖŞÜçğıöşüÀÆÐÞŁŒȘȚẞαΩΆАЯЁЇЄҐЉЊЏЎабяёїєґљњџў')
        assert.ok(codes.has(character.codePointAt(0)), `Missing ${character}`);
    assert.ok(codes.size > 1500);
});

test('unsupported catalog glyph fails explicitly instead of silently becoming a blank', () => {
    assert.throws(() => fontCoverage('温'), /Unsupported OLED glyph U\+6E29/);
    assert.doesNotThrow(() => fontCoverage('Türkçe\nEnglish\tБългарски'));
});

test('generated one-bit fonts preserve bitmap bounds and A/y contours without threshold loss', () => {
    const output = fs.mkdtempSync(path.join(outputRoot, 'font-generation-'));
    try {
        generateFonts(output);
        for (const size of [10, 14, 20]) {
            const file = path.join(output, `racool_font_${size}.c`);
            const source = fs.readFileSync(file, 'utf8');
            assert.match(source, /\.bpp = 1,/);
            assert.match(source, /\.bitmap_format = 0/);
            const descriptors = [...source.matchAll(/\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}/g)]
                .map(match => match.slice(1).map(Number));
            const raw = source.match(/glyph_bitmap\[\] = \{([\s\S]*?)\};/)[1].replace(/\/\*[\s\S]*?\*\//g, '');
            const bytes = [...raw.matchAll(/0x([\da-f]+)/gi)].map(match => parseInt(match[1], 16));
            assert.ok(descriptors.length > 1500);
            for (const [index,, width, height] of descriptors)
                assert.ok(index + Math.ceil(width * height / 8) <= bytes.length, 'Bitmap bounds');
            for (const character of 'AyMm') {
                // The first cmap is the contiguous printable ASCII range beginning at glyph 1.
                const [index, advance, width, height,, offsetY] = descriptors[character.codePointAt(0) - 31];
                assert.ok(advance > 0 && width > 0 && height > 0);
                const rows = Array.from({length: height}, (_, y) => Array.from({length: width}, (_, x) => {
                    const pixel = y * width + x;
                    return (bytes[index + (pixel >> 3)] >> (7 - (pixel & 7))) & 1;
                }).filter(Boolean).length);
                assert.ok(rows.every(count => count > 0), `${size}/${character}: no missing horizontal stroke row`);
                if (character === 'y') assert.ok(offsetY < 0, 'Descender survives in glyph metrics');
            }
            const before = fs.statSync(file).mtimeMs;
            fs.writeFileSync(path.join(output, `mtime-${size}.json`), JSON.stringify({before, source}));
        }
        generateFonts(output);
        for (const size of [10, 14, 20]) {
            const {before, source} = JSON.parse(fs.readFileSync(path.join(output, `mtime-${size}.json`)));
            const file = path.join(output, `racool_font_${size}.c`);
            assert.equal(fs.readFileSync(file, 'utf8'), source, 'Reproducible bytes');
            assert.equal(fs.statSync(file).mtimeMs, before, 'Unchanged inputs do not invalidate builds');
        }
    } finally {
        for (const file of fs.readdirSync(output)) fs.unlinkSync(path.join(output, file));
        fs.rmdirSync(output);
    }
});
