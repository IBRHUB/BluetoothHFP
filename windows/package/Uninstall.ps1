[CmdletBinding()]
param([switch]$Silent)
$ErrorActionPreference = 'Stop'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$admin) {
    $argsLine = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '"'
    if ($Silent) { $argsLine += ' -Silent' }
    $p = Start-Process powershell.exe -Verb RunAs -ArgumentList $argsLine -WindowStyle Hidden -Wait -PassThru
    exit $p.ExitCode
}
Add-Type -AssemblyName System.Windows.Forms
try {
    $target = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    $expected = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'BluetoothHFP'
    if ($target.TrimEnd('\') -ne $expected.TrimEnd('\')) { throw 'Uninstall must run from the installed application directory' }
    if ((Get-Item $target).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing reparse installation directory' }
    if (Get-Process bluetooth_hfp,ax201_headset -ErrorAction SilentlyContinue) { throw 'Close Bluetooth HFP and stop the headset before uninstalling.' }
    if (!$Silent -and [Windows.Forms.MessageBox]::Show('Restore Windows Bluetooth and remove the app? Settings and driver backups will be preserved.','Bluetooth HFP','YesNo') -ne 'Yes') { exit 2 }
    $backups = Join-Path $env:ProgramData 'BluetoothHFP\backups'
    if (Test-Path $backups) {
        if (@(Get-ChildItem $backups -Filter snapshot.json -Recurse).Count) {
            $powershell = Join-Path $env:windir 'System32\WindowsPowerShell\v1.0\powershell.exe'
            & $powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Controller.ps1') -Action Native
            if ($LASTEXITCODE) { throw 'Bluetooth recovery failed. App and backups retained. Reconnect the AX201 controller, retry recovery, then uninstall.' }
        }
    }
    # Exact fixed install path was checked above; recovery lives outside this tree.
    Remove-Item -LiteralPath $target -Recurse -Force
    $menu = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Bluetooth HFP'
    foreach ($name in @('Bluetooth HFP.lnk','Restore Windows Bluetooth.lnk')) {
        $shortcut = Join-Path $menu $name
        if (Test-Path $shortcut) { Remove-Item -LiteralPath $shortcut -Force }
    }
    if ((Test-Path $menu) -and !(Get-ChildItem -LiteralPath $menu)) { Remove-Item -LiteralPath $menu }
    Remove-Item -LiteralPath 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\BluetoothHFP' -ErrorAction SilentlyContinue
    if (!$Silent) { [Windows.Forms.MessageBox]::Show('Application removed. Bluetooth recovery files and your settings were preserved.','Bluetooth HFP') | Out-Null }
    exit 0
} catch {
    if (!$Silent) { [Windows.Forms.MessageBox]::Show($_.Exception.Message,'Uninstall stopped') | Out-Null }
    Write-Error $_ -ErrorAction Continue
    exit 1
}
