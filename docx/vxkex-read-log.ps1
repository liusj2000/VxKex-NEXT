<#
.SYNOPSIS
    Summarise the failures recorded in a VxKex NEXT log file (.vxl).

.DESCRIPTION
    VxKex writes one binary .vxl log per process into
    %LOCALAPPDATA%\VxKex\Logs, named <exe>-<timestamp>-<pid>.vxl.
    This script extracts the UTF-16 text from the log and prints:

      1. Hard errors (loader failures). For STATUS_ENTRYPOINT_NOT_FOUND the
         two string parameters are the missing function and the DLL that was
         expected to export it. These are fatal for the module being loaded.
      2. DLLs that failed to load dynamically (LoadLibrary / dlopen).
      3. GetProcAddress-style lookups that failed. These are often optional
         probes with a fallback inside the application, so they are only
         suspects, not proof of a fault.
      4. The list of rewritten DLL imports (with -ShowRewrites).

    Read-only. Nothing is modified.

.PARAMETER Name
    Executable base name without extension, for example opencode, agy, codex.
    The newest matching log(s) are used.

.PARAMETER Path
    Explicit path of one .vxl file. Overrides -Name.

.PARAMETER Last
    How many of the newest matching logs to summarise. Default 1.

.PARAMETER LogDir
    Log directory. Default: %LOCALAPPDATA%\VxKex\Logs.

.PARAMETER ShowRewrites
    Also list "Rewrote DLL import: X -> Y" entries (unique).

.PARAMETER Dump
    Print every text entry of the log instead of the summary.

.EXAMPLE
    pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-read-log.ps1 -Name opencode

.EXAMPLE
    pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-read-log.ps1 -Name agy -Last 3 -ShowRewrites
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Name = '',
    [string]$Path = '',
    [int]$Last = 1,
    [string]$LogDir = '',
    [switch]$ShowRewrites,
    [switch]$Dump
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

if (-not $LogDir) { $LogDir = Join-Path $env:LOCALAPPDATA 'VxKex\Logs' }

$files = @()
if ($Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Write-Host "Log file not found: $Path"
        exit 2
    }
    $files = @(Get-Item -LiteralPath $Path)
} else {
    if (-not $Name) {
        Write-Host 'Specify -Name <exe base name> or -Path <file.vxl>.'
        exit 2
    }
    if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) {
        Write-Host "Log directory not found: $LogDir"
        exit 2
    }
    $files = @(Get-ChildItem -LiteralPath $LogDir -Filter ($Name + '-*.vxl') |
        Sort-Object LastWriteTime -Descending | Select-Object -First $Last)
    if ($files.Count -eq 0) {
        Write-Host "No log matching $Name-*.vxl in $LogDir"
        Write-Host 'Is VxKex enabled for this program, and is logging not disabled?'
        exit 2
    }
}

function Get-LogStrings([string]$File) {
    $fs = [System.IO.File]::Open($File, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $len = [int]$fs.Length
        $bytes = New-Object 'System.Byte[]' $len
        $read = 0
        while ($read -lt $len) {
            $n = $fs.Read($bytes, $read, $len - $read)
            if ($n -le 0) { break }
            $read += $n
        }
    } finally {
        $fs.Close()
    }
    $even = $read - ($read % 2)
    $text = [System.Text.Encoding]::Unicode.GetString($bytes, 0, $even)
    $out = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches($text, "[\x20-\x7E\r\n\t]{6,}")) {
        [void]$out.Add($m.Value)
    }
    return ,$out
}

function Write-Counted($Title, $Items) {
    Write-Host ''
    Write-Host $Title
    if ($Items.Count -eq 0) {
        Write-Host '    (none)'
        return
    }
    foreach ($g in ($Items | Group-Object | Sort-Object -Property @{ Expression = 'Count'; Descending = $true }, Name)) {
        Write-Host ("    {0,3} x  {1}" -f $g.Count, $g.Name)
    }
}

$fatalTotal = 0

foreach ($f in $files) {
    Write-Host ('=' * 78)
    Write-Host "Log : $($f.FullName)"
    Write-Host "Time: $($f.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))   Size: $($f.Length) bytes"

    $strings = Get-LogStrings $f.FullName

    if ($Dump) {
        foreach ($s in $strings) { Write-Host '---'; Write-Host $s.TrimEnd() }
        continue
    }

    $hard = New-Object System.Collections.ArrayList
    $loadFail = New-Object System.Collections.ArrayList
    $resolveFail = New-Object System.Collections.ArrayList
    $rewrites = New-Object System.Collections.ArrayList
    $other = New-Object System.Collections.ArrayList

    foreach ($s in $strings) {
        if ($s -match 'ErrorStatus:\s+(\S+ \(0x[0-9A-Fa-f]+\))') {
            $status = $Matches[1]
            $p1 = ''
            $p2 = ''
            if ($s -match 'StringParameter1:\s+"([^"]*)"') { $p1 = $Matches[1] }
            if ($s -match 'StringParameter2:\s+"([^"]*)"') { $p2 = $Matches[1] }
            if ($p1 -or $p2) {
                [void]$hard.Add("$status   parameter1=""$p1""   parameter2=""$p2""")
            } else {
                [void]$hard.Add($status)
            }
        }
        if ($s -match 'Failed to dynamically load (.+?)\.?\s*$') {
            [void]$loadFail.Add($Matches[1].Trim())
        } elseif ($s -match 'Failed to resolve (\S+) \(#(\d+)\) from (\S+)') {
            [void]$resolveFail.Add("$($Matches[1])   from $($Matches[3])")
        } elseif ($s -match 'Rewrote DLL import:\s+(\S+)\s+->\s+(\S+)') {
            [void]$rewrites.Add("$($Matches[1]) -> $($Matches[2])")
        } elseif ($s -match '(?i)could not be rewritten|could not start|access violation|unhandled exception|STATUS_DLL_NOT_FOUND|STATUS_ORDINAL_NOT_FOUND|STATUS_INVALID_IMAGE') {
            [void]$other.Add((($s -replace '\s+', ' ').Trim()))
        }
    }

    Write-Counted '[1] Hard errors (fatal for the module being loaded). For 0xc0000139: parameter1 = missing function, parameter2 = DLL expected to export it' $hard
    Write-Counted '[2] DLLs that failed to load dynamically' $loadFail
    Write-Counted '[3] Failed GetProcAddress lookups (often optional probes - suspects only)' $resolveFail
    if ($other.Count -gt 0) { Write-Counted '[4] Other notable entries' $other }
    if ($ShowRewrites) { Write-Counted '[5] Rewritten DLL imports' $rewrites }

    $fatalTotal += $hard.Count + $loadFail.Count
}

Write-Host ''
if ($Dump) { exit 0 }
if ($fatalTotal -gt 0) {
    Write-Host 'RESULT: loader failures found. Next: run vxkex-check-imports.ps1 on the DLL named in [2].'
    exit 1
}
Write-Host 'RESULT: no loader failure recorded in the selected log(s).'
exit 0
