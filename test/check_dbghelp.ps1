$dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe"
$exe = "D:\VSCode-win32-x64-1.107.1\data\extensions\openai.chatgpt-26.1007.21434-win32-x64\bin\windows-x86_64\codex-code-mode-host.exe"
$out = & $dumpbin /IMPORTS $exe
for ($i = 0; $i -lt $out.Length; $i++) {
    if ($out[$i] -match "dbghelp\.dll") {
        for ($j = $i; $j -lt [Math]::Min($i + 15, $out.Length); $j++) {
            Write-Host $out[$j]
        }
        break
    }
}

