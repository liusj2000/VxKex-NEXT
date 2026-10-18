$binDir = "D:\VSCode-win32-x64-1.107.1\data\extensions\openai.chatgpt-26.1007.21434-win32-x64\bin\windows-x86_64"
$dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe"
$kex64 = "D:\project\VxKex-NEXT\x64\Release"
$sys32 = "C:\Windows\System32"

$redirects = @{
    "kernel32.dll" = "kxbase.dll";
    "kernelbase.dll" = "kxbase.dll";
    "advapi32.dll" = "kxadvapi.dll";
    "combase.dll" = "kxcom.dll";
    "ole32.dll" = "kxcom.dll";
    "user32.dll" = "kxuser.dll";
    "ws2_32.dll" = "kxnet.dll";
    "winhttp.dll" = "kxnet.dll";
    "secur32.dll" = "kxcryp.dll";
    "bcrypt.dll" = "kxcryp.dll";
    "bcryptprimitives.dll" = "kxcryp.dll";
    "userenv.dll" = "kxmi.dll";
    "wtsapi32.dll" = "kxmi.dll";
    "api-ms-win-core-synch-l1-2-0.dll" = "kxbase.dll";
    "api-ms-win-security-base-l1-2-2.dll" = "kxbase.dll";
    "api-ms-win-core-winrt-l1-1-0.dll" = "kxcom.dll";
    "api-ms-win-core-apiquery-l2-1-0.dll" = "kxnt.dll";
    "ntdll.dll" = "kxnt.dll"
}

$exportCache = @{}
function Get-ExportsCached($dllPath) {
    if ($exportCache.ContainsKey($dllPath)) { return $exportCache[$dllPath] }
    if (-not (Test-Path $dllPath)) { return @{} }
    $out = & $dumpbin /EXPORTS $dllPath
    $exps = @{}
    foreach ($l in $out) {
        if ($l -match "^\s+\d+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([a-zA-Z0-9_]+)") {
            $exps[$matches[1]] = $true
        }
        if ($l -match "^\s+\d+\s+[0-9a-fA-F]+\s+([a-zA-Z0-9_]+)\s+\(forwarded") {
            $exps[$matches[1]] = $true
        }
    }
    $exportCache[$dllPath] = $exps
    return $exps
}

$codex = Join-Path $binDir "codex.exe"
Write-Host "`nChecking codex.exe against NEW x64\Release DLLs..."
$out = & $dumpbin /IMPORTS $codex
$currentDll = ""
$missingTotal = 0

foreach ($line in $out) {
    if ($line -match "^\s+([a-zA-Z0-9_\-\.]+)\.dll$") {
        $currentDll = $matches[1] + ".dll"
    } elseif ($line -match "^\s+[0-9a-fA-F]+\s+([a-zA-Z0-9_]+)$") {
        $fn = $matches[1]
        if ($currentDll -and $fn -ne "_RDATA") {
            $targetDll = if ($redirects.ContainsKey($currentDll.ToLower())) { $redirects[$currentDll.ToLower()] } else { $currentDll }
            $targetPath = Join-Path $kex64 $targetDll
            if (-not (Test-Path $targetPath)) {
                $targetPath = Join-Path $sys32 $targetDll
            }
            $exports = Get-ExportsCached $targetPath
            if (-not $exports.ContainsKey($fn)) {
                Write-Host "[-] MISSING: $currentDll -> $targetDll ($targetPath) : $fn"
                $missingTotal++
            }
        }
    }
}

if ($missingTotal -eq 0) {
    Write-Host "[SUCCESS] All $(($out | Where-Object { $_ -match '^\s+[0-9a-fA-F]+\s+[a-zA-Z0-9_]+$' }).Count) imports satisfied for codex.exe!"
} else {
    Write-Host "[FAIL] Still missing $missingTotal imports in codex.exe"
}

