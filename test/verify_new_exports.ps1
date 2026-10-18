$dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe"
$x64 = "D:\project\VxKex-NEXT\x64\Release"

function Check-Export($dll, $sym) {
    $path = Join-Path $x64 $dll
    $out = & $dumpbin /EXPORTS $path
    $m = $out | Where-Object { $_ -match "\b$sym\b" }
    if ($m) {
        Write-Host "[PASS] $dll -> $sym ($($m.Trim()))"
    } else {
        Write-Host "[FAIL] $dll DOES NOT EXPORT $sym"
    }
}

Check-Export "KexDll.dll" "IsApiSetImplemented"
Check-Export "KxNt.dll" "IsApiSetImplemented"
Check-Export "KxBase.dll" "DeriveCapabilitySidsFromName"
Check-Export "KxAdvapi.dll" "DeriveCapabilitySidsFromName"

