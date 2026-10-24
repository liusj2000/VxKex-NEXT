$ErrorActionPreference = 'Continue'
$result = 'D:\project\VxKex-NEXT\docx\extension-inspect-20261024.txt'
& {
    'READ_ONLY_DIAGNOSTICS'
    Get-Date -Format o
    foreach ($drive in [Environment]::GetLogicalDrives()) {
        try {
            $info = [IO.DriveInfo]::new($drive)
            '{0} type={1} ready={2}' -f $drive, $info.DriveType, $info.IsReady
        } catch { '{0} ERROR {1}' -f $drive, $_.Exception.Message }
    }
    Get-ChildItem 'C:\Users\liusj\.gemini\antigravity\log' -Force | Select-Object Name,Length,LastWriteTime | Format-Table
    Get-ChildItem 'C:\Users\liusj\.gemini\antigravity\crashes' -Force | Select-Object Name,Length,LastWriteTime | Format-Table
    $source = [IO.File]::ReadAllText('D:\VSCode-win32-x64-1.107.1\data\extensions\google.google-antigravity-1.9.0\extension.js')
    foreach ($pattern in @('probe request timed out','Timed out waiting for server')) {
        $index = $source.IndexOf($pattern)
        if ($index -ge 0) {
            $start = [Math]::Max(0, $index - 2200)
            $source.Substring($start, [Math]::Min(4500, $source.Length - $start))
        }
    }
    Get-ChildItem 'D:\VSCode-win32-x64-1.107.1\data\extensions\openai.chatgpt-26.1021.22027-win32-x64' | Select-Object Name | Format-Table
    'DIAGNOSTICS_COMPLETE'
} 2>&1 | Out-File -LiteralPath $result -Width 260 -Encoding utf8