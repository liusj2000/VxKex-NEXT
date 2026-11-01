$ErrorActionPreference='Stop'
$log='D:\project\VxKex-NEXT\docx\opencode-runtime-validation-20261101.json'
try {
    . 'D:\project\VxKex-NEXT\docx\opencode-win7-console.ps1'
    [Console]::InputEncoding=[Text.Encoding]::UTF8
    [Console]::OutputEncoding=[Text.Encoding]::UTF8
    $arguments=@('', 'space value', 'quote"value', 'C:\space path\', 'slash\"quote', ([string][char]0x7ee7 + [char]0x7eed))
    Invoke-KxConsole936 -ExecutablePath 'D:\project\claude-code-engineering\python-3.11.9-embed-amd64\python.exe' -ArgumentList (@('-S','D:\project\VxKex-NEXT\docx\probe-opencode-runtime-child-20261101.py') + $arguments)
    $code=$LASTEXITCODE
    $data=Get-Content -LiteralPath 'D:\project\VxKex-NEXT\docx\opencode-runtime-child-20261101.json' -Raw | ConvertFrom-Json
    $match=$data.argv.Count -eq $arguments.Count
    for ($i=0;$i -lt $arguments.Count;$i++) { if ($data.argv[$i] -cne $arguments[$i]) { $match=$false } }
    $inputCp=[VxKex.OpenCodeConsole]::GetConsoleCP()
    $outputCp=[VxKex.OpenCodeConsole]::GetConsoleOutputCP()
    $result=@{ExitCode=$code;ArgumentsMatch=$match;RestoredInput=$inputCp;RestoredOutput=$outputCp;Samples=$data.samples}
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $log -Encoding UTF8
    if ($code -ne 23 -or -not $match -or $inputCp -ne 65001 -or $outputCp -ne 65001) { exit 1 }
} catch { $_ | Out-String | Set-Content -LiteralPath $log -Encoding UTF8; exit 2 }
