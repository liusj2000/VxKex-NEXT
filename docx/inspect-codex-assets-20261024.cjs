'use strict';
const fs = require('fs');
const path = require('path');
const root = 'D:\\VSCode-win32-x64-1.107.1\\data\\extensions\\openai.chatgpt-26.1021.22027-win32-x64';
const out = [];
const assets = path.join(root, 'webview', 'assets');
const files = fs.readdirSync(assets);
const missing = new Set();
let refs = 0;
for (const file of files.filter(f => f.endsWith('.js'))) {
 const source = fs.readFileSync(path.join(assets, file), 'utf8');
 for (const m of source.matchAll(/(?:from\s*|import\s*\(|import\s*)["'](\.\/[^"']+)["']/g)) {
   refs++;
   if (!fs.existsSync(path.join(assets, m[1]))) missing.add(file + ' -> ' + m[1]);
 }
}
out.push(`assets=${files.length} importReferences=${refs} missing=${missing.size}`, ...missing);
const source = fs.readFileSync(path.join(root, 'out', 'extension.js'), 'utf8');
for (const pattern of ['K2=','class K2','30000','async getHtml','PROD_CSP_TAG_HERE']) {
 const i = source.indexOf(pattern);
 out.push(`PATTERN ${pattern} INDEX ${i}`, i < 0 ? '' : source.slice(Math.max(0, i - 400), i + 4500));
}
fs.writeFileSync('D:\\project\\VxKex-NEXT\\docx\\codex-assets-inspect-20261024.txt', out.join('\n'));