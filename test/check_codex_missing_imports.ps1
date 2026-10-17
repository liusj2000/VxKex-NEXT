$lines = Get-Content "D:\project\VxKex-NEXT\test\codex_imports.txt"
$currentDll = ""
$imports = @{}

foreach ($line in $lines) {
    if ($line -match "^\s+([a-zA-Z0-9_\-\.]+)\.dll$") {
        $currentDll = $matches[1] + ".dll"
        $imports[$currentDll] = [System.Collections.Generic.List[string]]::new()
    } elseif ($line -match "^\s+[0-9a-fA-F]+\s+([a-zA-Z0-9_]+)$") {
        if ($currentDll) {
            $imports[$currentDll].Add($matches[1])
        }
    }
}

Write-Host "Total DLLs imported: $($imports.Keys.Count)"

# Check against installed VxKex Kex64 and Windows System32
$kex64 = "C:\Program Files\VxKex\Kex64"
$newKex64 = "D:\project\VxKex-NEXT\x64\Release"
$sys32 = "C:\Windows\System32"

# VxKex redirects map:
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

Write-Host "`n=== Checking with CURRENTLY INSTALLED VxKex (C:\Program Files\VxKex\Kex64) ==="

# Helper to get exports of a DLL using dumpbin
function Get-Exports($dllPath) {
    if (-not (Test-Path $dllPath)) { return @() }
    $out = & "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe" /EXPORTS $dllPath
    $exps = @{}
    foreach ($l in $out) {
        if ($l -match "^\s+\d+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([a-zA-Z0-9_]+)") {
            $exps[$matches[1]] = $true
        }
        if ($l -match "^\s+\d+\s+[0-9a-fA-F]+\s+([a-zA-Z0-9_]+)\s+\(forwarded") {
            $exps[$matches[1]] = $true
        }
    }
    return $exps
}

foreach ($dll in $imports.Keys) {
    $targetDllName = if ($redirects.ContainsKey($dll.ToLower())) { $redirects[$dll.ToLower()] } else { $dll }
    $installedPath = Join-Path $kex64 $targetDllName
    if (-not (Test-Path $installedPath)) {
        $installedPath = Join-Path $sys32 $targetDllName
    }
    
    $exports = Get-Exports $installedPath
    $missing = @()
    foreach ($fn in $imports[$dll]) {
        if (-not $exports.ContainsKey($fn)) {
            $missing += $fn
        }
    }
    if ($missing.Count -gt 0) {
        Write-Host "[-] $dll -> ${targetDllName} ($installedPath): MISSING $($missing.Count) exports:"
        foreach ($m in $missing) {
            Write-Host "    $m"
        }
    } else {
        Write-Host "[+] $dll -> ${targetDllName}: OK ($($imports[$dll].Count) functions)"
    }
}
