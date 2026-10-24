'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const net = require('net');
const { spawn } = require('child_process');
const root = 'D:\\project\\VxKex-NEXT\\docx';
const original = process.argv.includes('--original-profile');
const log = path.join(root, original ? 'extension-original-profile-probe-20261024.jsonl' : 'extension-startup-probe-20261024.jsonl');
fs.writeFileSync(log, '');
const record = data => fs.appendFileSync(log, JSON.stringify({ time: new Date().toISOString(), ...data }) + '\n');
const extension = 'D:\\VSCode-win32-x64-1.107.1\\data\\extensions\\openai.chatgpt-26.1021.22027-win32-x64';
const source = fs.readFileSync(path.join(extension, 'out', 'extension.js'), 'utf8');
for (const text of ['renderer_ready_timeout', 'Webview renderer did not become ready', 'index.html']) {
  const i = source.indexOf(text);
  record({ codexPattern: text, index: i, context: i < 0 ? '' : source.slice(Math.max(0, i - 1800), i + 2000) });
}
const html = fs.readFileSync(path.join(extension, 'webview', 'index.html'), 'utf8');
record({ html });
for (const match of html.matchAll(/(?:src|href)="([^"#]+)"/g)) {
  const file = path.join(extension, 'webview', match[1].replace(/^\.\//, '').replace(/^\//, ''));
  record({ asset: file, exists: fs.existsSync(file), bytes: fs.existsSync(file) ? fs.statSync(file).size : null });
}
const server = net.createServer();
server.listen(0, '127.0.0.1', () => {
  const port = server.address().port;
  server.close(() => {
    const start = Date.now();
    const args = ['--hub', `--hub-port=${port}`, original ? '--app_data_dir=antigravity' : '--app_data_dir=antigravity-startup-probe-20261024'];
    if (original) args.push('--add-dir=D:\\project\\VxKex-NEXT');
    const child = spawn('C:\\Users\\liusj\\.gemini\\bin\\agy.exe', args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    record({ event: 'spawn', pid: child.pid, port });
    for (const name of ['stdout', 'stderr']) child[name].on('data', chunk => {
      const text = chunk.toString().split('\n').filter(line => !/token|authorization|cookie|csrf/i.test(line)).join('\n');
      if (text) record({ stream: name, text });
    });
    let finished = false;
    const stop = reason => { if (finished) return; finished = true; record({ event: 'stop', reason, elapsedMs: Date.now() - start }); child.kill(); };
    const deadline = setTimeout(() => stop('deadline_120s'), 120000);
    child.on('error', error => { record({ event: 'spawn_error', message: error.message }); clearTimeout(deadline); });
    child.on('exit', (code, signal) => { finished = true; clearTimeout(deadline); record({ event: 'exit', code, signal }); });
    const poll = () => {
      if (finished) return;
      const req = http.get({ host: '127.0.0.1', port, path: '/healthz', agent: false }, res => {
        res.resume(); record({ event: 'http', status: res.statusCode, elapsedMs: Date.now() - start });
        if (res.statusCode >= 200 && res.statusCode < 500) stop('ready');
        else setTimeout(poll, 1000);
      });
      req.setTimeout(2000, () => req.destroy(new Error('probe_timeout')));
      req.on('error', error => { record({ event: 'probe_error', message: error.message, elapsedMs: Date.now() - start }); if (!finished) setTimeout(poll, 1000); });
    };
    poll();
  });
});