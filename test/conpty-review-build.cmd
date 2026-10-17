@echo off
if exist "D:\project\VxKex-NEXT\bash.cmd" del /f /q "D:\project\VxKex-NEXT\bash.cmd"
for /d %%D in ("D:\tools\Win7SDK\conpty*") do rmdir /s /q "%%~fD"
if exist "D:\tools\Win7SDK\conpty*" del /f /q "D:\tools\Win7SDK\conpty*"

call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x64 >nul 2>&1
set VxKex_NEXT_Platform_Toolset=Windows7.1SDK
MSBuild.exe "D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbase.vcxproj" /p:SolutionDir=D:\project\VxKex-NEXT\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m > "D:\project\VxKex-NEXT\docx\conpty-target-build-20261017.txt" 2>&1
set RESULT=%errorlevel%
echo BUILD_EXIT=%RESULT% >> "D:\project\VxKex-NEXT\docx\conpty-target-build-20261017.txt"
if not %RESULT%==0 exit /b %RESULT%

MSBuild.exe "D:\project\VxKex-NEXT\01-Extended DLLs\KxAdvapi\KxAdvapi.vcxproj" /p:SolutionDir=D:\project\VxKex-NEXT\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m >> "D:\project\VxKex-NEXT\docx\conpty-target-build-20261017.txt" 2>&1
echo ADVAPI_BUILD_EXIT=%errorlevel% >> "D:\project\VxKex-NEXT\docx\conpty-target-build-20261017.txt"

"C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\amd64\dumpbin.exe" /exports D:\project\VxKex-NEXT\x64\Release\KxBase.dll > D:\project\VxKex-NEXT\docx\conpty-exports-20261017.txt

MSBuild.exe "D:\project\VxKex-NEXT\test\conpty-review-test.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo > "D:\project\VxKex-NEXT\docx\conpty-test-build-20261017.txt" 2>&1
set TEST_BUILD=%errorlevel%
if not %TEST_BUILD%==0 exit /b %TEST_BUILD%

"D:\project\VxKex-NEXT\test\x64\Release\conpty-review-test.exe" > "D:\project\VxKex-NEXT\docx\conpty-test-run-20261017.txt" 2>&1
set TEST_EXIT=%errorlevel%
echo TEST_EXIT=%TEST_EXIT% >> "D:\project\VxKex-NEXT\docx\conpty-test-run-20261017.txt"
exit /b %TEST_EXIT%
