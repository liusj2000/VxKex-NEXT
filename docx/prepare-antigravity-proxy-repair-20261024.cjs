'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto'),vm=require('vm'),assert=require('assert'),cp=require('child_process');
const dir=__dirname;
const installed='D:\\VSCode-win32-x64-1.107.1\\data\\extensions\\google.google-antigravity-1.9.0\\extension.js';
const candidate=path.join(dir,'antigravity-proxy-candidate-20261024.js');
const report=path.join(dir,'antigravity-proxy-repair-20261024.json');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const original=fs.readFileSync(installed);
assert.equal(hash(original),'57027f41332aedd78e91917f2a2bd8e059edcd612e63ae6aff72a08af2c61a06');
const source=original.toString('utf8');
const before='c.HTTP_PROXY||\nc.http_proxy||(c.HTTP_PROXY=k,c.http_proxy=k),c.HTTPS_PROXY||c.https_proxy||(c.HTTPS_PROXY=k,c.https_proxy=k)';
const after='(c.HTTP_PROXY=k,c.http_proxy=k,c.HTTPS_PROXY=k,c.https_proxy=k)/* VxKex repair: explicit VS Code http.proxy takes precedence for this backend */';
assert.equal(source.split(before).length,2);
const revised=source.replace(before,after);
fs.writeFileSync(candidate,revised);
const checked=cp.spawnSync(process.execPath,['--check',candidate],{encoding:'utf8',windowsHide:true});
assert.equal(checked.status,0,checked.stderr);
function resolve(src,baseEnv,proxy){
 const start=src.indexOf('function xc(a){'),end=src.indexOf('\npc.buildServerEnvironment=xc;',start);
 assert(start>=0&&end>start);
 const env={...baseEnv};
 const sandbox={process:{env:{},platform:'win32'},la:{existsSync:()=>false},Ha:{homedir:()=> 'C:\\Users\\liusj'},n:{env:{uriScheme:'vscode'}},qc:()=>undefined,vc:s=>s||'localhost,127.0.0.1'};
 const f=vm.runInNewContext(src.slice(start,end)+';xc',sandbox);
 const result=f({baseEnv:env,platform:'win32',configOverride:{get:key=>key==='proxy'?proxy:undefined}});
 assert.deepEqual(env,baseEnv,'caller environment mutated');return result;
}
const old='http://127.0.0.1:10809',current='http://10.0.0.1:10809';
const base={HTTP_PROXY:old,HTTPS_PROXY:old,NO_PROXY:'localhost,127.0.0.1',KEEP:'unchanged',SSL_CERT_FILE:'existing-ca'};
const oldActual=resolve(source,base,current);assert.equal(oldActual.HTTPS_PROXY,old);
const checks=[];
for(const [name,env,proxy,expected] of [
 ['explicit config replaces stale environment',base,current,current],
 ['no explicit config preserves environment',base,undefined,old],
 ['blank explicit config preserves environment',base,'  ',old],
 ['trim explicit config',base,' '+current+' ',current],
 ['lowercase environment is replaced',{http_proxy:old,https_proxy:old},current,current],
 ['explicit config works without environment',{},current,current]
]){
 const r=resolve(revised,env,proxy);
 assert.equal(r.HTTPS_PROXY||r.https_proxy,expected);
 if(proxy&&proxy.trim())for(const key of ['HTTP_PROXY','HTTPS_PROXY','http_proxy','https_proxy'])assert.equal(r[key],expected);
 for(const key of ['NO_PROXY','KEEP','SSL_CERT_FILE'])if(env[key])assert.equal(r[key],env[key]);
 assert.equal(r.ANTIGRAVITY_VSCODE_HOST,'1');
 checks.push({name,passed:true});
}
assert(revised.includes('manifest verification'));
assert(revised.includes('async isServerHealthy(a=2E3)'));
const result={time:new Date().toISOString(),installed,candidate,beforeHash:hash(original),candidateHash:hash(Buffer.from(revised)),status:'candidate_only_not_applied',nodeCheckExitCode:checked.status,oldResolverSelectedStaleProxy:true,checks,change:'Only backend environment: explicit http.proxy takes precedence; no explicit proxy preserves inherited environment',manifestReusePreserved:true};
fs.writeFileSync(report,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
