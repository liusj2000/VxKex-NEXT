param([switch]$Rollback)
$env:ELECTRON_RUN_AS_NODE = '1'
$arguments = 'D:\project\VxKex-NEXT\docx\repair-extension-startup-20261024.cjs'
if ($Rollback) { $arguments += ' --rollback' }
Start-Process -FilePath 'D:\VSCode-win32-x64-1.107.1\Code.exe' -ArgumentList $arguments -WindowStyle Hidden