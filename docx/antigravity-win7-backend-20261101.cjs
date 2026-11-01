// VXKEX_AGY_WIN7_BACKEND_V1: use an authentic, verified compatible backend on NT 6.1.
async function kxAcquireWin7Backend(options) {
    const os = require('node:os');
    if (process.platform !== 'win32' || !/^6\.1\./.test(os.release())) {
        return Fc(options);
    }
    const fs = require('node:fs');
    const path = require('node:path');
    const crypto = require('node:crypto');
    const {execFile} = require('node:child_process');
    const {promisify} = require('node:util');
    const backend = path.join(os.homedir(), '.gemini', 'bin', 'agy-win7-1.4.6.exe');
    const expected = '56e6f15ce99d349ae8186d4e049a1ca8c7002e09447b5498da06581bd02843641bc95aa37046b0121dd3783c3a863135edbc0650b409a0e6b562c60e76240d22';
    const started = Date.now();
    const fingerprint = stat => [stat.size, stat.mtimeMs, stat.ctimeMs, stat.ino].join(':');
    let before;
    try {
        before = await fs.promises.stat(backend);
    } catch (error) {
        throw new Error('Win7 compatible Antigravity backend is missing: ' + backend + '. Reapply the recorded Win7 repair. (' + error.code + ')');
    }
    if (before.size !== 204022936) throw new Error('Win7 compatible Antigravity backend has an unexpected size.');
    const digest = crypto.createHash('sha512');
    for await (const chunk of fs.createReadStream(backend)) digest.update(chunk);
    if (digest.digest('hex') !== expected) throw new Error('Win7 compatible Antigravity backend failed official SHA512 verification.');
    if (fingerprint(before) !== fingerprint(await fs.promises.stat(backend))) {
        throw new Error('Win7 compatible Antigravity backend changed during verification.');
    }
    const result = await promisify(execFile)(backend, ['--version'], {timeout:30000, windowsHide:true, maxBuffer:1048576});
    const match = (result.stdout + ' ' + result.stderr).match(/\b(\d+\.\d+\.\d+)(?=\s|$|[+)])/);
    if (!match || match[1] !== '1.4.6') throw new Error('Win7 compatible Antigravity backend did not report version 1.4.6.');
    if (fingerprint(before) !== fingerprint(await fs.promises.stat(backend))) {
        throw new Error('Win7 compatible Antigravity backend changed during the version check.');
    }
    options.outputChannel?.appendLine('[WIN7 COMPAT] Reusing existing backend v1.4.6 after SHA512 manifest verification. Backend v1.4.12 requires QueryInterruptTimePrecise; compatible version pinned on Windows 7.');
    options.onBinaryCheck?.({cacheHit:true,downloadReason:'up_to_date',cliVersion:'1.4.6',versionCheckMs:Date.now()-started,versionCheckSource:'spawn',versionCheckTimeout:false});
    return backend;
}