param(
    [string]$ShortcutPath
)

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ShortcutPath -or [string]::IsNullOrWhiteSpace($ShortcutPath)) {
    $ShortcutPath = Join-Path $projectRoot "LlamaCode.lnk"
}

$exePath = Join-Path $projectRoot "build\\Debug\\LlamaCode.exe"
$iconPath = Join-Path $projectRoot "assets\\app_icon.ico"

$wsh = New-Object -ComObject WScript.Shell
$shortcut = $wsh.CreateShortcut($ShortcutPath)

if (Test-Path $exePath) {
    $shortcut.TargetPath = $exePath
    $shortcut.Arguments = ""
    $shortcut.WorkingDirectory = Split-Path -Parent $exePath
    # Use the executable icon so the shortcut always matches the built app.
    $shortcut.IconLocation = "$exePath,0"
} else {
    # Fallback when the binary does not exist yet.
    $shortcut.TargetPath = "$env:WINDIR\\System32\\cmd.exe"
    $shortcut.Arguments = '/c start "" ".\build\Debug\LlamaCode.exe"'
    $shortcut.WorkingDirectory = $projectRoot
    if (Test-Path $iconPath) {
        $shortcut.IconLocation = "$iconPath,0"
    }
}

$shortcut.Save()

$saved = $wsh.CreateShortcut($ShortcutPath)
[PSCustomObject]@{
    ShortcutPath      = $ShortcutPath
    TargetPath        = $saved.TargetPath
    Arguments         = $saved.Arguments
    WorkingDirectory  = $saved.WorkingDirectory
    IconLocation      = $saved.IconLocation
} | Format-List
