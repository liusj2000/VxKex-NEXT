const fs=require('fs'),path=require('path'),net=require('net'),{spawn}=require('child_process');
const exe='D:\\project\\claude-code-engineering\\node-v22.22.2-win-x64\\node_modules\\@anthropic-ai\\claude-code\\bin\\claude.exe';
const pty=require('D:/VSCode-win32-x64-1.107.1/resources/app/node_modules/node-pty');
const root=path.join(__dirname,'claude-network-probe-20261101');
async function run(label,proxy){
 const dir=path.join(root,label); fs.mkdirSync(dir,{recursive:true});
 const env={...process.env}; for(const k of Object.keys(env))if(/proxy|anthropic|claude|bun_be_bun/i.test(k))delete env[k];
 Object.assign(env,{CLAUDE_CONFIG_DIR:dir,HTTP_PROXY:proxy,HTTPS_PROXY:proxy,NO_PROXY:'',DISABLE_AUTOUPDATER:'1',CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC:'1'});
 console.log('CASE',label);
 await new Promise(resolve=>{
  const p=pty.spawn(exe,['--debug-file',path.join(dir,'debug.txt')],{cwd:dir,env,useConpty:false,cols:110,rows:30,name:'xterm-256color'});let out='';
  p.onData(x=>out+=x);
  const timer=setTimeout(()=>p.kill(),15000);
  p.onExit(event=>{clearTimeout(timer);fs.writeFileSync(path.join(dir,'output.txt'),out);console.log('EXIT',event.exitCode,'ENOTSUP='+out.includes('ENOTSUP'));resolve()});
 });
}
(async()=>{
 await run('configured-proxy','http://127.0.0.1:10809');
 let connections=0,requests=0;const sockets=new Set();
 const server=net.createServer(s=>{connections++;sockets.add(s);s.on('close',()=>sockets.delete(s));s.on('error',()=>{});s.once('data',d=>{requests++;console.log('LOCAL_PROXY_REQUEST',d.toString().split('\r\n')[0]);s.end('HTTP/1.1 502 Diagnostic proxy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')})});
 await new Promise(r=>server.listen(0,'127.0.0.1',r));
 await new Promise((resolve,reject)=>{require('http').get({host:'127.0.0.1',port:server.address().port,path:'/'},r=>{console.log('CONTROL_HTTP',r.statusCode);r.resume();r.on('end',resolve)}).on('error',reject)});
 connections=0;requests=0;
 await run('local-proxy','http://127.0.0.1:'+server.address().port);
 console.log('LOCAL_PROXY_COUNTS',JSON.stringify({connections,requests}));fs.writeFileSync(path.join(root,'summary.json'),JSON.stringify({time:new Date().toISOString(),connections,requests},null,2));for(const s of sockets)s.destroy();server.close(()=>process.exit(0));
})().catch(e=>{console.error(e);process.exitCode=1});