param([string]$Root = 'D:\tools\Win7SDK')
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an Administrator PowerShell window. No installation performed.'
}
New-Item -ItemType Directory -Path $Root -Force | Out-Null
$log = Join-Path $Root 'install.log'
try {
    "Started: $(Get-Date -Format o)" | Out-File $log -Encoding utf8
    'WAITING_FOR_MEDIA' | Set-Content (Join-Path $Root 'install.status')
    $deadline = (Get-Date).AddHours(4)
    while ($true) {
        $status = Get-Content (Join-Path $Root 'prepare.status') -ErrorAction SilentlyContinue
        if ($status -eq 'READY') { break }
        if ($status -eq 'FAILED') { throw 'Media preparation failed. See prepare.log.' }
        if ((Get-Date) -gt $deadline) { throw 'Media preparation wait exceeded four hours.' }
        Start-Sleep -Seconds 10
    }
    $iso = Join-Path $Root 'GRMSDKX_EN_DVD.iso'
    if ((Get-FileHash $iso -Algorithm SHA1).Hash -ne '9203529F5F70D556A60C37F118A95214E6D10B5A') {
        throw 'ISO SHA1 mismatch; installation blocked.'
    }
    $setup = Join-Path $Root 'media\setup.exe'
    $signature = Get-AuthenticodeSignature $setup
    $signature | Format-List Status,StatusMessage,SignerCertificate | Out-String | Add-Content $log
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
        throw 'A valid Microsoft setup signature is required. Do not disable certificate checks.'
    }
    'INSTALLING' | Set-Content (Join-Path $Root 'install.status')
    $process = Start-Process $setup -ArgumentList '-q -params:ADDLOCAL=ALL' -WorkingDirectory (Split-Path $setup) -PassThru -Wait
    "setup_exit=$($process.ExitCode)" | Add-Content $log
    foreach ($folder in @($env:TEMP, "$env:WINDIR\Temp")) {
        Get-ChildItem $folder -File -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -match 'SDK|dd_' -and $_.LastWriteTime -gt (Get-Date).AddHours(-4)
        } | ForEach-Object {
            Copy-Item $_.FullName (Join-Path $Root ($_.Name + '.setup-log')) -Force -ErrorAction SilentlyContinue
        }
    }
    if ($process.ExitCode -notin @(0,3010)) { throw 'SDK setup reported failure.' }
    $envScript = 'C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd'
    $props = 'C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\Microsoft.Cpp.Default.props'
    "SetEnv exists: $(Test-Path $envScript)" | Add-Content $log
    "CPP props exists: $(Test-Path $props)" | Add-Content $log
    if (!(Test-Path $envScript) -or !(Test-Path $props)) {
        throw 'Setup ended but expected SDK/C++ build prerequisites are absent. Review logs.'
    }
    'INSTALLED_REQUIRES_BUILD_VERIFICATION' | Set-Content (Join-Path $Root 'install.status')
    $process.ExitCode | Set-Content (Join-Path $Root 'install.exitcode')
} catch {
    $_ | Out-String | Add-Content $log
    'FAILED' | Set-Content (Join-Path $Root 'install.status')
    '1' | Set-Content (Join-Path $Root 'install.exitcode')
    exit 1
}