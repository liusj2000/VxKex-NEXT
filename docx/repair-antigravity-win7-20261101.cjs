'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto'),{spawnSync}=require('child_process');
const workspace='D:/project/VxKex-NEXT';
const root=path.join(workspace,'.tools/antigravity-win7-20261101');
const target='D:/VSCode-win32-x64-1.107.1/data/extensions/google.google-antigravity-1.10.0/extension.js';
const original=path.join(root,'extension.js.original');
const candidate=path.join(root,'extension.js.candidate.cjs');
const recordPath=path.join(root,'repair.json');
const pinned='C:/Users/liusj/.gemini/bin/agy-win7-1.4.6.exe';
const expectedOriginal='1bf9b3213f43e1077cd85631d2f43d4cb6ad01a70ba45c3cf3465b4c2dd87d02';
const expectedBackend='56e6f15ce99d349ae8186d4e049a1ca8c7002e09447b5498da06581bd02843641bc95aa37046b0121dd3783c3a863135edbc0650b409a0e6b562c60e76240d22';
function sha(file,algo='sha256'){return crypto.createHash(algo).update(fs.readFileSync(file)).digest('hex')}
function checkJS(file){const r=spawnSync(process.execPath,['--check',file],{windowsHide:true,encoding:'utf8'});if(r.status!==0)throw Error('Syntax check failed: '+r.stderr)}
function save(record){fs.writeFileSync(recordPath,JSON.stringify(record,null,2)+'\n')}
function replaceOnce(text,old,replacement){const at=text.indexOf(old);if(at<0||text.indexOf(old,at+old.length)>=0)throw Error('Expected a unique patch anchor: '+old);return text.replace(old,replacement)}
const mode=process.argv[2]||'--prepare';
if(mode==='--prepare'){
 const pkg=JSON.parse(fs.readFileSync(path.join(path.dirname(target),'package.json'),'utf8'));
 if(pkg.version!=='1.10.0'||sha(target)!==expectedOriginal)throw Error('Extension version or original hash changed. Review before patching.');
 fs.mkdirSync(root,{recursive:true});
 if(fs.existsSync(original)&&sha(original)!==expectedOriginal)throw Error('A different original backup already exists.');
 if(!fs.existsSync(original))fs.copyFileSync(target,original);
 let source=fs.readFileSync(target,'utf8');
 const helper=fs.readFileSync(path.join(__dirname,'antigravity-win7-backend-20261101.cjs'),'utf8');
 source=replaceOnce(source,'Ma.acquireInstalledBinaryPath=Fc;',helper+'\nMa.acquireInstalledBinaryPath=kxAcquireWin7Backend;');
 source=replaceOnce(source,'this.binaryAcquirer=Fc;','this.binaryAcquirer=kxAcquireWin7Backend;');
 fs.writeFileSync(candidate,source);checkJS(candidate);
 const record={extensionVersion:pkg.version,target,backup:original,candidate,pinnedBackend:pinned,originalSha256:sha(original),patchedSha256:sha(candidate),backendVersion:'1.4.6',backendSha512:expectedBackend,preparedAt:new Date().toISOString()};
 save(record);console.log('PREPARED',JSON.stringify(record));
}else if(mode==='--apply'){
 const record=JSON.parse(fs.readFileSync(recordPath,'utf8'));
 if(sha(target)!==record.originalSha256||sha(original)!==record.originalSha256||sha(candidate)!==record.patchedSha256)throw Error('Extension or prepared files changed.');
 const downloaded=path.join(root,'agy-1.4.6.download.exe');
 if(sha(downloaded,'sha512')!==expectedBackend)throw Error('Downloaded backend hash mismatch.');
 if(fs.existsSync(pinned)&&sha(pinned,'sha512')!==expectedBackend)throw Error('Pinned backend path already contains different bytes; refusing to overwrite.');
 if(!fs.existsSync(pinned))fs.copyFileSync(downloaded,pinned);
 if(sha(pinned,'sha512')!==expectedBackend)throw Error('Installed compatible backend hash mismatch.');
 const version=spawnSync(pinned,['--version'],{windowsHide:true,encoding:'utf8',timeout:30000});
 if(version.status!==0||!/\b1\.4\.6\b/.test(version.stdout+' '+version.stderr))throw Error('Compatible backend version check failed: '+version.stderr);
 fs.copyFileSync(candidate,target);checkJS(target);
 if(sha(target)!==record.patchedSha256)throw Error('Installed extension hash mismatch.');
 record.appliedAt=new Date().toISOString();save(record);console.log('APPLIED',record.patchedSha256);
}else if(mode==='--rollback'){
 const record=JSON.parse(fs.readFileSync(recordPath,'utf8'));
 if(sha(original)!==record.originalSha256)throw Error('Original backup hash mismatch.');
 if(sha(target)!==record.patchedSha256)throw Error('Extension changed after repair; refusing rollback.');
 fs.copyFileSync(original,target);checkJS(target);
 if(sha(target)!==record.originalSha256)throw Error('Rollback hash mismatch.');
 record.rolledBackAt=new Date().toISOString();save(record);console.log('ROLLED_BACK_EXTENSION_ONLY');
}else if(mode==='--verify'){
 const record=JSON.parse(fs.readFileSync(recordPath,'utf8'));
 if(sha(original)!==record.originalSha256||sha(target)!==record.patchedSha256||sha(pinned,'sha512')!==expectedBackend)throw Error('Installed repair verification failed.');
 checkJS(target);console.log('INSTALLED_REPAIR_VERIFIED');
}else{throw Error('Unknown mode')}