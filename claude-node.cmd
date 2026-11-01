@echo off
setlocal
set "DISABLE_AUTOUPDATER=1"
set "PATH=D:\project\claude-code-engineering\node-v22.22.2-win-x64;%PATH%"
"D:\project\claude-code-engineering\node-v22.22.2-win-x64\node.exe" "%~dp0.tools\claude-code-node-2.1.112\node_modules\@anthropic-ai\claude-code\cli.js" %*
exit /b %errorlevel%
