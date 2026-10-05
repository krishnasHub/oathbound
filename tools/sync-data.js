// Sync the shared game data into both builds.
//
//   data/game-data.json            <- the single source of truth (edit this)
//     -> prototype/rpg-prototype.html   (inlined, so the HTML stays one standalone file)
//     -> unreal/Content/Data/game-data.json   (read by URPGData at startup)
//
// Usage:  node tools/sync-data.js
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const src = path.join(root, 'data', 'game-data.json');
const html = path.join(root, 'prototype', 'rpg-prototype.html');
const unreal = path.join(root, 'unreal', 'Content', 'Data', 'game-data.json');

const text = fs.readFileSync(src, 'utf8').replace(/\s+$/, '');
JSON.parse(text);   // refuse to sync broken JSON

const page = fs.readFileSync(html, 'utf8');
const re = /(<script type="application\/json" id="game-data">\n)[\s\S]*?(\n<\/script>)/;
if (!re.test(page)) throw new Error('game-data block not found in ' + html);
fs.writeFileSync(html, page.replace(re, (_, open, close) => open + text + close));
console.log('updated', path.relative(root, html));

fs.mkdirSync(path.dirname(unreal), { recursive: true });
fs.writeFileSync(unreal, text + '\n');
console.log('updated', path.relative(root, unreal));
