[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('WinUSB','Intel')][string]$Mode,
    [string]$BackupDirectory
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $BackupDirectory) {
    $backup = Get-ChildItem (Join-Path $repo '.local\ax201-backup') -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $backup) { throw 'Run Backup-Driver.ps1 while the Intel driver is active first' }
    $BackupDirectory = $backup.FullName
}
$root = (Resolve-Path -LiteralPath $BackupDirectory).Path
$snapshot = Get-Content (Join-Path $root 'snapshot.json') -Raw | ConvertFrom-Json
if ($snapshot.InstanceId -notlike 'USB\VID_8087&PID_0026\*' -or $snapshot.Service -ne 'BTHUSB' -or
    $snapshot.DriverProvider -ne 'Intel Corporation' -or $snapshot.Inf -notmatch '^oem\d+\.inf$') { throw 'Not an original Intel AX201 backup' }
$manifest = Get-Content (Join-Path $root 'hashes.json') -Raw | ConvertFrom-Json
if (-not $manifest) { throw 'Empty backup hash manifest' }
foreach ($entry in $manifest) {
    $file = [IO.Path]::GetFullPath((Join-Path $root $entry.RelativePath))
    if (-not $file.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup path' }
    if ((Get-FileHash -LiteralPath $file).Hash -ne $entry.SHA256) { throw "Backup hash mismatch: $file" }
}
$helper = Join-Path $repo 'build\ax201-poc\Release\ax201_driver.exe'
if (-not (Test-Path $helper)) { $helper = Join-Path $root 'ax201_driver.exe' }
if (-not (Test-Path $helper)) { throw 'Build.ps1 must complete first' }
# The original package must still be in the driver store. Never delete OEM packages.
$originalInf = Join-Path $env:windir "INF\$($snapshot.Inf)"
if (-not (Test-Path $originalInf)) { throw 'Original INF is not staged; follow ROLLBACK.txt first' }
if ((Get-FileHash $originalInf).Hash -ne (Get-FileHash (Join-Path $root $snapshot.Inf)).Hash) { throw 'Original INF no longer matches backup' }
if (Get-Process ax201_headset -ErrorAction SilentlyContinue) { throw 'Stop the headset process before changing USB ownership' }
$wifiBefore = @{}
foreach ($wifi in $snapshot.Wifi) { $wifiBefore[$wifi.InstanceId] = (Get-PnpDevice -InstanceId $wifi.InstanceId -PresentOnly).Status }
$inf = if ($Mode -eq 'WinUSB') { Join-Path $env:windir 'INF\winusb.inf' } else { $originalInf }
$section = if ($Mode -eq 'WinUSB') { 'WINUSB' } else { 'ibtusb' }
& $helper list $snapshot.InstanceId $originalInf 'ibtusb'
if ($LASTEXITCODE) { throw 'Original driver recovery candidate missing' }
& $helper list $snapshot.InstanceId $inf $section
if ($LASTEXITCODE) { throw 'Replacement candidate missing' }
Copy-Item -LiteralPath $helper -Destination (Join-Path $root 'ax201_driver.exe') -Force -ErrorAction SilentlyContinue
try {
    & $helper install $snapshot.InstanceId $inf $section
    if ($LASTEXITCODE -eq 3010) { throw 'Windows requires a reboot; automatic reboot is disabled' }
    if ($LASTEXITCODE) { throw 'Driver binding failed' }
    if ($Mode -eq 'WinUSB') {
        & (Join-Path $repo 'build\ax201-poc\Release\ax201_probe.exe') --probe
        if ($LASTEXITCODE) { throw 'WinUSB gate failed after binding' }
    } else {
        & (Join-Path $PSScriptRoot 'Restore-Driver.ps1') -BackupDirectory $root -VerifyOnly
    }
    foreach ($wifi in $snapshot.Wifi) {
        if ((Get-PnpDevice -InstanceId $wifi.InstanceId -PresentOnly).Status -ne $wifiBefore[$wifi.InstanceId]) { throw 'Wi-Fi PnP status changed' }
    }
    Write-Output "[DRIVER] $Mode binding verified; Wi-Fi PnP status unchanged"
} catch {
    $failure = $_
    Write-Warning 'Binding failed. Restoring the original Intel driver on the exact Bluetooth instance.'
    & $helper install $snapshot.InstanceId $originalInf 'ibtusb'
    $recoveryExit = $LASTEXITCODE
    if ($recoveryExit -ne 0) { Write-Warning "Recovery returned $recoveryExit. Follow $root\ROLLBACK.txt" }
    throw $failure
}
