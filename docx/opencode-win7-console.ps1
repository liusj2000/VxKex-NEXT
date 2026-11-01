# Win7 OpenCode console guard. ASCII-only; compatible with PowerShell 5.1.
function Invoke-KxConsole936 {
    param(
        [Parameter(Mandatory=$true)][string]$ExecutablePath,
        [string[]]$ArgumentList = @()
    )
    if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
        throw "Executable not found: $ExecutablePath"
    }
    if (-not ('VxKex.OpenCodeConsole' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
namespace VxKex {
    public static class OpenCodeConsole {
        [DllImport("kernel32.dll")] public static extern uint GetConsoleCP();
        [DllImport("kernel32.dll")] public static extern uint GetConsoleOutputCP();
        [DllImport("kernel32.dll")] public static extern bool SetConsoleCP(uint cp);
        [DllImport("kernel32.dll")] public static extern bool SetConsoleOutputCP(uint cp);
        public static IDisposable StartGuard() { return new Guard(); }
        private sealed class Guard : IDisposable {
            private readonly uint input = GetConsoleCP();
            private readonly uint output = GetConsoleOutputCP();
            private readonly ManualResetEvent stop = new ManualResetEvent(false);
            private readonly Thread thread;
            private bool disposed;
            private static void Enforce() {
                if (GetConsoleCP() != 936) SetConsoleCP(936);
                if (GetConsoleOutputCP() != 936) SetConsoleOutputCP(936);
            }
            public Guard() {
                Enforce();
                thread = new Thread(Run);
                thread.IsBackground = true;
                thread.Start();
            }
            private void Run() {
                while (!stop.WaitOne(100)) Enforce();
            }
            public void Dispose() {
                if (disposed) return;
                disposed = true;
                stop.Set();
                thread.Join();
                stop.Close();
                SetConsoleCP(input);
                SetConsoleOutputCP(output);
            }
        }
    }
}
'@
    }
    $guard = $null
    $exitCode = 1
    try {
        if ([VxKex.OpenCodeConsole]::GetConsoleCP() -ne 0 -and
            [VxKex.OpenCodeConsole]::GetConsoleOutputCP() -ne 0 -and
            -not [Console]::IsOutputRedirected -and -not [Console]::IsInputRedirected) {
            $guard = [VxKex.OpenCodeConsole]::StartGuard()
        }
        # Keep PowerShell's native invocation, argument/stdio and Ctrl+C handling.
        & $ExecutablePath @ArgumentList
        $exitCode = $LASTEXITCODE
    } finally {
        if ($null -ne $guard) { $guard.Dispose() }
    }
    $global:LASTEXITCODE = $exitCode
}

function global:opencode {
    $exe = 'D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@opencode\cli\bin\opencode.exe'
    if ($MyInvocation.ExpectingInput) {
        $input | & $exe @args
        $global:LASTEXITCODE = $LASTEXITCODE
        return
    }
    if ($MyInvocation.PipelineLength -gt 1) {
        & $exe @args
        $global:LASTEXITCODE = $LASTEXITCODE
        return
    }
    $env:OTUI_USE_ALTERNATE_SCREEN = 'true'
    Invoke-KxConsole936 -ExecutablePath $exe -ArgumentList $args
}
