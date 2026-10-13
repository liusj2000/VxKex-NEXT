param([string]$Root = 'D:\tools\Win7SDK')
$ErrorActionPreference = 'Stop'
$log = Join-Path $Root 'prepare.log'
try {
    "Started: $(Get-Date -Format o)" | Out-File $log -Encoding utf8
    'WAITING_FOR_DOWNLOAD' | Set-Content (Join-Path $Root 'prepare.status')
    $deadline = (Get-Date).AddHours(4)
    while ($true) {
        $status = Get-Content (Join-Path $Root 'download.exitcode') -ErrorAction SilentlyContinue
        if ($status -eq '0') { break }
        if ($status -eq '1') { throw 'Download failed. See download.log.' }
        if ((Get-Date) -gt $deadline) { throw 'Download wait exceeded four hours.' }
        Start-Sleep -Seconds 10
    }
    $iso = Join-Path $Root 'GRMSDKX_EN_DVD.iso'
    if ((Get-FileHash $iso -Algorithm SHA1).Hash -ne '9203529F5F70D556A60C37F118A95214E6D10B5A') {
        throw 'ISO SHA1 mismatch.'
    }
    $media = Join-Path $Root 'media'
    New-Item -ItemType Directory -Path $media -Force | Out-Null
    'EXTRACTING' | Set-Content (Join-Path $Root 'prepare.status')
    $arguments = 'x -ibck -y "{0}" "{1}\"' -f $iso, $media
    $process = Start-Process 'C:\Program Files\WinRAR\WinRAR.exe' -ArgumentList $arguments -PassThru -Wait
    "extract_exit=$($process.ExitCode)" | Add-Content $log
    if ($process.ExitCode -ne 0) { throw 'ISO extraction failed.' }
    $setup = Join-Path $media 'setup.exe'
    if (!(Test-Path $setup)) { throw 'Root setup.exe not found in extracted media.' }
    Get-AuthenticodeSignature $setup | Format-List Status,StatusMessage,SignerCertificate | Out-String | Add-Content $log
    Get-FileHash $setup -Algorithm SHA256 | Format-List | Out-String | Add-Content $log
    'READY' | Set-Content (Join-Path $Root 'prepare.status')
    '0' | Set-Content (Join-Path $Root 'prepare.exitcode')
} catch {
    $_ | Out-String | Add-Content $log
    'FAILED' | Set-Content (Join-Path $Root 'prepare.status')
    '1' | Set-Content (Join-Path $Root 'prepare.exitcode')
    exit 1
}