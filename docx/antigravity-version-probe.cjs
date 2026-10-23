'use strict';
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const run = promisify(execFile);
const binary = 'C:\\Users\\liusj\\.gemini\\bin\\agy.exe';
(async () => {
  console.log(JSON.stringify({ node: process.version, electron: process.versions.electron }));
  for (const options of [{ timeout: 5000 }, { timeout: 5000, windowsHide: true }, { timeout: 30000 }, { timeout: 30000, windowsHide: true }]) {
    const start = Date.now();
    try {
      const result = await run(binary, ['--version'], options);
      console.log(JSON.stringify({ options, elapsedMs: Date.now() - start, ...result, exitCode: 0 }));
    } catch (error) {
      console.log(JSON.stringify({ options, elapsedMs: Date.now() - start, code: error.code, signal: error.signal, killed: error.killed, stdout: error.stdout, stderr: error.stderr, message: error.message }));
    }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });