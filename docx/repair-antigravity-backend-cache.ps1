[CmdletBinding()]
param(
    [ValidateSet('Preview','Apply','Rollback','Verify')][string]$Mode = 'Verify',
    [string]$ExtensionPath = 'D:\VSCode-win32-x64-1.107.1\data\extensions\google.google-antigravity-1.9.0',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$target = Join-Path $ExtensionPath 'extension.js'
$backupRoot = Join-Path (Split-Path (Split-Path $ExtensionPath -Parent) -Parent) 'antigravity-repair-backups\google.google-antigravity-1.9.0'
$backupFile = Join-Path $backupRoot 'extension.js.original'
$recordFile = Join-Path $backupRoot 'repair.json'
$marker = 'VXKEX_AGY_VERIFIED_REUSE_V1'
function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
if ($Mode -eq 'Rollback') {
    $record = Get-Content -LiteralPath $recordFile -Raw | ConvertFrom-Json
    if ((Get-Sha256 $backupFile) -ne $record.originalSha256) { throw 'Backup hash mismatch.' }
    if ((Get-Sha256 $target) -ne $record.patchedSha256) { throw 'Extension changed since repair; rollback refused.' }
    Copy-Item -LiteralPath $backupFile -Destination $target -Force
    Write-Output "ROLLED_BACK SHA256=$(Get-Sha256 $target)"
    return
}
$package = Get-Content -LiteralPath (Join-Path $ExtensionPath 'package.json') -Raw | ConvertFrom-Json
if ($package.version -ne '1.9.0') { throw 'This repair is scoped to extension version 1.9.0.' }
$source = [IO.File]::ReadAllText($target)
if ($source.Contains($marker)) {
    if ($Mode -eq 'Preview' -and $OutputPath) { [IO.File]::WriteAllText($OutputPath,$source,[Text.UTF8Encoding]::new($false)) }
    Write-Output "ALREADY_PATCHED SHA256=$(Get-Sha256 $target)"
    if ($Mode -eq 'Verify') {
        $record = Get-Content -LiteralPath $recordFile -Raw | ConvertFrom-Json
        if ((Get-Sha256 $target) -ne $record.patchedSha256) { throw 'Patched extension hash differs from repair record.' }
        if ((Get-Sha256 $backupFile) -ne $record.originalSha256) { throw 'Backup hash mismatch.' }
        Write-Output 'PATCH_AND_BACKUP_VERIFIED'
    }
    return
}
if ($Mode -eq 'Verify') { throw 'Repair not installed.' }
function Replace-Once([string]$Old,[string]$New) {
    $first = $script:source.IndexOf($Old,[StringComparison]::Ordinal)
    if ($first -lt 0 -or $script:source.IndexOf($Old,$first+$Old.Length,[StringComparison]::Ordinal) -ge 0) { throw "Expected one patch anchor: $Old" }
    $script:source = $script:source.Replace($Old,$New)
}
$helper = @"
// VXKEX_AGY_VERIFIED_REUSE_V1: reuse only exact manifest-verified bytes.
async function kxReuseVerifiedBackend(binaryPath,manifest,outputChannel){
  try{
    if(!await Oa(binaryPath))return false;
    const info=`$b(manifest,outputChannel);
    const algorithm=info.sha512?'sha512':'sha256';
    const expected=info.sha512||info.sha256;
    if(typeof expected!=='string'||!new RegExp(algorithm==='sha512'?'^[0-9a-f]{128}`$':'^[0-9a-f]{64}`$','i').test(expected))return false;
    const before=await lb(binaryPath);
    const actual=await Gb(binaryPath,algorithm);
    if(before!==await lb(binaryPath)||actual.toLowerCase()!==expected.toLowerCase()){
      outputChannel.appendLine('[INSTALL] Existing backend does not match release manifest; keeping normal download path.');
      return false;
    }
    jb.set(before,manifest.version);
    outputChannel.appendLine('[INSTALL] Reusing existing backend v'+manifest.version+' after '+algorithm.toUpperCase()+' manifest verification (version probe failed).');
    return true;
  }catch(error){
    outputChannel.appendLine('[INSTALL] Existing backend manifest verification unavailable: '+Wa(error));
    return false;
  }
}
"@
Replace-Once 'async function dc(a)' ($helper + "`nasync function dc(a)")
Replace-Once 'let Nb=Ma.MIN_AGY_VERSION;a=!1;' 'let Nb=Ma.MIN_AGY_VERSION,localReleaseManifest;a=!1;'
Replace-Once 'Nb=Bf.version,a=!0' 'Nb=Bf.version,a=!0,localReleaseManifest=Bf'
Replace-Once 'Nb=Xb.version,a=!0' 'Nb=Xb.version,a=!0,localReleaseManifest=Xb'
Replace-Once 'let Sb=g?void 0:b(await c(qb(D,Nb,m)));' 'if(!g&&!kb.version&&localReleaseManifest)b(await c(kxReuseVerifiedBackend(D,localReleaseManifest,m)));let Sb=g?void 0:b(await c(qb(D,Nb,m)));'
Replace-Once 'Na(a,["--version"],{timeout:5E3})' 'Na(a,["--version"],{timeout:30E3,windowsHide:!0})'
Replace-Once '[INSTALL] Failed to execute binary ${a}: ${m}' '[INSTALL] Failed to execute binary ${a}: ${m}; code=${g==null?void 0:g.code}, signal=${g==null?void 0:g.signal}, killed=${g==null?void 0:g.killed}'
Replace-Once 'm.appendLine(`[INSTALL] Antigravity Backend successfully installed to ${D}.`);' 'Nb=Kc.targetVersion;m.appendLine(`[INSTALL] Antigravity Backend successfully installed to ${D}.`);'
if ($Mode -eq 'Preview') {
    if (!$OutputPath) { throw 'Preview requires OutputPath.' }
    [IO.File]::WriteAllText($OutputPath,$source,[Text.UTF8Encoding]::new($false))
    Write-Output "PREVIEW=$OutputPath SHA256=$(Get-Sha256 $OutputPath)"
    return
}
$originalHash = Get-Sha256 $target
if (Test-Path -LiteralPath $backupFile) {
    if ((Get-Sha256 $backupFile) -ne $originalHash) { throw 'A different original backup already exists.' }
} else {
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $backupFile
}
if ((Get-Sha256 $backupFile) -ne $originalHash) { throw 'Backup verification failed.' }
$staging = $target + '.repair-staging'
[IO.File]::WriteAllText($staging,$source,[Text.UTF8Encoding]::new($false))
$patchedHash = Get-Sha256 $staging
@{ extensionVersion='1.9.0'; originalSha256=$originalHash; patchedSha256=$patchedHash; backupFile=$backupFile; installedAt=(Get-Date).ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath $recordFile -Encoding UTF8
if ((Get-Sha256 $target) -ne $originalHash) { throw 'Extension changed during repair.' }
Move-Item -LiteralPath $staging -Destination $target -Force
if ((Get-Sha256 $target) -ne $patchedHash) { throw 'Installed patch hash mismatch.' }
Write-Output "APPLIED ORIGINAL_SHA256=$originalHash PATCHED_SHA256=$patchedHash BACKUP=$backupFile"