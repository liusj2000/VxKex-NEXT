'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const helper=fs.readFileSync(path.join(__dirname,'antigravity-win7-backend-20261101.cjs'),'utf8');
const expected='56e6f15ce99d349ae8186d4e049a1ca8c7002e09447b5498da06581bd02843641bc95aa37046b0121dd3783c3a863135edbc0650b409a0e6b562c60e76240d22';
const results=[];
function factory(change={}){
 const state={platform:'win32',release:'6.1.7601',hash:expected,size:204022936,version:'1.4.6',calls:0,delegations:0,...change};
 const stat=()=>({size:state.size,mtimeMs:10+(state.changed&&++state.calls>1?1:0),ctimeMs:20,ino:30});
 const modules={'node:os':{release:()=>state.release,homedir:()=>'/user'},'node:path':path,
  'node:fs':{promises:{stat:async()=>{if(state.missing)throw Object.assign(Error('missing'),{code:'ENOENT'});return stat()}},createReadStream:async function*(){yield Buffer.from('test')}},
  'node:crypto':{createHash:()=>({update(){},digest:()=>state.hash})},
  'node:child_process':{execFile:async()=>({stdout:state.version,stderr:''})},'node:util':{promisify:x=>x}};
 const fn=vm.runInNewContext(helper+';kxAcquireWin7Backend',{require:k=>modules[k],process:{platform:state.platform},Fc:async options=>{state.delegations++;return {original:true,options}},Date,Error});
 return {fn,state};
}
async function test(name,run){await run();results.push({name,passed:true});console.log('PASS',name)}
(async()=>{
 for(const [name,change]of [['non-Windows',{platform:'linux',release:'6.1.0'}],['Windows 10',{release:'10.0.19045'}]])await test(name+' retains original updater',async()=>{const {fn,state}=factory(change);const options={tag:1};const value=await fn(options);assert.equal(value.original,true);assert.equal(value.options,options);assert.equal(state.delegations,1)});
 await test('Win7 returns verified backend with its real version',async()=>{const {fn,state}=factory();let event;const result=await fn({onBinaryCheck:x=>event=x});assert.equal(path.basename(result),'agy-win7-1.4.6.exe');assert.equal(event.cliVersion,'1.4.6');assert.equal(state.delegations,0)});
 for(const [name,change,pattern]of [['missing file',{missing:true},/missing/],['wrong size',{size:1},/size/],['wrong digest',{hash:'bad'},/SHA512/],['file changes during verification',{changed:true},/changed/],['wrong reported version',{version:'1.4.12'},/version 1.4.6/]])await test('Win7 rejects '+name,async()=>{const {fn,state}=factory(change);await assert.rejects(fn({}),pattern);assert.equal(state.delegations,0)});
 const candidate=fs.readFileSync(path.join(__dirname,'../.tools/antigravity-win7-20261101/extension.js.candidate.cjs'),'utf8');
 await test('candidate routes exported and actual manager acquisition',async()=>{assert(candidate.includes('Ma.acquireInstalledBinaryPath=kxAcquireWin7Backend;'));assert(candidate.includes('this.binaryAcquirer=kxAcquireWin7Backend;'));assert(!candidate.includes('this.binaryAcquirer=Fc;'))});
 fs.writeFileSync(path.join(__dirname,'antigravity-win7-tests-20261101.json'),JSON.stringify({tests:results,passed:results.length},null,2)+'\n');
})().catch(e=>{console.error(e);process.exitCode=1});