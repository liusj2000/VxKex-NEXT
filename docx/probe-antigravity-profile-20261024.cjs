'use strict';
const fs=require('fs'),path=require('path'),http=require('http'),net=require('net');
const {spawn}=require('child_process');
const log=path.join(__dirname,'antigravity-profile-probe-20261024.jsonl');
fs.writeFileSync(log,'');
const record=x=>{const row=JSON.stringify({time:new Date().toISOString(),...x});fs.appendFileSync(log,row+'\n');if(x.event)console.log(row);};
const reserve=net.createServer();
reserve.listen(0,'127.0.0.1',()=>{const port=reserve.address().port;reserve.close(()=>{
 const started=Date.now();
 const child=spawn('C:\\Users\\liusj\\.gemini\\bin\\agy.exe',
 ['--hub','--hub-port='+port,'--app_data_dir=antigravity','--add-dir=D:\\project\\VxKex-NEXT'],
 {windowsHide:true,stdio:['ignore','pipe','pipe']});
 record({event:'spawn',pid:child.pid,port,profile:'original'});
 let done=false,timer,pollTimer;
 const stop=reason=>{if(done)return;done=true;clearTimeout(timer);clearTimeout(pollTimer);record({event:'stop',reason,elapsedMs:Date.now()-started});child.kill();};
 child.on('error',e=>{record({event:'spawn_error',message:e.message});stop('spawn_error');});
 child.on('exit',(code,signal)=>{done=true;clearTimeout(timer);clearTimeout(pollTimer);record({event:'exit',code,signal});});
 for(const stream of ['stdout','stderr'])child[stream].on('data',buf=>{
   const text=buf.toString().split('\n').filter(s=>!/token|authorization|cookie|csrf/i.test(s)).join('\n');
   if(text)record({stream,text});
 });
 timer=setTimeout(()=>stop('deadline_120s'),120000);
 let attempts=0;
 const poll=()=>{
   if(done)return;
   const endpoint=attempts++%2===0?'/healthz':'/';
   const req=http.get({host:'127.0.0.1',port,path:endpoint,agent:false},res=>{
     res.resume();
     record({event:'http',endpoint,status:res.statusCode,elapsedMs:Date.now()-started});
     if(res.statusCode===200)stop('ready');
     else pollTimer=setTimeout(poll,750);
   });
   req.setTimeout(2000,()=>req.destroy(new Error('probe_timeout')));
   req.on('error',e=>{if(done)return;record({event:'probe_error',endpoint,message:e.message,elapsedMs:Date.now()-started});pollTimer=setTimeout(poll,750);});
 };
 poll();
});});
