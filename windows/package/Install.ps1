[CmdletBinding()]
param([switch]$Silent)
$ErrorActionPreference = 'Stop'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$admin) {
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '"'
    if ($Silent) { $arguments += ' -Silent' }
    $child = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    exit $child.ExitCode
}
Add-Type -AssemblyName System.Windows.Forms
$base = [Environment]::GetFolderPath('ProgramFiles')
$target = Join-Path $base 'BluetoothHFP'
$staging = Join-Path $base ('BluetoothHFP.staging.' + [guid]::NewGuid().ToString('N'))
$previous = Join-Path $base ('BluetoothHFP.previous.' + [guid]::NewGuid().ToString('N'))
function RemoveOwnedDirectory([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if (!$full.StartsWith($base.TrimEnd('\') + '\BluetoothHFP.', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid installer cleanup target' }
    if (Test-Path -LiteralPath $full) {
        if ((Get-Item -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing reparse directory' }
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}
$installed = $false
try {
    if (Get-Process bluetooth_hfp,ax201_headset -ErrorAction SilentlyContinue) { throw 'Close Bluetooth HFP and stop its engine before installing or updating.' }
    $archive = Join-Path $PSScriptRoot 'BluetoothHFP.zip'
    $expected = (Get-Content (Join-Path $PSScriptRoot 'payload.sha256') -Raw).Trim()
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'Installer payload verification failed' }
    Expand-Archive -LiteralPath $archive -DestinationPath $staging
    foreach ($required in @('bluetooth_hfp.exe','flutter_windows.dll','engine\ax201_headset.exe','engine\ax201_driver.exe','engine\tools\Controller.ps1')) {
        if (!(Test-Path (Join-Path $staging $required))) { throw "Incomplete payload: $required" }
    }
    if (Test-Path $target) {
        if ((Get-Item $target).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing reparse installation directory' }
        # Both exact destinations are checked under the fixed Program Files directory.
        if ([IO.Path]::GetFullPath($target) -ne (Join-Path $base 'BluetoothHFP')) { throw 'Invalid install target' }
        Move-Item -LiteralPath $target -Destination $previous
    }
    Move-Item -LiteralPath $staging -Destination $target
    $installed = $true
    $menu = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Bluetooth HFP'
    New-Item -ItemType Directory -Force $menu | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    foreach ($entry in @(@('Bluetooth HFP',''),@('Restore Windows Bluetooth','--recover'))) {
        $shortcut = $shell.CreateShortcut((Join-Path $menu ($entry[0] + '.lnk')))
        $shortcut.TargetPath = Join-Path $target 'bluetooth_hfp.exe'
        $shortcut.Arguments = $entry[1]; $shortcut.WorkingDirectory = $target
        $shortcut.Save()
    }
    [Runtime.InteropServices.Marshal]::ReleaseComObject($shell) | Out-Null
    $key = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\BluetoothHFP'
    New-Item -Path $key -Force | Out-Null
    $uninstall = '"' + (Join-Path $env:windir 'System32\WindowsPowerShell\v1.0\powershell.exe') + '" -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $target 'engine\tools\Uninstall.ps1') + '"'
    $values = @{DisplayName='Bluetooth HFP';DisplayVersion='2.0.0';Publisher='Bluetooth HFP project';InstallLocation=$target;
        DisplayIcon=(Join-Path $target 'bluetooth_hfp.exe');UninstallString=$uninstall;QuietUninstallString=($uninstall + ' -Silent')}
    foreach ($name in $values.Keys) { New-ItemProperty -Path $key -Name $name -Value $values[$name] -PropertyType String -Force | Out-Null }
    New-ItemProperty -Path $key -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $key -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
    RemoveOwnedDirectory $previous
    if (!$Silent) { [Windows.Forms.MessageBox]::Show('Installation complete. Open Bluetooth HFP from the Start menu. Bluetooth drivers have not been changed by installation.', 'Bluetooth HFP') | Out-Null }
    exit 0
} catch {
    $reason = $_.Exception.Message
    # Never delete recovery packages or per-user settings during failed updates.
    if (!$installed -and (Test-Path $previous) -and !(Test-Path $target)) { Move-Item -LiteralPath $previous -Destination $target }
    if (Test-Path $staging) { RemoveOwnedDirectory $staging }
    if (!$Silent) { [Windows.Forms.MessageBox]::Show($reason,'Installation failed') | Out-Null }
    Write-Error $reason -ErrorAction Continue
    exit 1
}
