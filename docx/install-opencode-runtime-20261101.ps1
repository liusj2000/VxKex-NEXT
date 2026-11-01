$ErrorActionPreference='Stop'
$root='D:\project\VxKex-NEXT'
$source=Join-Path $root 'docx\opencode-win7-console.ps1'
$helper='C:\Users\liusj\Documents\PowerShell\VxKex.OpenCode.ps1'
if(Test-Path -LiteralPath $helper) { throw 'Helper already exists; inspect before replacing' }
$backup=Join-Path $root ('.tools\opencode-runtime-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$manifest=@()
$paths=@('C:\Users\liusj\Documents\PowerShell\profile.ps1','C:\Users\liusj\Documents\WindowsPowerShell\profile.ps1')
$block="# VxKex OpenCode runtime CP936 guard (20261101)" + [Environment]::NewLine + ". 'C:\Users\liusj\Documents\PowerShell\VxKex.OpenCode.ps1'" + [Environment]::NewLine
for($i=0;$i -lt $paths.Count;$i++) {
    $path=$paths[$i]; $original=[IO.File]::ReadAllText($path)
    if($original.Contains('VxKex.OpenCode.ps1')) { throw 'Profile already loads a helper' }
    $save=Join-Path $backup ("profile-$i.original.ps1")
    Copy-Item -LiteralPath $path -Destination $save
    $candidate=Join-Path $backup ("profile-$i.candidate.ps1")
    [IO.File]::WriteAllText($candidate,$original.TrimEnd()+[Environment]::NewLine+[Environment]::NewLine+$block,[Text.UTF8Encoding]::new($false))
    $manifest += [pscustomobject]@{Path=$path;Backup=$save;Candidate=$candidate;BeforeSHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;AfterSHA256=(Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash}
}
foreach($path in @($source)+@($manifest.Candidate)) {
    $tokens=$null;$parseErrors=$null
    [void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count) { throw ($parseErrors | Out-String) }
}
$helperHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
@{Profiles=$manifest;Helper=$helper;HelperSHA256=$helperHash} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
foreach($item in $manifest) {
    if((Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.BeforeSHA256) { throw 'Profile changed during preparation' }
}
$written=@()
try {
    Copy-Item -LiteralPath $source -Destination $helper
    foreach($item in $manifest) {
        Copy-Item -LiteralPath $item.Candidate -Destination $item.Path
        $written += $item
        if((Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.AfterSHA256) { throw 'Profile hash mismatch' }
    }
    if((Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash -ne $helperHash) { throw 'Helper hash mismatch' }
} catch {
    foreach($item in $written) { Copy-Item -LiteralPath $item.Backup -Destination $item.Path }
    if(Test-Path -LiteralPath $helper) { Remove-Item -LiteralPath $helper }
    throw
}
Write-Output ("Backup=" + $backup)
Write-Output ("HelperSHA256=" + $helperHash)
