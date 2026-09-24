[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BackupDirectory, [switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $BackupDirectory).Path
$snapshot = Get-Content -LiteralPath (Join-Path $root 'snapshot.json') -Raw | ConvertFrom-Json
if ($snapshot.InstanceId -notlike 'USB\VID_8087&PID_0026\*') { throw 'Backup is not for AX201 Bluetooth' }
$hashes = Get-Content -LiteralPath (Join-Path $root 'hashes.json') -Raw | ConvertFrom-Json
if (-not $hashes -or @($hashes).Count -eq 0) { throw 'Backup hash manifest is empty' }
foreach ($entry in $hashes) {
    $file = [IO.Path]::GetFullPath((Join-Path $root $entry.RelativePath))
    if (-not $file.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup path' }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.SHA256) { throw "Backup hash mismatch: $file" }
}
if (-not $VerifyOnly) {
    foreach ($inf in Get-ChildItem -LiteralPath (Join-Path $root 'driver-package') -Filter '*.inf' -Recurse) {
        # Stage only: /install can affect every matching device, so binding is deliberately per-device Have Disk.
        & pnputil.exe /add-driver $inf.FullName
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) { throw 'Original driver package staging failed' }
    }
}
$device = Get-PnpDevice -InstanceId $snapshot.InstanceId -PresentOnly
$props = @(Get-PnpDeviceProperty -InstanceId $snapshot.InstanceId)
$service = ($props | Where-Object KeyName -eq 'DEVPKEY_Device_Service').Data
$version = ($props | Where-Object KeyName -eq 'DEVPKEY_Device_DriverVersion').Data
$provider = ($props | Where-Object KeyName -eq 'DEVPKEY_Device_DriverProvider').Data
$filters = @(($props | Where-Object KeyName -eq 'DEVPKEY_Device_LowerFilters').Data)
if ($device.Status -ne 'OK' -or $service -ne $snapshot.Service -or $version -ne $snapshot.DriverVersion -or
    $provider -ne $snapshot.DriverProvider -or ($filters -join ';') -ne ($snapshot.LowerFilters -join ';')) {
    throw 'Original driver binding not restored. Follow the exact-device Have Disk steps in ROLLBACK.txt, then verify again.'
}
foreach ($wifi in $snapshot.Wifi) {
    $now = Get-PnpDevice -InstanceId $wifi.InstanceId -PresentOnly
    if ($now.Status -ne $wifi.Status) { throw "Wi-Fi status changed: $($now.Status)" }
}
Write-Output '[ROLLBACK] Original Bluetooth provider/version/service/filters verified; Wi-Fi PnP status verified'
