$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public static class KxStartupCp { [DllImport("kernel32.dll")] public static extern uint GetConsoleCP(); [DllImport("kernel32.dll")] public static extern uint GetConsoleOutputCP(); }'
$initial = @{Input=[KxStartupCp]::GetConsoleCP();Output=[KxStartupCp]::GetConsoleOutputCP();Alternate=$env:OTUI_USE_ALTERNATE_SCREEN}
$results = @()
foreach ($p in @('C:\Users\liusj\Documents\PowerShell\profile.ps1','C:\Users\liusj\Documents\PowerShell\Microsoft.PowerShell_profile.ps1','C:\Users\liusj\Documents\WindowsPowerShell\profile.ps1','C:\Users\liusj\Documents\WindowsPowerShell\Microsoft.PowerShell_profile.ps1')) {
    [Console]::InputEncoding = [Text.Encoding]::UTF8
    [Console]::OutputEncoding = [Text.Encoding]::UTF8
    $env:OTUI_USE_ALTERNATE_SCREEN = 'false'
    . $p
    $result = @{Profile=$p; Input=[KxStartupCp]::GetConsoleCP(); Output=[KxStartupCp]::GetConsoleOutputCP();Alternate=$env:OTUI_USE_ALTERNATE_SCREEN;PSReadLineLoaded=[bool](Get-Module PSReadLine)}
    $results += $result
    if ($result.Input -ne 936 -or $result.Output -ne 936 -or $result.Alternate -ne 'true' -or $result.PSReadLineLoaded) { throw "Profile verification failed: $p" }
}
& 'D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@opencode\cli\bin\opencode.exe' --version > 'D:\project\VxKex-NEXT\docx\opencode-codepage-version-20261101.txt' 2>&1
$versionExit=$LASTEXITCODE
@{Initial=$initial;Profiles=$results;VersionExit=$versionExit;FinalInput=[KxStartupCp]::GetConsoleCP();FinalOutput=[KxStartupCp]::GetConsoleOutputCP()} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath 'D:\project\VxKex-NEXT\docx\opencode-startup-validation-20261101.json' -Encoding UTF8
if ($initial.Input -ne 936 -or $initial.Output -ne 936 -or $versionExit -ne 0) { exit 1 }
