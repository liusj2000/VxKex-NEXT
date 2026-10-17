$dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe"
$codex = "D:\VSCode-win32-x64-1.107.1\data\extensions\openai.chatgpt-26.1007.21434-win32-x64\bin\windows-x86_64\codex.exe"
$outFile = "D:\project\VxKex-NEXT\test\codex_imports.txt"

& $dumpbin /IMPORTS $codex | Out-File -FilePath $outFile -Encoding utf8
Write-Host "Dump complete, lines: " (Get-Content $outFile).Length
