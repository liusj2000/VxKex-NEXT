$dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe"
$exe = "D:\VSCode-win32-x64-1.107.1\data\extensions\openai.chatgpt-26.1007.21434-win32-x64\bin\windows-x86_64\codex-code-mode-host.exe"
$sys32 = "C:\Windows\System32"
$kex64 = "C:\Program Files\VxKex\Kex64"

$out = & $dumpbin /IMPORTS $exe
$currentDll = ""
$missing = @()

foreach ($line in $out) {
    if ($line -match "^\s+([a-zA-Z0-9_\-\.]+)\.dll$") {
        $currentDll = $matches[1] + ".dll"
    } elseif ($line -match "^\s+[0-9a-fA-F]+\s+([a-zA-Z0-9_]+)$") {
        $fn = $matches[1]
        if ($currentDll) {
            $targetPath = Join-Path $kex64 $currentDll
            if (-not (Test-Path $targetPath)) {
                $targetPath = Join-Path $sys32 $currentDll
            }
            if (Test-Path $targetPath) {
                $exp = & $dumpbin /EXPORTS $targetPath
                $found = $exp | Where-Object { $_ -match "\b$fn\b" }
                if (-not $found) {
                    $missing += "$currentDll : $fn"
                }
            }
        }
    }
}

Write-Host "Missing in codex-code-mode-host.exe: $($missing.Count)"
$missing

