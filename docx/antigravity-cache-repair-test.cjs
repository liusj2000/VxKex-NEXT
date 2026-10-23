'use strict';
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const { pipeline } = require('node:stream/promises');
const { promisify } = require('node:util');
const { execFile } = require('node:child_process');
const semver = require('D:/VSCode-win32-x64-1.107.1/resources/app/node_modules/semver');
const sourcePath = process.argv[2] || 'C:/Users/liusj/AppData/Local/Temp/antigravity-extension-repaired.js';
const source = fs.readFileSync(sourcePath, 'utf8');
function extract(start, end) {
  const begin = source.indexOf(start);
  const finish = source.indexOf(end, begin);
  assert(begin >= 0 && finish > begin, 'Missing original function anchor: '+start);
  return source.slice(begin, finish + end.length);
}
const code = [
  extract('async function lb(a)', 'Ma.clearBinaryVersionCache=function(){jb.clear()};'),
  extract('async function mb(a,b)', 'Ma.inspectBinaryVersion=mb;'),
  extract('async function ob(a,b,c)', 'Ma.checkBinaryVersion=ob;'),
  extract('async function qb(a,b,c)', 'Ma.verifyBinaryVersion=qb;'),
  extract('async function Gb(a,b="sha512")', 'Ma.computeFileChecksum=Gb;'),
  extract('async function kxReuseVerifiedBackend(', 'Ma.acquireInstalledBinaryPath=dc;'),
  'globalThis.api={dc,mb,kxReuseVerifiedBackend};',
].join('\n');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'agy-reuse-test-'));
const binaryPath = path.join(temp, 'agy.exe');
const fixture = Buffer.from('controlled backend fixture, never executed');
const digest = crypto.createHash('sha512').update(fixture).digest('hex');
const baseUrl = 'https://official-release.test';
let passed = 0;
async function check(name, options={}) {
  fs.writeFileSync(binaryPath, fixture);
  if(options.missing) fs.unlinkSync(binaryPath);
  const logs = [];
  let downloadCalls = 0;
  let probeCalls = 0;
  const manifest = options.manifest || { version: '1.4.6', sha512: digest };
  const target = options.live ? 'C:/Users/liusj/.gemini/bin/agy.exe' : binaryPath;
  const realFs = { ...fs, promises: { ...fs.promises } };
  if(options.statRace) {
    let count=0;
    realFs.promises.stat = async filename => {
      const stat = await fs.promises.stat(filename);
      // lb: initial probe, pre-check, post-check; emulate concurrent replacement.
      if(++count>=3) return { ...stat, size: stat.size+1 };
      return stat;
    };
  }
  const context = {
    Ma: { MIN_AGY_VERSION: '1.1.11' }, jb: new Map(),
    f: { asyncContextStart: () => () => value => value },
    la: realFs, Ia: path, Ga: crypto, Ja: { pipeline },
    Oa: async filename => fs.existsSync(filename),
    Na: async (filename, args, probeOptions) => {
      probeCalls++;
      assert.equal(probeOptions.timeout,30000);
      assert.equal(probeOptions.windowsHide,true);
      if(options.realProbe) return promisify(execFile)(filename,args,probeOptions);
      if(options.probeVersion) return { stdout: options.probeVersion, stderr: '' };
      throw Object.assign(new Error('Controlled failed version probe'), { code: options.timeout?'ETIMEDOUT':3221225794, killed:!!options.timeout });
    },
    ib: (actual,expected) => semver.gte(actual,expected),
    Qb: async()=>{}, Ub: async()=>{}, Hb: ()=>target,
    Ra: ()=>baseUrl, bc: ()=> 'controlled_test', Wa: String,
    Fb: async()=> { if(options.manifestFailure) throw new Error('offline'); return manifest; },
    $b: value=>value,
    n: { window: { withProgress: async()=> { downloadCalls++; throw new Error('DOWNLOAD_REQUIRED'); } }, ProgressLocation: { Notification: 1 } },
    process, setTimeout, clearTimeout, console,
  };
  vm.runInNewContext(code,context,{filename:sourcePath});
  const storedUrl=options.channelChange?'https://other-channel.test':baseUrl;
  let result;
  try {
    result = await context.api.dc({
      context: { extension: { packageJSON: { version:'1.9.0' } }, globalState: { get:()=>storedUrl } },
      outputChannel: { appendLine: line=>logs.push(line) },
      configOverride: { get:()=>undefined }, targetPathOverride:target,
    });
  } catch(error) {
    if(error.message!=='DOWNLOAD_REQUIRED') throw error;
  }
  assert.equal(downloadCalls,options.expectDownload?1:0,name);
  if(!options.expectDownload) assert.equal(result,target,name);
  if(options.expectReuse) assert(logs.some(line=>line.includes('after SHA512 manifest verification')),name);
  if(!options.probeVersion&&!options.missing) assert(logs.some(line=>line.includes('code=')&&line.includes('killed=')),name);
  console.log(JSON.stringify({ test:name, result:'PASS', probeCalls, downloadCalls, verifiedReuse:logs.some(line=>line.includes('after SHA512 manifest verification')) }));
  passed++;
}
(async()=>{
  await check('valid version skips download',{probeVersion:'1.4.6'});
  await check('failed probe and matching SHA512 reuse',{expectReuse:true});
  await check('timeout and matching SHA512 reuse',{timeout:true,expectReuse:true});
  await check('digest mismatch downloads',{manifest:{version:'1.4.6',sha512:'0'.repeat(128)},expectDownload:true});
  await check('missing binary downloads',{missing:true,expectDownload:true});
  await check('old working version downloads update',{probeVersion:'1.4.5',expectDownload:true});
  await check('release channel change downloads',{channelChange:true,expectDownload:true});
  await check('missing checksum downloads',{manifest:{version:'1.4.6'},expectDownload:true});
  await check('malformed checksum downloads',{manifest:{version:'1.4.6',sha512:'abc'},expectDownload:true});
  await check('file replaced while hashing downloads',{statRace:true,expectDownload:true});
  await check('offline valid version reuses',{probeVersion:'1.4.6',manifestFailure:true});
  await check('offline failed probe keeps original download path',{manifestFailure:true,expectDownload:true});
  const official=JSON.parse(fs.readFileSync(path.join(__dirname,'antigravity-release-windows-amd64-20261023.json'),'utf8'));
  await check('installed backend matches official manifest despite failed probe',{live:true,manifest:official,expectReuse:true});
  await check('installed backend real version probe',{live:true,manifest:official,realProbe:true,probeVersion:'real'});
  console.log('PASS '+passed+' behavioral checks; installer functions extracted from '+sourcePath);
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(()=>fs.rmSync(temp,{recursive:true,force:true}));