param([string]$Root = 'D:\tools\Win7SDK')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $Root -Force | Out-Null
$log = Join-Path $Root 'download.log'
try {
    "Started: $(Get-Date -Format o)" | Out-File $log -Encoding utf8
    'RUNNING' | Set-Content (Join-Path $Root 'download.exitcode')
    $url = 'https://download.microsoft.com/download/F/1/0/F10113F5-B750-4969-A255-274341AC6BCE/GRMSDKX_EN_DVD.iso'
    $file = Join-Path $Root 'GRMSDKX_EN_DVD.iso'
    $expected = '9203529F5F70D556A60C37F118A95214E6D10B5A'
    if (!(Test-Path $file) -or (Get-FileHash $file -Algorithm SHA1).Hash -ne $expected) {
        $partial = "$file.partial"
        & 'C:\Program Files\Git\mingw64\bin\curl.exe' -4 --http1.1 --fail --location --retry 3 --retry-all-errors --connect-timeout 30 --output $partial $url 2>> $log
        if ($LASTEXITCODE -ne 0) { throw "curl download failed: $LASTEXITCODE" }
        if ((Get-FileHash $partial -Algorithm SHA1).Hash -ne $expected) { throw 'Official ISO SHA1 mismatch; do not install.' }
        Move-Item -LiteralPath $partial -Destination $file -Force
    }
    "Source: $url" | Add-Content $log
    Get-FileHash $file -Algorithm SHA1 | Format-List | Out-String | Add-Content $log
    Get-FileHash $file -Algorithm SHA256 | Format-List | Out-String | Add-Content $log
    'DOWNLOAD_VERIFIED' | Add-Content $log
    '0' | Set-Content (Join-Path $Root 'download.exitcode')
} catch {
    $_ | Out-String | Add-Content $log
    '1' | Set-Content (Join-Path $Root 'download.exitcode')
    exit 1
}