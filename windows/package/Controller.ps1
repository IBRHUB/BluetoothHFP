[CmdletBinding()]
param([ValidateSet('Inspect','Headset','Native','Bootstrap')][string]$Action = 'Inspect')
$ErrorActionPreference = 'Stop'
$state = Join-Path $env:ProgramData 'BluetoothHFP'
$backups = Join-Path $state 'backups'
$bin = Join-Path $PSScriptRoot '..'
function DeviceProperty($id, $key) {
    (Get-PnpDeviceProperty -InstanceId $id -KeyName $key -ErrorAction SilentlyContinue).Data
}
function Inventory {
    $devices = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID_8087&PID_0026\*' })
    $items = @($devices | ForEach-Object {
        [ordered]@{id=$_.InstanceId; name=$_.FriendlyName; status=$_.Status;
            service=(DeviceProperty $_.InstanceId 'DEVPKEY_Device_Service');
            inf=(DeviceProperty $_.InstanceId 'DEVPKEY_Device_DriverInfPath'); support='AX201 experimental; tested on reference machine'}
    })
    [ordered]@{devices=$items; supported=($devices.Count -eq 1); backupPath=$backups;
        reason=$(if ($devices.Count -eq 1) {'AX201 found'} elseif ($devices.Count -eq 0) {'No tested AX201 USB 8087:0026 found; no driver will be changed'} else {'Multiple AX201 controllers found; detach extra controller before switching'})}
}
if ($Action -eq 'Inspect') { Inventory | ConvertTo-Json -Depth 6 -Compress; exit 0 }
function CheckedNative($exe, [string[]]$arguments) {
    & $exe @arguments | Out-String | Write-Verbose
    if ($LASTEXITCODE -eq 3010) { throw 'Windows requires a restart. Backup retained; no automatic reboot.' }
    if ($LASTEXITCODE -ne 0) { throw "Native operation failed: $exe (exit $LASTEXITCODE)" }
}
function VerifyBackup($directory) {
    $root = (Resolve-Path -LiteralPath $directory).Path
    $snapshot = Get-Content -LiteralPath (Join-Path $root 'snapshot.json') -Raw | ConvertFrom-Json
    if ($snapshot.InstanceId -ne $script:device.InstanceId -or $snapshot.Service -ne 'BTHUSB' -or
        $snapshot.DriverProvider -ne 'Intel Corporation' -or $snapshot.Inf -notmatch '^oem\d+\.inf$') { throw 'Original Intel backup does not match this device' }
    $hashes = Get-Content -LiteralPath (Join-Path $root 'hashes.json') -Raw | ConvertFrom-Json
    if (!$hashes -or @($hashes).Count -eq 0) { throw 'Empty backup' }
    foreach ($entry in $hashes) {
        $file = [IO.Path]::GetFullPath((Join-Path $root $entry.RelativePath))
        if (!$file.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
            (Get-FileHash -LiteralPath $file).Hash -ne $entry.SHA256) { throw 'Driver backup integrity check failed' }
    }
    return $snapshot
}
function Bind($mode) {
    $inf = if ($mode -eq 'Native') { Join-Path $env:windir "INF\$($script:snapshot.Inf)" } else { Join-Path $env:windir 'INF\winusb.inf' }
    $section = if ($mode -eq 'Native') { 'ibtusb' } else { 'WINUSB' }
    CheckedNative (Join-Path $bin 'ax201_driver.exe') @('list',$device.InstanceId,$inf,$section)
    CheckedNative (Join-Path $bin 'ax201_driver.exe') @('install',$device.InstanceId,$inf,$section)
    $service = DeviceProperty $device.InstanceId 'DEVPKEY_Device_Service'
    $expected = if ($mode -eq 'Native') {'BTHUSB'} else {'WINUSB'}
    if ($service -ne $expected) { throw 'Driver service verification failed' }
}
$changed = $false
$snapshot = $null
try {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (!$admin) { throw 'Administrator permission is required only to switch drivers' }
    New-Item -ItemType Directory -Force $state,$backups | Out-Null
    # Recovery packages can be read by users, but only Administrators/SYSTEM may modify them.
    & icacls.exe $state /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
    if ($LASTEXITCODE) { throw 'Cannot protect recovery folder' }
    if (Get-Process ax201_headset -ErrorAction SilentlyContinue) { throw 'Stop the headset in the app before switching Bluetooth mode' }
    $devices = @(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'USB\VID_8087&PID_0026\*')
    if ($devices.Count -ne 1) { throw 'Exactly one AX201 USB 8087:0026 is required. No driver changed.' }
    $script:device = $devices[0]
    $service = DeviceProperty $device.InstanceId 'DEVPKEY_Device_Service'
    if ($Action -eq 'Native' -and $service -eq 'BTHUSB' -and $device.Status -eq 'OK') {
        @{ok=$true;message='Windows Bluetooth already active'} | ConvertTo-Json | Set-Content (Join-Path $state 'operation.json') -Encoding UTF8
        exit 0
    }
    $candidates = @(Get-ChildItem $backups -Directory | Sort-Object Name -Descending | Where-Object {
        $p = Join-Path $_.FullName 'snapshot.json'
        (Test-Path $p) -and ((Get-Content $p -Raw | ConvertFrom-Json).InstanceId -eq $device.InstanceId)
    })
    if (!$candidates.Count) {
        if ($service -ne 'BTHUSB') { throw 'No original driver backup found. Restore the Intel driver before first setup.' }
        & (Join-Path $PSScriptRoot 'Backup-Driver.ps1') -OutputDirectory $backups | Out-Null
        $candidates = @(Get-ChildItem $backups -Directory | Sort-Object Name -Descending)
    }
    $backup = $candidates[0].FullName
    $script:snapshot = VerifyBackup $backup
    $original = Join-Path $env:windir "INF\$($snapshot.Inf)"
    if (!(Test-Path $original) -or (Get-FileHash $original).Hash -ne (Get-FileHash (Join-Path $backup $snapshot.Inf)).Hash) {
        throw 'Original driver store INF changed; use saved recovery instructions before switching'
    }
    CheckedNative (Join-Path $bin 'ax201_driver.exe') @('list',$device.InstanceId,$original,'ibtusb')
    $wifiBefore = @{}
    foreach ($wifi in $snapshot.Wifi) { $wifiBefore[$wifi.InstanceId] = (Get-PnpDevice -InstanceId $wifi.InstanceId -PresentOnly).Status }
    $changed = $true
    if ($Action -eq 'Native') {
        Bind 'Native'
        & (Join-Path $PSScriptRoot 'Restore-Driver.ps1') -BackupDirectory $backup -VerifyOnly | Out-Null
    } else {
        if ($Action -eq 'Bootstrap') { Bind 'Native'; Start-Sleep -Seconds 2 }
        if ((DeviceProperty $device.InstanceId 'DEVPKEY_Device_Service') -ne 'WINUSB') { Bind 'Headset' }
        try { CheckedNative (Join-Path $bin 'ax201_probe.exe') @('--hci') }
        catch {
            if ($Action -eq 'Bootstrap') { throw }
            Bind 'Native'; Start-Sleep -Seconds 2; Bind 'Headset'
            CheckedNative (Join-Path $bin 'ax201_probe.exe') @('--hci')
        }
    }
    foreach ($wifi in $snapshot.Wifi) {
        if ((Get-PnpDevice -InstanceId $wifi.InstanceId -PresentOnly).Status -ne $wifiBefore[$wifi.InstanceId]) { throw 'Wi-Fi status changed unexpectedly' }
    }
    @{ok=$true;message='Driver and controller verified';mode=$Action;backup=$backup} | ConvertTo-Json | Set-Content (Join-Path $state 'operation.json') -Encoding UTF8
    exit 0
} catch {
    $reason = $_.Exception.Message
    $trace = $_.ScriptStackTrace
    if ($changed -and $snapshot) {
        try { Bind 'Native'; $reason += '; original Intel binding restored' }
        catch { $reason += '; automatic recovery failed: ' + $_.Exception.Message + '. Recovery files retained at ' + $backups }
    }
    if (Test-Path $state) { @{ok=$false;message=$reason;trace=$trace} | ConvertTo-Json | Set-Content (Join-Path $state 'operation.json') -Encoding UTF8 }
    Write-Error $reason -ErrorAction Continue
    exit 1
}
