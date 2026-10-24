'use strict';
const fs=require('fs'),crypto=require('crypto'),vm=require('vm'),assert=require('assert');
const file='D:\\VSCode-win32-x64-1.107.1\\data\\extensions\\google.google-antigravity-1.9.0\\extension.js';
const source=fs.readFileSync(file,'utf8');
const start=source.indexOf('function Ac(a){'),end=source.indexOf('\nfunction Bc(',start);
assert(start>=0 && end>start,'timeout resolver anchor missing');
const resolver=source.slice(start,end);
const checks=[];
function test(name,configValue,envValue,expected) {
 const sandbox={process:{env:{}},pc:{DEFAULT_SERVER_STARTUP_TIMEOUT_MS:30000},
 n:{workspace:{getConfiguration:()=>({get:(key,fallback)=>configValue===undefined?fallback:configValue})}}};
 if(envValue!==undefined)sandbox.process.env.ANTIGRAVITY_SERVER_READY_TIMEOUT_MS=envValue;
 const actual=vm.runInNewContext(resolver+';Ac(undefined)',sandbox);
 assert.strictEqual(actual,expected,name);checks.push({name,actual,expected,passed:true});
}
test('old schema default',undefined,undefined,30000);
test('new user setting',120000,undefined,120000);
test('valid environment retains priority',120000,'45000',45000);
test('invalid environment falls through to setting',120000,'invalid',120000);
test('invalid config retains original default',-1,undefined,30000);
assert(source.includes('this.waitForServerReady(w,pa,'));
assert(source.includes('async isServerHealthy(a=2E3)'));
assert(source.includes('manifest verification'));
const result={checks,launchPassesResolvedTimeout:true,shortHealthProbeUnchanged:true,manifestReusePresent:true,
 extensionHash:crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')};
fs.writeFileSync('D:\\project\\VxKex-NEXT\\docx\\antigravity-timeout-resolver-test-20261024.json',JSON.stringify(result,null,2));
console.log(JSON.stringify(result));
