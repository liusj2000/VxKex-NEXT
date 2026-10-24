$env:ELECTRON_RUN_AS_NODE = '1'
$p = Start-Process -FilePath 'D:\VSCode-win32-x64-1.107.1\Code.exe' -ArgumentList 'D:\project\VxKex-NEXT\docx\extension-startup-probe-20261024.cjs' -PassThru -WindowStyle Hidden
$p.Id | Out-File 'D:\project\VxKex-NEXT\docx\extension-probe-pid-20261024.txt'