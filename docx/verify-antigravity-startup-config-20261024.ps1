param([switch]$Rollback)
$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $PSScriptRoot 'antigravity-startup-config-repair-20261024.json'
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$current = (Get-FileHash -LiteralPath $report.settings -Algorithm SHA256).Hash
$backup = (Get-FileHash -LiteralPath $report.backup -Algorithm SHA256).Hash
if ($backup -ne $report.beforeHash) { throw 'Backup hash mismatch.' }
if (-not $Rollback) {
    [pscustomobject]@{ Settings=$report.settings; MatchesRepaired=($current -eq $report.afterHash); BackupValid=$true }
    return
}
if ($current -ne $report.afterHash) { throw 'Settings changed after repair; refusing to overwrite newer edits.' }
Copy-Item -LiteralPath $report.backup -Destination $report.settings -Force
if ((Get-FileHash -LiteralPath $report.settings -Algorithm SHA256).Hash -ne $report.beforeHash) {
    throw 'Rollback verification failed.'
}
[pscustomobject]@{ Success=$true; Time=(Get-Date -Format o); Scope='Only VS Code user settings; no extension JS or Codex changes' } |
    ConvertTo-Json | Set-Content -LiteralPath ($reportPath+'.rollback.json') -Encoding UTF8
'Restored the settings backup. Antigravity startup timeout is back to its previous value.'
