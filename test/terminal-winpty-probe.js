// Run with the existing Node runtime; no npm install is required.
// args: output-code-page [emit|emit-fixed]
const fs = require('fs');
const pty = require('D:/VSCode-win32-x64-1.107.1/resources/app/node_modules/node-pty');
const cp = process.argv[2] || '65001';
const mode = process.argv[3] || 'emit-fixed';
let output = '';
const term = pty.spawn('D:\\project\\VxKex-NEXT\\test\\x64\\Release\\terminal-redraw-test.exe', [mode,cp], {
  name:'xterm-256color', cols:80, rows:25, cwd:'D:\\project\\VxKex-NEXT',
  env:process.env, useConpty:false
});
term.onData(data => { output += data; });
term.onExit(event => {
  clearTimeout(timer);
  const passed = event.exitCode === 0 && output.includes('BEGIN检查项目状态gEND') && !output.includes('检检');
  const result = {cp, mode, event, passed, output};
  fs.writeFileSync('test/x64/Release/winpty-'+cp+'.json', JSON.stringify(result,null,2));
  console.log(JSON.stringify(result));
  process.exit(passed ? 0 : 1);
});
const timer = setTimeout(() => {
  console.error('TIMEOUT');
  term.kill();
  setTimeout(() => process.exit(3), 1000);
}, 8000);
