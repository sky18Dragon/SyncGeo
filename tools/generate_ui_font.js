#!/usr/bin/env node
/*
 * Regenerate the LVGL font subset used by FurbleUI Chinese labels.
 *
 * The stock LVGL SourceHanSansSC CJK font only contains a small common subset,
 * so this script extracts all non-ASCII UI characters and feeds numeric Unicode
 * ranges to lv_font_conv. Numeric ranges avoid terminal/codepage corruption when
 * running on Windows PowerShell.
 */
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const uiPath = path.join(root, 'src', 'FurbleUI.cpp');
const sourceFont = path.join(
  root,
  'managed_components',
  'lvgl__lvgl',
  'scripts',
  'built_in_font',
  'SourceHanSansSC-Normal.otf'
);
const output = path.join(root, 'src', 'FurbleUIFontZh14.c');

const ui = fs.readFileSync(uiPath, 'utf8');
const zhStrings = [
  ...ui.matchAll(/const UI_Text\s+\w+\s*=\s*\{\s*"(?:[^"\\]|\\.)*"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}/g),
].map(m => m[1]);

// Keep common punctuation available even if not present in today's labels.
// Written as code points so this script remains ASCII-only and survives
// Windows PowerShell's legacy code page defaults.
const extraCodePoints = [
  0xff0c, 0x3002, 0xff01, 0xff1f, 0xff08, 0xff09, 0x300a, 0x300b,
  0xff1a, 0xff1b, 0x3001, 0x2103, 0x2190, 0x2192, 0x2191, 0x2193,
  0x002b, 0x002d, 0x2013, 0x2014, 0x2026, 0x00b7,
];
const extra = String.fromCodePoint(...extraCodePoints);
const chars = [...new Set([...zhStrings.join('') + extra].filter(ch => ch.codePointAt(0) > 0x7f))]
  .sort((a, b) => a.codePointAt(0) - b.codePointAt(0));
const range = '0x20-0x7f,' + chars.map(ch => `0x${ch.codePointAt(0).toString(16)}`).join(',');

console.log(`Generating ${path.relative(root, output)} with ${chars.length} non-ASCII glyphs`);
const args = [
  'lv_font_conv@latest',
  '--no-compress', '--no-prefilter', '--no-kerning',
  '--bpp', '2',
  '--size', '14',
  '--font', sourceFont,
  '-r', range,
  '--format', 'lvgl',
  '--lv-include', 'lvgl.h',
  '--lv-font-name', 'furble_font_zh_14',
  '-o', output,
  '--force-fast-kern-format',
];

if (process.platform === 'win32') {
  const npxCli = path.join(path.dirname(process.execPath), 'node_modules', 'npm', 'bin', 'npx-cli.js');
  execFileSync(
    process.execPath,
    [npxCli, ...args],
    { cwd: root, stdio: 'inherit' }
  );
} else {
  execFileSync('npx', args, { cwd: root, stdio: 'inherit' });
}

const generated = fs.readFileSync(output, 'utf8');
const missing = chars.filter(
  ch => !generated.includes(`U+${ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0')}`)
);
if (missing.length) {
  throw new Error(`Generated font is missing glyphs: ${missing.join('')}`);
}
console.log('Font subset coverage verified.');
