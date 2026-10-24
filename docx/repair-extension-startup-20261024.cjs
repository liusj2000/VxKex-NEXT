'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawnSync } = require('child_process');
const base = 'D:\\VSCode-win32-x64-1.107.1\\data';
const backup = path.join(base, 'extension-startup-backup-20261024');
const report = 'D:\\project\\VxKex-NEXT\\docx\\extension-startup-repair-20261024.json';
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const specs = [
 { name: 'codex', file: path.join(base, 'extensions', 'openai.chatgpt-26.1021.22027-win32-x64', 'out', 'extension.js'), before: 'receivedWebviewMessage:this.receivedWebviewMessage,timeoutMs:3e4})},3e4)', after: 'receivedWebviewMessage:this.receivedWebviewMessage,timeoutMs:12e4})},12e4)' },
 { name: 'antigravity', file: path.join(base, 'extensions', 'google.google-antigravity-1.9.0', 'extension.js'), before: 'k&&!isNaN(k)?k:pc.DEFAULT_SERVER_STARTUP_TIMEOUT_MS', after: 'k&&!isNaN(k)?k:Math.max(pc.DEFAULT_SERVER_STARTUP_TIMEOUT_MS,120000)' }
];
const results = [];
try {
 if (process.argv.includes('--rollback')) {
   const saved = JSON.parse(fs.readFileSync(report, 'utf8'));
   for (const item of saved.results) {
     if (hash(fs.readFileSync(item.file)) !== item.afterHash) throw Error('Current file changed: ' + item.file);
     if (hash(fs.readFileSync(item.backup)) !== item.beforeHash) throw Error('Backup hash mismatch');
   }
   for (const item of saved.results) fs.copyFileSync(item.backup, item.file);
   fs.writeFileSync(report + '.rollback.json', JSON.stringify({ success: true, time: new Date().toISOString() }, null, 2));
 } else {
   fs.mkdirSync(backup, { recursive: true });
   for (const spec of specs) {
     const bytes = fs.readFileSync(spec.file);
     const source = bytes.toString('utf8');
     if (source.split(spec.before).length !== 2) throw Error('Expected one original patch anchor: ' + spec.name);
     const candidate = source.replace(spec.before, spec.after);
     const original = path.join(backup, spec.name + '.extension.js.original');
     if (fs.existsSync(original) && hash(fs.readFileSync(original)) !== hash(bytes)) throw Error('Existing backup differs');
     if (!fs.existsSync(original)) fs.writeFileSync(original, bytes, { flag: 'wx' });
     const temp = path.join(backup, spec.name + '.candidate.js');
     fs.writeFileSync(temp, candidate);
     const checked = spawnSync(process.execPath, ['--check', temp], { env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' }, windowsHide: true, timeout: 30000, encoding: 'utf8' });
     if (checked.status !== 0) throw Error('Syntax check failed: ' + spec.name + ' ' + checked.stderr);
     results.push({ name: spec.name, file: spec.file, backup: original, candidate: temp, beforeHash: hash(bytes), afterHash: hash(Buffer.from(candidate)), syntaxExit: checked.status });
   }
   for (const item of results) {
     if (hash(fs.readFileSync(item.file)) !== item.beforeHash) throw Error('File changed before apply');
   }
   for (const item of results) {
     fs.copyFileSync(item.candidate, item.file);
     if (hash(fs.readFileSync(item.file)) !== item.afterHash) throw Error('Post-write hash mismatch');
   }
   fs.writeFileSync(report, JSON.stringify({ success: true, scope: 'startup timeout mitigation; UI verification pending', time: new Date().toISOString(), results }, null, 2));
 }
} catch (error) {
 fs.writeFileSync(report + '.error.json', JSON.stringify({ error: error.message, results }, null, 2));
 process.exitCode = 1;
}