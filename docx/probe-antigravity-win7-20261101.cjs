'use strict';
const fs=require('fs'),path=require('path'),os=require('os'),vm=require('vm'),net=require('net'),http=require('http'),crypto=require('crypto');
const {spawn}=require('child_process');
const workspace='D:/project/VxKex-NEXT', root=path.join(workspace,'.tools/antigravity-win7-20261101');
const ext=process.argv.includes('--installed')?'D:/VSCode-win32-x64-1.107.1/data/extensions/google.google-antigravity-1.10.0/extension.js':path.join(root,'extension.js.candidate.cjs');
const source=fs.readFileSync(ext,'utf8');
const label=process.argv.includes('--installed')?'installed':'candidate';
let backend=process.argv.includes('--installed')?'C:/Users/liusj/.gemini/bin/agy-win7-1.4.6.exe':path.join(root,'agy-1.4.6.download.exe');
const logFile=path.join(root,'health-'+label+'.jsonl');fs.writeFileSync(logFile,'');
function record(row){const text=JSON.stringify({time:new Date().toISOString(),...row});fs.appendFileSync(logFile,text+'\n');if(row.event!=='probe_error')console.log(text)}
const settings=fs.readFileSync('D:/VSCode-win32-x64-1.107.1/data/user-data/User/settings.json','utf8');
function setting(key){const m=settings.match(new RegExp('"'+key.replace(/\./g,'\\.')+'"\\s*:\\s*("(?:[^"\\\\]|\\\\.)*")'));return m?JSON.parse(m[1]):undefined}
const start=source.indexOf('function ad('),end=source.indexOf('$c.buildServerEnvironment=fd;',start);
if(start<0||end<start)throw Error('Environment builder anchors missing');
const envBuilder=vm.runInNewContext(source.slice(start,end)+';fd',{process,la:fs,Ha:os,$c:{},n:{env:{uriScheme:'vscode'}}});
const env=envBuilder({baseEnv:process.env,configOverride:{get:k=>setting('http.'+k)}});
const expected=JSON.parse(fs.readFileSync(path.join(workspace,'docx/antigravity-release-windows-amd64-20261023.json'),'utf8').replace(/^\uFEFF/,''));
(async()=>{
 if(process.argv.includes('--installed')) {
  const begin=source.indexOf('// VXKEX_AGY_WIN7_BACKEND_V1:'), end=source.indexOf('Ma.acquireInstalledBinaryPath=kxAcquireWin7Backend;',begin);
  if(begin<0||end<begin)throw Error('Installed repair helper not found');
  const acquire=vm.runInNewContext(source.slice(begin,end)+';kxAcquireWin7Backend',{require,process,Fc:()=>{throw Error('Unexpected incompatible updater invocation')}});
  backend=await acquire({outputChannel:{appendLine:text=>record({event:'acquire',text})},onBinaryCheck:event=>record({event:'binary_check',...event})});
 }
 const digest=crypto.createHash('sha512');for await(const chunk of fs.createReadStream(backend))digest.update(chunk);if(digest.digest('hex')!==expected.sha512)throw Error('Backend hash mismatch');
 const reserve=net.createServer();await new Promise(r=>reserve.listen(0,'127.0.0.1',r));const port=reserve.address().port;await new Promise(r=>reserve.close(r));
 const began=Date.now(),child=spawn(backend,['--hub','--hub-port='+port,'--app_data_dir=antigravity','--csrf_token=local-win7-repair-validation','--add-dir='+workspace],{env,cwd:workspace,windowsHide:true,stdio:['ignore','pipe','pipe']});
 record({event:'spawn',pid:child.pid,port,backend,proxy:env.HTTPS_PROXY?.replace(/(\/\/)[^/@]+@/,'$1<redacted>@')});
 let done=false,summary={backend,pid:child.pid,port,healthStatus:null,pageStatus:null},pollTimer;
 const finish=reason=>{if(done)return;done=true;clearTimeout(deadline);clearTimeout(pollTimer);summary.reason=reason;summary.elapsedMs=Date.now()-began;fs.writeFileSync(path.join(root,'health-'+label+'-summary.json'),JSON.stringify(summary,null,2));record({event:'finish',...summary});child.kill();setTimeout(()=>process.exit(summary.healthStatus===200&&summary.pageStatus===200?0:1),500)};
 for(const name of ['stdout','stderr'])child[name].on('data',d=>{const text=d.toString().split(/\r?\n/).filter(x=>x&&!/token|csrf|authorization|cookie|api.?key/i.test(x)).join('\n');if(text)record({event:'log',stream:name,text:text.slice(-6000)})});
 child.on('error',e=>{summary.spawnError=e.code;finish('spawn_error')});child.on('exit',(code,signal)=>{record({event:'exit',code,signal});if(!done){summary.exitCode=code;finish('early_exit')}});
 const deadline=setTimeout(()=>finish('startup_timeout_120s'),120000);
 function request(endpoint,cb){const req=http.get({host:'127.0.0.1',port,path:endpoint,agent:false},res=>{let bytes=0;res.on('data',d=>bytes+=d.length);res.on('end',()=>cb(null,{status:res.statusCode,bytes,contentType:res.headers['content-type']}))});req.setTimeout(2000,()=>req.destroy(Error('probe_timeout')));req.on('error',cb)}
 function poll(){if(done)return;request('/healthz',(e,result)=>{if(done)return;if(!e&&result.status===200){summary.healthStatus=200;summary.readyMs=Date.now()-began;record({event:'health',...result,readyMs:summary.readyMs});request('/',(error,page)=>{if(error){summary.pageError=error.message;finish('page_error')}else{summary.pageStatus=page.status;record({event:'page',...page});finish('ready')}})}else{record({event:'probe_error',message:e?.message,status:result?.status});pollTimer=setTimeout(poll,700)}})}poll();
})().catch(e=>{console.error(e);process.exitCode=1});