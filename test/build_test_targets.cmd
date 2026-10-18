@echo off
setlocal
call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x64 >nul 2>&1
set VxKex_NEXT_Platform_Toolset=Windows7.1SDK
set "MSB64=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe"

echo --- Building KexDll x64 ---
"%MSB64%" "D:\project\VxKex-NEXT\KexDll\KexDll.vcxproj" /p:SolutionDir=D:\project\VxKex-NEXT\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] KexDll build failed.
    exit /b 1
)

echo --- Building KxNt x64 ---
"%MSB64%" "D:\project\VxKex-NEXT\01-Extended DLLs\KxNt\KxNt.vcxproj" /p:SolutionDir=D:\project\VxKex-NEXT\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] KxNt build failed.
    exit /b 1
)

echo --- Building KxBase x64 ---
"%MSB64%" "D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbase.vcxproj" /p:SolutionDir=D:\project\VxKex-NEXT\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] KxBase build failed.
    exit /b 1
)

echo --- Building KxAdvapi x64 ---
"%MSB64%" "D:\project\VxKex-NEXT\01-Extended DLLs\KxAdvapi\KxAdvapi.vcxproj" /p:SolutionDir=D:\project\VxKex-NEXT\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m
if errorlevel 1 (
    echo [ERROR] KxAdvapi build failed.
    exit /b 1
)

echo [ALL TARGETS COMPILED SUCCESSFULLY]
exit /b 0

