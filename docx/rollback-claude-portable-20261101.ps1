[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference='Stop'
$record=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'before.json') -Raw | ConvertFrom-Json
$expected='D:\project\claude-code-engineering\node-v22.22.2-win-x64'
if($record.prefix -ne $expected){throw 'Unexpected prefix.'}
$target=Join-Path $expected 'node_modules\@anthropic-ai\claude-code'
$old=Join-Path $PSScriptRoot 'claude-code'
function Test-Record($Files,[string]$Package,[string]$Launchers){
    foreach($f in $Files){
        if($f.relativePath.StartsWith('claude-code\')){$path=Join-Path $Package $f.relativePath.Substring(12)}else{$path=Join-Path $Launchers $f.relativePath}
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $f.sha256){throw ('Hash mismatch: '+$path)}
    }
}
Test-Record $record.files $old $PSScriptRoot
if(!$Apply){Write-Output 'BACKUP_VERIFIED. Use -Apply to restore Node 2.1.112.';return}
$after=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'after.json') -Raw | ConvertFrom-Json
Test-Record $after.files $target $expected
$running=Get-CimInstance Win32_Process | Where-Object {$_.Name -ieq 'claude.exe' -and $_.ExecutablePath -and $_.ExecutablePath.StartsWith($target,[System.StringComparison]::OrdinalIgnoreCase)}
if($running){throw 'Close the portable Claude process first.'}
$retired=Join-Path $PSScriptRoot 'native-after-rollback'
if(Test-Path -LiteralPath $retired){throw 'Rollback destination exists.'}
$root=[System.IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')+'\'
if(![System.IO.Path]::GetFullPath($retired).StartsWith($root,[System.StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe rollback destination.'}
if([System.IO.Path]::GetFullPath($target) -ne ($expected+'\node_modules\@anthropic-ai\claude-code')){throw 'Unsafe rollback target.'}
Move-Item -LiteralPath $target -Destination $retired
Copy-Item -LiteralPath $old -Destination $target -Recurse
foreach($name in @('claude','claude.cmd','claude.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $expected $name) -Force}
Test-Record $record.files $target $expected
Write-Output 'RESTORED_AND_VERIFIED Node 2.1.112.'
