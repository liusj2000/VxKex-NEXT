<#
.SYNOPSIS
    Offline check: can every import of a PE file be resolved under VxKex NEXT?

.DESCRIPTION
    Parses the import table (and delay-load import table) of an EXE/DLL/.node
    file, applies the VxKex DLL redirect table (KexDll\redirects.h), and checks
    that every imported function is exported by the DLL the loader would use.
    Forwarded exports are followed (for example kxnt -> KexDll -> ...).

    Nothing is executed and nothing is modified. Pure PE parsing, no dumpbin.

    Exit code: 0 = all static imports resolvable, 1 = something is missing,
               2 = usage error.

.PARAMETER Target
    Full path of the EXE / DLL / .node file to check.

.PARAMETER KexDir
    Directory holding the Kx*.dll set to check against.
    Default: the installed set, C:\Program Files\VxKex\Kex64 (or Kex32).
    Use D:\project\VxKex-NEXT\x64\Release to check against a new build.

    When KexDir is not the installed directory, the installed directory is
    searched right after it, because a new build only replaces the DLLs that
    this repository builds (ucrtbase.dll and others come from the installer).

.PARAMETER NoInstalledFallback
    Do not search the installed VxKex directory after -KexDir.

.PARAMETER RedirectsHeader
    Path of redirects.h. Default: ..\KexDll\redirects.h next to this script.

.PARAMETER ExtraSearchDir
    Additional directories searched last, after KexDir, the installed VxKex
    directory, the target directory and the system directory.

.PARAMETER ShowOk
    Also print one line for every import DLL that is fully satisfied.

.EXAMPLE
    pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-check-imports.ps1 -Target C:\path\app.dll -ShowOk

.EXAMPLE
    pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-check-imports.ps1 -Target C:\path\app.dll -KexDir D:\project\VxKex-NEXT\x64\Release
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Target,
    [string]$KexDir = '',
    [switch]$NoInstalledFallback,
    [string]$RedirectsHeader = '',
    [string[]]$ExtraSearchDir = @(),
    [switch]$ShowOk
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------- PE parsing

function Open-PeFile([string]$Path) {
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $br = New-Object System.IO.BinaryReader($fs)
    $pe = @{ Path = $Path; Stream = $fs; Reader = $br }

    $fs.Position = 0
    if ($br.ReadUInt16() -ne 0x5A4D) { $fs.Close(); throw "Not an MZ file: $Path" }
    $fs.Position = 0x3C
    $peOff = $br.ReadInt32()
    $fs.Position = $peOff
    if ($br.ReadUInt32() -ne 0x4550) { $fs.Close(); throw "Not a PE file: $Path" }
    $pe.Machine = $br.ReadUInt16()
    $sectionCount = $br.ReadUInt16()
    $fs.Position = $peOff + 20
    $optSize = $br.ReadUInt16()
    $optOff = $peOff + 24
    $fs.Position = $optOff
    $magic = $br.ReadUInt16()
    $pe.Is64 = ($magic -eq 0x20B)

    if ($pe.Is64) {
        $fs.Position = $optOff + 24
        $pe.ImageBase = $br.ReadUInt64()
        $dirCountOff = $optOff + 108
    } else {
        $fs.Position = $optOff + 28
        $pe.ImageBase = [uint64]$br.ReadUInt32()
        $dirCountOff = $optOff + 92
    }

    $fs.Position = $dirCountOff
    $dirCount = $br.ReadUInt32()
    if ($dirCount -gt 16) { $dirCount = 16 }
    $dirs = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $dirCount; $i++) {
        $rva = $br.ReadUInt32()
        $size = $br.ReadUInt32()
        [void]$dirs.Add(@{ Rva = $rva; Size = $size })
    }
    $pe.Dirs = $dirs

    $sections = New-Object System.Collections.ArrayList
    $secOff = $optOff + $optSize
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $fs.Position = $secOff + 40 * $i + 8
        $vsize = $br.ReadUInt32()
        $va = $br.ReadUInt32()
        $rawSize = $br.ReadUInt32()
        $rawPtr = $br.ReadUInt32()
        [void]$sections.Add(@{ VSize = $vsize; VA = $va; RawSize = $rawSize; RawPtr = $rawPtr })
    }
    $pe.Sections = $sections
    return $pe
}

function Close-PeFile($Pe) {
    $Pe.Reader.Close()
    $Pe.Stream.Close()
}

function Convert-RvaToOffset($Pe, [uint32]$Rva) {
    foreach ($s in $Pe.Sections) {
        $span = $s.VSize
        if ($s.RawSize -gt $span) { $span = $s.RawSize }
        if ($Rva -ge $s.VA -and $Rva -lt ($s.VA + $span)) {
            return [int64]($Rva - $s.VA + $s.RawPtr)
        }
    }
    # Inside the headers: RVA equals file offset.
    return [int64]$Rva
}

function Read-AsciiZ($Pe, [int64]$Offset) {
    $Pe.Stream.Position = $Offset
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt 1024; $i++) {
        $b = $Pe.Stream.ReadByte()
        if ($b -le 0) { break }
        [void]$sb.Append([char]$b)
    }
    return $sb.ToString()
}

# Returns a list of @{ Dll; Name; Ordinal; Delay }.
function Read-ThunkList($Pe, [uint32]$ThunkRva, [string]$Dll, [bool]$Delay, $Out) {
    $fs = $Pe.Stream
    $br = $Pe.Reader
    $raw = New-Object System.Collections.ArrayList
    $fs.Position = Convert-RvaToOffset $Pe $ThunkRva

    while ($true) {
        if ($Pe.Is64) {
            $lo = $br.ReadUInt32()
            $hi = $br.ReadUInt32()
            if ($lo -eq 0 -and $hi -eq 0) { break }
            [void]$raw.Add(@{ Lo = $lo; IsOrdinal = ($hi -ge 2147483648) })
        } else {
            $lo = $br.ReadUInt32()
            if ($lo -eq 0) { break }
            [void]$raw.Add(@{ Lo = $lo; IsOrdinal = ($lo -ge 2147483648) })
        }
        if ($raw.Count -gt 100000) { throw "Import thunk list too long in $($Pe.Path)" }
    }

    foreach ($t in $raw) {
        if ($t.IsOrdinal) {
            [void]$Out.Add(@{ Dll = $Dll; Name = $null; Ordinal = [int]($t.Lo -band 0xFFFF); Delay = $Delay })
        } else {
            $nameRva = $t.Lo
            if ($Delay -and $nameRva -ge $Pe.ImageBase -and $Pe.ImageBase -gt 0 -and -not $Pe.Is64 -and $Pe.DelayUsesVa) {
                $nameRva = [uint32]($nameRva - $Pe.ImageBase)
            }
            $name = Read-AsciiZ $Pe ((Convert-RvaToOffset $Pe $nameRva) + 2)
            [void]$Out.Add(@{ Dll = $Dll; Name = $name; Ordinal = -1; Delay = $Delay })
        }
    }
}

function Get-PeImports($Pe) {
    $out = New-Object System.Collections.ArrayList
    $fs = $Pe.Stream
    $br = $Pe.Reader
    $Pe.DelayUsesVa = $false

    # Normal import directory (index 1).
    if ($Pe.Dirs.Count -gt 1 -and $Pe.Dirs[1].Rva -ne 0) {
        $base = Convert-RvaToOffset $Pe $Pe.Dirs[1].Rva
        $descs = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt 4096; $i++) {
            $fs.Position = $base + 20 * $i
            $oft = $br.ReadUInt32()
            [void]$br.ReadUInt32()
            [void]$br.ReadUInt32()
            $nameRva = $br.ReadUInt32()
            $ft = $br.ReadUInt32()
            if ($nameRva -eq 0 -and $ft -eq 0) { break }
            [void]$descs.Add(@{ Oft = $oft; NameRva = $nameRva; Ft = $ft })
        }
        foreach ($d in $descs) {
            $dll = Read-AsciiZ $Pe (Convert-RvaToOffset $Pe $d.NameRva)
            $thunk = $d.Oft
            if ($thunk -eq 0) { $thunk = $d.Ft }
            Read-ThunkList $Pe $thunk $dll $false $out
        }
    }

    # Delay-load import directory (index 13).
    if ($Pe.Dirs.Count -gt 13 -and $Pe.Dirs[13].Rva -ne 0) {
        $base = Convert-RvaToOffset $Pe $Pe.Dirs[13].Rva
        $descs = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt 4096; $i++) {
            $fs.Position = $base + 32 * $i
            $attrs = $br.ReadUInt32()
            $nameRva = $br.ReadUInt32()
            [void]$br.ReadUInt32()
            [void]$br.ReadUInt32()
            $intRva = $br.ReadUInt32()
            if ($nameRva -eq 0) { break }
            [void]$descs.Add(@{ Attrs = $attrs; NameRva = $nameRva; IntRva = $intRva })
        }
        foreach ($d in $descs) {
            $nameRva = $d.NameRva
            $intRva = $d.IntRva
            if (($d.Attrs -band 1) -eq 0) {
                # Old-style descriptor: fields are VAs, not RVAs (32-bit only).
                $Pe.DelayUsesVa = $true
                $nameRva = [uint32]($nameRva - $Pe.ImageBase)
                $intRva = [uint32]($intRva - $Pe.ImageBase)
            }
            if ($intRva -eq 0) { continue }
            $dll = Read-AsciiZ $Pe (Convert-RvaToOffset $Pe $nameRva)
            Read-ThunkList $Pe $intRva $dll $true $out
        }
    }

    return ,$out
}

# Returns @{ Names = name -> forwarder or ''; Ordinals = ordinal -> forwarder or '' }.
function Get-PeExports([string]$Path) {
    $names = New-Object System.Collections.Hashtable ([System.StringComparer]::Ordinal)
    $ordinals = @{}
    $pe = Open-PeFile $Path
    try {
        if ($pe.Dirs.Count -lt 1 -or $pe.Dirs[0].Rva -eq 0) {
            return @{ Names = $names; Ordinals = $ordinals }
        }
        $dirRva = $pe.Dirs[0].Rva
        $dirSize = $pe.Dirs[0].Size
        $fs = $pe.Stream
        $br = $pe.Reader
        $fs.Position = (Convert-RvaToOffset $pe $dirRva) + 16
        $ordBase = $br.ReadUInt32()
        $funcCount = $br.ReadUInt32()
        $nameCount = $br.ReadUInt32()
        $funcsRva = $br.ReadUInt32()
        $namesRva = $br.ReadUInt32()
        $nameOrdsRva = $br.ReadUInt32()

        $funcs = New-Object 'System.UInt32[]' $funcCount
        if ($funcCount -gt 0) {
            $fs.Position = Convert-RvaToOffset $pe $funcsRva
            for ($i = 0; $i -lt $funcCount; $i++) { $funcs[$i] = $br.ReadUInt32() }
        }

        $fwd = New-Object 'System.String[]' $funcCount
        for ($i = 0; $i -lt $funcCount; $i++) {
            $f = $funcs[$i]
            if ($f -eq 0) { continue }
            if ($f -ge $dirRva -and $f -lt ($dirRva + $dirSize)) {
                $fwd[$i] = Read-AsciiZ $pe (Convert-RvaToOffset $pe $f)
            } else {
                $fwd[$i] = ''
            }
            $ordinals[[int]($ordBase + $i)] = $fwd[$i]
        }

        if ($nameCount -gt 0) {
            $nameRvas = New-Object 'System.UInt32[]' $nameCount
            $fs.Position = Convert-RvaToOffset $pe $namesRva
            for ($i = 0; $i -lt $nameCount; $i++) { $nameRvas[$i] = $br.ReadUInt32() }
            $nameOrds = New-Object 'System.UInt16[]' $nameCount
            $fs.Position = Convert-RvaToOffset $pe $nameOrdsRva
            for ($i = 0; $i -lt $nameCount; $i++) { $nameOrds[$i] = $br.ReadUInt16() }
            for ($i = 0; $i -lt $nameCount; $i++) {
                $n = Read-AsciiZ $pe (Convert-RvaToOffset $pe $nameRvas[$i])
                $idx = $nameOrds[$i]
                if ($idx -lt $funcCount -and $funcs[$idx] -ne 0) {
                    $names[$n] = $fwd[$idx]
                }
            }
        }
    } finally {
        Close-PeFile $pe
    }
    return @{ Names = $names; Ordinals = $ordinals }
}

# ------------------------------------------------------------ VxKex redirect

function Read-RedirectTable([string]$HeaderPath) {
    $map = @{}   # case-insensitive keys, same as the string mapper in KexDll
    foreach ($line in [System.IO.File]::ReadAllLines($HeaderPath)) {
        $t = $line.Trim()
        if ($t.StartsWith('//') -or $t.StartsWith('#')) { continue }
        $m = [regex]::Match($t, 'DLL_REDIRECT\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)')
        if ($m.Success) { $map[$m.Groups[1].Value] = $m.Groups[2].Value }
    }
    return $map
}

# Mirrors KexpLookupDllRewriteEntry in KexDll\dllrewrt.c.
function Get-RedirectedDll($Map, [string]$DllName) {
    $n = $DllName
    if ($n.Length -gt 4 -and $n.Substring($n.Length - 4) -ieq '.dll') {
        $n = $n.Substring(0, $n.Length - 4)
    }
    if ($n -match '^(api|ext)-' -and $n.Length -gt 7) {
        $n = $n.Substring(0, $n.Length - 7)   # strip "-lX-Y-Z"
    }
    if ($Map.ContainsKey($n)) { return $Map[$n] }
    return $null
}

# ------------------------------------------------------------------- lookup

$script:SearchDirs = @()
$script:ExportCache = @{}
$script:FileCache = @{}

function Find-DllFile([string]$BaseName) {
    $n = $BaseName
    if ($n -notmatch '\.[A-Za-z0-9]{1,4}$') { $n = $n + '.dll' }
    $key = $n.ToLowerInvariant()
    if ($script:FileCache.ContainsKey($key)) { return $script:FileCache[$key] }
    $found = $null
    foreach ($d in $script:SearchDirs) {
        $p = Join-Path $d $n
        if (Test-Path -LiteralPath $p -PathType Leaf) { $found = $p; break }
    }
    $script:FileCache[$key] = $found
    return $found
}

function Get-ExportsCached([string]$Path) {
    $key = $Path.ToLowerInvariant()
    if (-not $script:ExportCache.ContainsKey($key)) {
        $script:ExportCache[$key] = Get-PeExports $Path
    }
    return $script:ExportCache[$key]
}

# Returns @{ Status = OK | NODLL | MISSING | FWD-BROKEN | FWD-UNVERIFIED; Detail }.
function Test-Symbol([string]$DllBase, [string]$Name, [int]$Ordinal, [int]$Depth) {
    $file = Find-DllFile $DllBase
    if (-not $file) { return @{ Status = 'NODLL'; Detail = $DllBase } }
    $exp = Get-ExportsCached $file

    if ($Name) {
        if (-not $exp.Names.ContainsKey($Name)) { return @{ Status = 'MISSING'; Detail = $file } }
        $fwd = $exp.Names[$Name]
    } else {
        if (-not $exp.Ordinals.ContainsKey($Ordinal)) { return @{ Status = 'MISSING'; Detail = $file } }
        $fwd = $exp.Ordinals[$Ordinal]
    }

    if ($fwd -and $Depth -lt 6) {
        $dot = $fwd.LastIndexOf('.')
        if ($dot -gt 0) {
            $mod = $fwd.Substring(0, $dot)
            $func = $fwd.Substring($dot + 1)
            if ($func.StartsWith('#')) {
                $r = Test-Symbol $mod $null ([int]$func.Substring(1)) ($Depth + 1)
            } else {
                $r = Test-Symbol $mod $func -1 ($Depth + 1)
            }
            if ($r.Status -eq 'NODLL') { return @{ Status = 'FWD-UNVERIFIED'; Detail = "$file -> $fwd" } }
            if ($r.Status -eq 'MISSING' -or $r.Status -eq 'FWD-BROKEN') { return @{ Status = 'FWD-BROKEN'; Detail = "$file -> $fwd" } }
            if ($r.Status -eq 'FWD-UNVERIFIED') { return $r }
        }
    }
    return @{ Status = 'OK'; Detail = $file }
}

# --------------------------------------------------------------------- main

if (-not (Test-Path -LiteralPath $Target -PathType Leaf)) {
    Write-Host "Target not found: $Target"
    exit 2
}
$Target = (Resolve-Path -LiteralPath $Target).ProviderPath

if (-not $RedirectsHeader) {
    $RedirectsHeader = Join-Path (Split-Path -Parent $PSScriptRoot) 'KexDll\redirects.h'
}
if (-not (Test-Path -LiteralPath $RedirectsHeader -PathType Leaf)) {
    Write-Host "redirects.h not found: $RedirectsHeader"
    exit 2
}

$pe = Open-PeFile $Target
try {
    $is64 = $pe.Is64
    $imports = Get-PeImports $pe
} finally {
    Close-PeFile $pe
}

if ($is64) { $installedKexDir = Join-Path $env:ProgramFiles 'VxKex\Kex64' }
else { $installedKexDir = Join-Path $env:ProgramFiles 'VxKex\Kex32' }

if (-not $KexDir) { $KexDir = $installedKexDir }
if (-not (Test-Path -LiteralPath $KexDir -PathType Container)) {
    Write-Host "KexDir not found: $KexDir"
    exit 2
}
$KexDir = (Resolve-Path -LiteralPath $KexDir).ProviderPath

$fallbackKexDir = $null
if (-not $NoInstalledFallback -and
    ($KexDir.TrimEnd('\') -ine $installedKexDir.TrimEnd('\')) -and
    (Test-Path -LiteralPath $installedKexDir -PathType Container)) {
    $fallbackKexDir = $installedKexDir
}

if ($is64) {
    if ([System.Environment]::Is64BitProcess) { $sysDir = Join-Path $env:windir 'System32' }
    else { $sysDir = Join-Path $env:windir 'Sysnative' }
} else {
    if ([System.Environment]::Is64BitOperatingSystem) { $sysDir = Join-Path $env:windir 'SysWOW64' }
    else { $sysDir = Join-Path $env:windir 'System32' }
}

# Same order as the loader search path VxKex sets up: KexDir, app dir, system.
$script:SearchDirs = @($KexDir)
if ($fallbackKexDir) { $script:SearchDirs += $fallbackKexDir }
$script:SearchDirs += @((Split-Path -Parent $Target), $sysDir)
$script:SearchDirs += $ExtraSearchDir
$map = Read-RedirectTable $RedirectsHeader

$bits = 32
if ($is64) { $bits = 64 }
Write-Host "Target   : $Target ($bits-bit)"
Write-Host "KexDir   : $KexDir"
if ($fallbackKexDir) { Write-Host "Fallback : $fallbackKexDir (installed set, for DLLs not built by this repository)" }
Write-Host "System   : $sysDir"
Write-Host "Redirects: $RedirectsHeader ($($map.Count) entries)"
Write-Host "Imports  : $($imports.Count) functions"
Write-Host ''

$groups = New-Object System.Collections.Specialized.OrderedDictionary
foreach ($imp in $imports) {
    $k = $imp.Dll
    if ($imp.Delay) { $k = $k + ' (delay-load)' }
    if (-not $groups.Contains($k)) { $groups[$k] = New-Object System.Collections.ArrayList }
    [void]$groups[$k].Add($imp)
}

$missingStatic = 0
$missingDelay = 0
$warnings = 0

foreach ($k in $groups.Keys) {
    $list = $groups[$k]
    $dll = $list[0].Dll
    $delay = $list[0].Delay
    $redirected = Get-RedirectedDll $map $dll
    $effective = $dll
    $arrow = "$dll"
    if ($redirected) { $effective = $redirected; $arrow = "$dll -> $redirected" }
    if ($delay) { $arrow = $arrow + ' (delay-load)' }

    $bad = New-Object System.Collections.ArrayList
    $warn = New-Object System.Collections.ArrayList
    $resolvedFile = $null
    foreach ($imp in $list) {
        $r = Test-Symbol $effective $imp.Name $imp.Ordinal 0
        $label = $imp.Name
        if (-not $label) { $label = "ordinal #$($imp.Ordinal)" }
        switch ($r.Status) {
            'OK'             { $resolvedFile = $r.Detail }
            'NODLL'          { [void]$bad.Add("$label   [DLL file not found: $($r.Detail)]") }
            'MISSING'        { $resolvedFile = $r.Detail; [void]$bad.Add("$label   [not exported by $($r.Detail)]") }
            'FWD-BROKEN'     { [void]$bad.Add("$label   [broken forwarder: $($r.Detail)]") }
            'FWD-UNVERIFIED' { [void]$warn.Add("$label   [forwarder target DLL not found: $($r.Detail)]") }
        }
    }

    if ($bad.Count -gt 0) {
        $tag = '[MISSING]'
        if ($delay) { $tag = '[MISSING-DELAY]'; $missingDelay += $bad.Count } else { $missingStatic += $bad.Count }
        Write-Host "$tag $arrow : $($bad.Count) of $($list.Count)"
        foreach ($b in $bad) { Write-Host "    $b" }
    } elseif ($ShowOk) {
        Write-Host "[OK] $arrow ($resolvedFile) : $($list.Count)"
    }
    if ($warn.Count -gt 0) {
        $warnings += $warn.Count
        Write-Host "[WARN] $arrow : $($warn.Count) forwarder(s) could not be verified"
        foreach ($w in $warn) { Write-Host "    $w" }
    }
}

Write-Host ''
Write-Host "RESULT: static missing = $missingStatic, delay-load missing = $missingDelay, unverified forwarders = $warnings"
if ($missingStatic -gt 0) {
    Write-Host 'A missing STATIC import makes the loader fail with STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139) / Win32 error 127.'
    exit 1
}
if ($missingDelay -gt 0) {
    Write-Host 'Missing delay-load imports only fail when the function is first called.'
}
exit 0
