$ErrorActionPreference = 'Stop'
$report = 'D:\project\VxKex-NEXT\docx\opencode-codepage-probe-20261101.json'
$exe = 'D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@opencode\cli\bin\opencode.exe'
try {
    [Console]::InputEncoding = [Text.Encoding]::GetEncoding(936)
    [Console]::OutputEncoding = [Text.Encoding]::GetEncoding(936)
    $before = @{ Input = [Console]::InputEncoding.CodePage; Output = [Console]::OutputEncoding.CodePage }
    & $exe --version > 'D:\project\VxKex-NEXT\docx\opencode-codepage-version-20261101.txt' 2>&1
    $code = $LASTEXITCODE
    Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public static class KxCp { [DllImport("kernel32.dll")] public static extern uint GetConsoleCP(); [DllImport("kernel32.dll")] public static extern uint GetConsoleOutputCP(); }'
    @{ Before = $before; After = @{Input=[KxCp]::GetConsoleCP();Output=[KxCp]::GetConsoleOutputCP()}; ExitCode=$code } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding UTF8
} catch { $_.ToString() | Set-Content -LiteralPath $report -Encoding UTF8; exit 1 }
