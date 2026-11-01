[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Stage,[switch]$Apply)
$ErrorActionPreference='Stop'
$workspace='D:\project\VxKex-NEXT'
$prefix='D:\project\claude-code-engineering\node-v22.22.2-win-x64'
$target=Join-Path $prefix 'node_modules\@anthropic-ai\claude-code'
$stageRoot=[System.IO.Path]::GetFullPath($Stage).TrimEnd('\')
if(!$stageRoot.StartsWith(($workspace+'\.tools\claude-native-2.1.316-'),[System.StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected stage path.'}
$source=Join-Path $stageRoot 'node_modules\@anthropic-ai\claude-code'
$pkg=Get-Content -LiteralPath (Join-Path $source 'package.json') -Raw | ConvertFrom-Json
if($pkg.version -ne '2.1.316' -or $pkg.bin.claude -ne 'bin/claude.exe'){throw 'Unexpected staged package.'}
if((Get-Item -LiteralPath (Join-Path $source 'bin\claude.exe')).Length -lt 10000000){throw 'Native binary missing.'}
foreach($name in @('claude','claude.cmd','claude.ps1')){
    $text=[System.IO.File]::ReadAllText((Join-Path $stageRoot $name))
    if(!$text.Contains('bin/claude.exe') -and !$text.Contains('bin\claude.exe')){throw ('Unexpected launcher: '+$name)}
    if($text.Contains('DISABLE_AUTOUPDATER')){throw 'Launcher still pins updates.'}
}
function Get-Manifest([string]$PackageRoot,[string]$LauncherRoot){
    foreach($f in (Get-ChildItem -LiteralPath $PackageRoot -Recurse -File -Force)){
        if(($f.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Unexpected reparse point.'}
        [pscustomobject]@{relativePath=('claude-code\'+$f.FullName.Substring($PackageRoot.Length).TrimStart('\'));sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash}
    }
    foreach($name in @('claude','claude.cmd','claude.ps1')){
        [pscustomobject]@{relativePath=$name;sha256=(Get-FileHash -LiteralPath (Join-Path $LauncherRoot $name) -Algorithm SHA256).Hash}
    }
}
function Test-Manifest($Files,[string]$PackageRoot,[string]$LauncherRoot){
    foreach($f in $Files){
        if($f.relativePath.StartsWith('claude-code\')){$path=Join-Path $PackageRoot $f.relativePath.Substring(12)}else{$path=Join-Path $LauncherRoot $f.relativePath}
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $f.sha256){throw ('Hash mismatch: '+$path)}
    }
}
$current=Get-Content -LiteralPath (Join-Path $target 'package.json') -Raw | ConvertFrom-Json
if($current.version -ne '2.1.112'){throw 'Current package changed.'}
$running=Get-CimInstance Win32_Process | Where-Object {
    ($_.Name -ieq 'claude.exe' -and $_.ExecutablePath -and $_.ExecutablePath.StartsWith($target,[System.StringComparison]::OrdinalIgnoreCase)) -or
    ($_.Name -ieq 'node.exe' -and $_.CommandLine -and $_.CommandLine.Contains($target) -and $_.CommandLine.Contains('cli.js'))
}
if($running){throw 'A process is using the portable Claude installation.'}
$oldFiles=@(Get-Manifest $target $prefix)
$newFiles=@(Get-Manifest $source $stageRoot)
if(!$Apply){[pscustomobject]@{Ready=$true;OldVersion=$current.version;NewVersion=$pkg.version;Files=$newFiles.Count} | ConvertTo-Json;return}
$backup=[System.IO.Path]::GetFullPath((Join-Path $workspace ('.tools\claude-node-before-latest-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))))
if(!$backup.StartsWith(($workspace+'\.tools\'),[System.StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe backup path.'}
if([System.IO.Path]::GetFullPath($target) -ne ($prefix+'\node_modules\@anthropic-ai\claude-code')){throw 'Unsafe target path.'}
New-Item -ItemType Directory -Path $backup | Out-Null
$before=[ordered]@{version=$current.version;prefix=$prefix;files=$oldFiles}
[System.IO.File]::WriteAllText((Join-Path $backup 'before.json'),($before | ConvertTo-Json -Depth 6),[System.Text.UTF8Encoding]::new($false))
foreach($name in @('claude','claude.cmd','claude.ps1')){Copy-Item -LiteralPath (Join-Path $prefix $name) -Destination (Join-Path $backup $name)}
Copy-Item -LiteralPath (Join-Path $workspace 'docx\rollback-claude-portable-20261101.ps1') -Destination (Join-Path $backup 'rollback.ps1')
$moved=$false
try {
    Move-Item -LiteralPath $target -Destination (Join-Path $backup 'claude-code')
    $moved=$true
    Test-Manifest $oldFiles (Join-Path $backup 'claude-code') $backup
    Copy-Item -LiteralPath $source -Destination $target -Recurse
    foreach($name in @('claude','claude.cmd','claude.ps1')){Copy-Item -LiteralPath (Join-Path $stageRoot $name) -Destination (Join-Path $prefix $name) -Force}
    Test-Manifest $newFiles $target $prefix
    $after=[ordered]@{version=$pkg.version;prefix=$prefix;stage=$stageRoot;backup=$backup;files=$newFiles}
    $json=$after | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText((Join-Path $backup 'after.json'),$json,[System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText((Join-Path $workspace '.tools\claude-latest-deployment.json'),$json,[System.Text.UTF8Encoding]::new($false))
    [pscustomobject]@{Deployed=$true;Version=$pkg.version;Prefix=$prefix;Backup=$backup;FilesVerified=$newFiles.Count} | ConvertTo-Json
} catch {
    $problem=$_
    if($moved){
        $failed=Join-Path $backup 'failed-deploy'
        if(![System.IO.Path]::GetFullPath($failed).StartsWith(($backup+'\'),[System.StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe recovery path.'}
        if(Test-Path -LiteralPath $target){Move-Item -LiteralPath $target -Destination $failed}
        Move-Item -LiteralPath (Join-Path $backup 'claude-code') -Destination $target
        foreach($name in @('claude','claude.cmd','claude.ps1')){Copy-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $prefix $name) -Force}
        Test-Manifest $oldFiles $target $prefix
    }
    throw $problem
}
