$content = [System.IO.File]::ReadAllText("C:\Users\liusj\AppData\Local\VxKex\Logs\codex-00134367237916402536-4068.vxl", [System.Text.Encoding]::Unicode)
$matches = [System.Text.RegularExpressions.Regex]::Matches($content, "[\x20-\x7E\r\n]{4,}")
foreach ($m in $matches) {
    if ($m.Value -match "(?i)error|fail|not found|exception|status|exit|cannot|rewrote|dll") {
        Write-Host $m.Value
    }
}
