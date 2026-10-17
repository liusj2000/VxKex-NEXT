@echo off
if exist "D:\project\VxKex-NEXT\bash.cmd" del /f /q "D:\project\VxKex-NEXT\bash.cmd"
setlocal
set "ROOT=D:\project\VxKex-NEXT"
set "LOG=%ROOT%\test\build-installer.log"

call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x64 > "%LOG%" 2>&1
set "VxKex_NEXT_Platform_Toolset=Windows7.1SDK"

echo [1/4] Building VxKex.sln (Release x64)... >> "%LOG%"
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe" "%ROOT%\VxKex.sln" /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m >> "%LOG%" 2>&1
if errorlevel 1 (
    echo [ERROR] Release x64 build failed. >> "%LOG%"
    exit /b 1
)

call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x86 >> "%LOG%" 2>&1
set "VxKex_NEXT_Platform_Toolset=Windows7.1SDK"

echo [2/4] Building VxKex.sln (Release Win32)... >> "%LOG%"
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe" "%ROOT%\VxKex.sln" /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /p:CustomBuildAfterTargets="" /m:1 /nologo /v:m >> "%LOG%" 2>&1
if errorlevel 1 (
    echo [ERROR] Release Win32 build failed. >> "%LOG%"
    exit /b 1
)

echo [3/4] Packaging KexSetup_Release.exe... >> "%LOG%"
call "%ROOT%\01-Development Utilities\7zSfx\makesfx.bat" /Release >> "%LOG%" 2>&1
if not exist "%ROOT%\KexSetup_Release.exe" (
    echo [ERROR] KexSetup_Release.exe was not generated. >> "%LOG%"
    exit /b 1
)

echo [4/4] Building and running ConPTY regression test... >> "%LOG%"
call "%ROOT%\test\conpty-review-build.cmd" >> "%LOG%" 2>&1
if errorlevel 1 (
    echo [ERROR] ConPTY regression test failed. >> "%LOG%"
    exit /b 1
)

dir "%ROOT%\KexSetup_Release.exe" "%ROOT%\x64\Release\KxBase.dll" "%ROOT%\Release\KxBase.dll" >> "%LOG%" 2>&1
certutil -hashfile "%ROOT%\KexSetup_Release.exe" SHA256 >> "%LOG%" 2>&1
certutil -hashfile "%ROOT%\x64\Release\KxBase.dll" SHA256 >> "%LOG%" 2>&1
echo [SUCCESS] Installer generated at: "%ROOT%\KexSetup_Release.exe" >> "%LOG%"
exit /b 0

