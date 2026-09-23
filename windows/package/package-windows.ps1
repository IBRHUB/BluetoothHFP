param([switch]$Register)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$release = Join-Path $repo 'build/windows/x64/runner/Release'
if (!(Test-Path (Join-Path $release 'bluetooth_hfp.exe'))) {
    throw 'Build first: flutter build windows --release'
}
$stage = Join-Path $repo 'build/package'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
# Keep the staging directory stable for loose-package development registration.
Copy-Item -Path (Join-Path $release '*') -Destination $stage -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repo 'windows/package/AppxManifest.xml') -Destination $stage -Force
$assets = Join-Path $stage 'Assets'
New-Item -ItemType Directory -Force -Path $assets | Out-Null
Add-Type -AssemblyName System.Drawing
foreach ($entry in @(@('StoreLogo.png',50), @('Logo.png',150), @('SmallLogo.png',44))) {
    $size = [int]$entry[1]
    $bitmap = New-Object System.Drawing.Bitmap($size,$size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $font = New-Object System.Drawing.Font('Segoe UI',($size / 3),[System.Drawing.FontStyle]::Bold)
    try {
        $graphics.Clear([System.Drawing.Color]::Black)
        $graphics.DrawString('BT',$font,[System.Drawing.Brushes]::White,0,($size / 5))
        $bitmap.Save((Join-Path $assets $entry[0]),[System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $font.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
}
$sdk = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/bin' -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'x64/makeappx.exe') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (!$sdk) { throw 'Windows SDK makeappx.exe is required.' }
$package = Join-Path $repo 'build/BluetoothHFP.msix'
& (Join-Path $sdk.FullName 'x64/makeappx.exe') pack /d $stage /p $package /o
if ($LASTEXITCODE -ne 0) { throw 'MSIX validation/packaging failed.' }
if ($Register) {
    # Developer Mode is required. No signing certificates or system policy changes.
    Add-AppxPackage -Register (Join-Path $stage 'AppxManifest.xml') -DisableDevelopmentMode:$false
    $installed = Get-AppxPackage -Name BluetoothHFP.Desktop
    $shortcutPath = Join-Path $repo 'build/Launch Bluetooth HFP.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = Join-Path $env:WINDIR 'explorer.exe'
    $shortcut.Arguments = 'shell:AppsFolder\' + $installed.PackageFamilyName + '!App'
    $shortcut.IconLocation = Join-Path $stage 'bluetooth_hfp.exe'
    $shortcut.Save()
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut) | Out-Null
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($shell) | Out-Null
    Write-Output "Registered app: shell:AppsFolder\$($installed.PackageFamilyName)!App"
}
Write-Output "Unsigned distribution package (sign before distribution): $package"
Write-Output "Local test app: $stage"
