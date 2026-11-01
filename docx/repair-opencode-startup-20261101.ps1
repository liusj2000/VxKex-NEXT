$ErrorActionPreference = 'Stop'
$root = 'D:\project\VxKex-NEXT'
$backup = Join-Path $root ('.tools\opencode-startup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$paths = @(
 'C:\Users\liusj\Documents\PowerShell\profile.ps1',
 'C:\Users\liusj\Documents\PowerShell\Microsoft.PowerShell_profile.ps1',
 'C:\Users\liusj\Documents\WindowsPowerShell\profile.ps1',
 'C:\Users\liusj\Documents\WindowsPowerShell\Microsoft.PowerShell_profile.ps1'
)
$block = @'
# BEGIN VxKex OpenCode CP936 startup fix 20261101
# Win7 console CJK cell widths require CP936, even when UTF-8 was inherited.
try {
    [Console]::InputEncoding = [System.Text.Encoding]::GetEncoding(936)
    [Console]::OutputEncoding = [System.Text.Encoding]::GetEncoding(936)
} catch {
    Write-Warning ("Could not restore console CP936: " + $_.Exception.Message)
}
$env:OTUI_USE_ALTERNATE_SCREEN = "true"
if (Get-Module -Name PSReadLine) {
    Remove-Module PSReadLine -ErrorAction SilentlyContinue
}
# END VxKex OpenCode CP936 startup fix 20261101
'@
$manifest = @()
for ($i = 0; $i -lt $paths.Count; $i++) {
    $path = $paths[$i]
    $original = [IO.File]::ReadAllText($path)
    if ($original.Contains('# BEGIN VxKex OpenCode CP936')) { throw "Fix already present: $path" }
    $save = Join-Path $backup ("profile-$i.original.ps1")
    Copy-Item -LiteralPath $path -Destination $save
    $candidate = Join-Path $backup ("profile-$i.candidate.ps1")
    [IO.File]::WriteAllText($candidate, $original.TrimEnd() + [Environment]::NewLine + [Environment]::NewLine + $block + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    $tokens = $null; $parseErrors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($candidate,[ref]$tokens,[ref]$parseErrors)
    if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
    $manifest += [pscustomobject]@{Path=$path;Backup=$save;Candidate=$candidate;BeforeSHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;AfterSHA256=(Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash}
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
try {
    foreach ($item in $manifest) {
        if ((Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.BeforeSHA256) { throw "Profile changed during preparation" }
        Copy-Item -LiteralPath $item.Candidate -Destination $item.Path
        if ((Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.AfterSHA256) { throw "Deployment verification failed" }
    }
} catch {
    foreach ($item in $manifest) { Copy-Item -LiteralPath $item.Backup -Destination $item.Path }
    throw
}
Write-Output ("Backup=" + $backup)
$manifest | Select-Object Path,BeforeSHA256,AfterSHA256 | ConvertTo-Json
