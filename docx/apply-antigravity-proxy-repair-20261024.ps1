param([switch]$Rollback)
$ErrorActionPreference='Stop'
$reportPath=Join-Path $PSScriptRoot 'antigravity-proxy-repair-20261024.json'
$r=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
function Hash($path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
if($Rollback){
 if(-not $r.backup){throw 'No applied backup recorded'}
 if((Hash $r.installed) -ne $r.candidateHash){throw 'Installed file changed; refusing rollback'}
 if((Hash $r.backup) -ne $r.beforeHash){throw 'Backup hash mismatch'}
 [IO.File]::WriteAllBytes($r.installed,[IO.File]::ReadAllBytes($r.backup))
 $r.status='rolled_back'
}else{
 if((Hash $r.installed) -ne $r.beforeHash){throw 'Installed extension changed; regenerate candidate'}
 if((Hash $r.candidate) -ne $r.candidateHash){throw 'Candidate hash mismatch'}
 $backupDir='D:\VSCode-win32-x64-1.107.1\data\antigravity-proxy-backup-'+(Get-Date -Format yyyyMMdd-HHmmss)
 [IO.Directory]::CreateDirectory($backupDir) | Out-Null
 $backup=Join-Path $backupDir 'extension.js.original'
 [IO.File]::WriteAllBytes($backup,[IO.File]::ReadAllBytes($r.installed))
 if((Hash $backup) -ne $r.beforeHash){throw 'Backup verification failed'}
 [IO.File]::WriteAllBytes($r.installed,[IO.File]::ReadAllBytes($r.candidate))
 if((Hash $r.installed) -ne $r.candidateHash){throw 'Installed hash verification failed'}
 $r | Add-Member -NotePropertyName backup -NotePropertyValue $backup -Force
 $r | Add-Member -NotePropertyName appliedAt -NotePropertyValue (Get-Date -Format o) -Force
 $r.status='applied_pending_extension_reload'
}
[IO.File]::WriteAllText($reportPath,($r | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$r | Select-Object status,installed,backup,beforeHash,candidateHash | Format-List
