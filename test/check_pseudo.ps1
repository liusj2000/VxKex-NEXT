$e = & "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe" /EXPORTS "C:\Program Files\VxKex\Kex64\kxbase.dll"
$matches = $e | Where-Object { $_ -match "PseudoConsole" }
Write-Host "Matches in installed kxbase.dll:"
$matches
