import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const root = fileURLToPath(new URL('../', import.meta.url));
const converter = path.join(root, 'node_modules/lv_font_conv/lv_font_conv.js');
const require = createRequire(converter);
const opentype = require('opentype.js');
const fontPath = path.join(root, 'resources/fonts/NotoSans-SemiBold.ttf');
const expectedHash = 'a7af8d4a11e8e49fea9a19786a6469210b512cd4061413e8036bdd2425722795';

// Seed complete supported alphabet blocks, independent of installed catalogs.
export const alphabetRanges = [
    [0x20, 0x7e], [0xa0, 0x24f], [0x300, 0x36f],
    [0x370, 0x52f], [0x1e00, 0x1fff], [0x2de0, 0x2dff],
    [0xa640, 0xa69f],
];

export function fontCoverage(extraText = '') {
    const data = fs.readFileSync(fontPath);
    if (crypto.createHash('sha256').update(data).digest('hex') !== expectedHash)
        throw Error('Noto Sans source checksum mismatch');
    const font = opentype.parse(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength));
    const present = code => font.charToGlyphIndex(String.fromCodePoint(code)) !== 0;
    const codes = new Set();
    for (const [first, last] of alphabetRanges)
        for (let code = first; code <= last; code++) if (present(code)) codes.add(code);
    for (const character of extraText.normalize('NFC')) {
        const code = character.codePointAt(0);
        if (code === 10 || code === 13 || code === 9) continue;
        if (!present(code)) throw Error(`Unsupported OLED glyph U+${code.toString(16).toUpperCase()}`);
        codes.add(code);
    }
    return [...codes].sort((a, b) => a - b);
}

function encodeRanges(codes) {
    const ranges = [];
    for (let index = 0; index < codes.length;) {
        const first = codes[index++];
        let last = first;
        while (codes[index] === last + 1) last = codes[index++];
        ranges.push(first === last ? `${first}` : `${first}-${last}`);
    }
    return ranges.join(',');
}

function writeChanged(file, contents) {
    if (fs.existsSync(file) && fs.readFileSync(file, 'utf8') === contents) return;
    const temporary = `${file}.${process.pid}.tmp`;
    fs.writeFileSync(temporary, contents);
    try { fs.renameSync(temporary, file); }
    finally { if (fs.existsSync(temporary)) fs.unlinkSync(temporary); }
}

export function generateFonts(output, extraText = '') {
    const version = JSON.parse(fs.readFileSync(path.join(root, 'node_modules/lv_font_conv/package.json'))).version;
    if (version !== '1.5.3') throw Error('Expected lv_font_conv 1.5.3');
    const codes = fontCoverage(extraText);
    const ranges = encodeRanges(codes);
    fs.mkdirSync(output, {recursive: true});
    for (const size of [10, 14, 20]) {
        const name = `racool_font_${size}`;
        const staging = fs.mkdtempSync(path.join(output, '.font-'));
        try {
            const generated = path.join(staging, `${name}.c`);
            const result = spawnSync(process.execPath, [converter, '--size', String(size),
                '--bpp', '1', '--format', 'lvgl', '--font', fontPath, '--range', ranges,
                '--no-compress', '--no-prefilter', '--lv-include', 'lvgl.h', '--lv-font-name', name, '-o', generated],
            {encoding: 'utf8', windowsHide: true, timeout: 120000});
            if (result.error || result.status !== 0 || result.stderr.trim())
                throw Error(result.error?.message || result.stderr || `Font converter exited ${result.status}`);
            // The tool records temporary absolute paths in its provenance comment.
            const source = fs.readFileSync(generated, 'utf8').replace(/^\/\*[\s\S]*?\*\//,
                `/* Generated from Noto Sans SemiBold v2.015 with lv_font_conv 1.5.3; ${size}px, 1bpp. */`);
            writeChanged(path.join(output, `${name}.c`), source);
        } finally {
            for (const file of fs.readdirSync(staging)) fs.unlinkSync(path.join(staging, file));
            fs.rmdirSync(staging);
        }
    }
    writeChanged(path.join(output, 'coverage.json'), JSON.stringify({sourceSha256: expectedHash,
        sizes: [10, 14, 20], bpp: 1, codepoints: codes}, null, 2) + '\n');
    return codes;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    if (process.argv.length < 3 || process.argv.length > 4)
        throw Error('Usage: node tools/generate_oled_fonts.mjs <output-directory> [catalog-characters-file]');
    const extraText = process.argv[3] ? fs.readFileSync(process.argv[3], 'utf8') : '';
    const codes = generateFonts(path.resolve(process.argv[2]), extraText);
    console.log(`Generated three monochrome OLED fonts with ${codes.length} supported characters each`);
}
