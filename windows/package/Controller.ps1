[CmdletBinding()]
param([ValidateSet('Inspect','Headset','Native','Bootstrap')][string]$Action = 'Inspect', [string]$ExpectedInstanceId)
$ErrorActionPreference = 'Stop'
$state = Join-Path $env:ProgramData 'BluetoothHFP'
$backups = Join-Path $state 'backups'
$bin = Join-Path $PSScriptRoot '..'
$policy = Join-Path $PSScriptRoot 'ControllerProfiles.ps1'
if (!(Test-Path $policy)) { $policy = Join-Path $PSScriptRoot '..\..\native-poc\tools\ControllerProfiles.ps1' }
. $policy
function DeviceProperty($id, $key) {
    (Get-PnpDeviceProperty -InstanceId $id -KeyName $key -ErrorAction SilentlyContinue).Data
}
function Inventory {
    $inventory = Get-ControllerInventory
    $inventory.backupPath = $backups
    $inventory
}
if ($Action -eq 'Inspect') { Inventory | ConvertTo-Json -Depth 6 -Compress; exit 0 }
function CheckedNative($exe, [string[]]$arguments) {
    & $exe @arguments | Out-String | Write-Verbose
    if ($LASTEXITCODE -eq 3010) { throw 'Windows requires a restart. Backup retained; no automatic reboot.' }
    if ($LASTEXITCODE -ne 0) { throw "Native operation failed: $exe (exit $LASTEXITCODE)" }
}
function VerifyBackup($directory) {
    Read-ControllerBackup $directory $script:device.InstanceId
}
function Bind($mode) {
    $inf = if ($mode -eq 'Native') { Join-Path $env:windir "INF\$($script:snapshot.Inf)" } else { Join-Path $env:windir 'INF\winusb.inf' }
    $section = if ($mode -eq 'Native') { Get-RecoverySection $script:snapshot $script:profile } else { 'WINUSB' }
    CheckedNative (Join-Path $bin 'ax201_driver.exe') @('list',$device.InstanceId,$inf,$section)
    CheckedNative (Join-Path $bin 'ax201_driver.exe') @('install',$device.InstanceId,$inf,$section)
    $service = DeviceProperty $device.InstanceId 'DEVPKEY_Device_Service'
    $expected = if ($mode -eq 'Native') {$script:profile.originalService} else {'WINUSB'}
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
    $inventory = Inventory
    Assert-ControllerSelection $inventory $ExpectedInstanceId
    $script:device = Get-PnpDevice -InstanceId $inventory.selectedId -PresentOnly
    $script:profile = Find-ControllerProfile $device.InstanceId
    $service = DeviceProperty $device.InstanceId 'DEVPKEY_Device_Service'
    if ($Action -eq 'Native' -and $service -eq $profile.originalService -and $device.Status -eq 'OK') {
        @{ok=$true;message='Windows Bluetooth already active'} | ConvertTo-Json | Set-Content (Join-Path $state 'operation.json') -Encoding UTF8
        exit 0
    }
    $candidates = @(Get-ChildItem $backups -Directory | Sort-Object Name -Descending | Where-Object {
        $p = Join-Path $_.FullName 'snapshot.json'
        (Test-Path $p) -and ((Get-Content $p -Raw | ConvertFrom-Json).InstanceId -eq $device.InstanceId)
    })
    if (!$candidates.Count) {
        if ($service -ne $profile.originalService) { throw 'No original driver backup found. Restore the original driver before first setup.' }
        & (Join-Path $PSScriptRoot 'Backup-Driver.ps1') -OutputDirectory $backups -InstanceId $device.InstanceId | Out-Null
        $candidates = @(Get-ChildItem $backups -Directory | Sort-Object Name -Descending | Where-Object {
            $p = Join-Path $_.FullName 'snapshot.json'
            (Test-Path $p) -and ((Get-Content $p -Raw | ConvertFrom-Json).InstanceId -eq $device.InstanceId)
        })
    }
    $backup = $candidates[0].FullName
    $script:snapshot = VerifyBackup $backup
    $original = Join-Path $env:windir "INF\$($snapshot.Inf)"
    if (!(Test-Path $original) -or (Get-FileHash $original).Hash -ne (Get-FileHash (Join-Path $backup $snapshot.Inf)).Hash) {
        throw 'Original driver store INF changed; use saved recovery instructions before switching'
    }
    CheckedNative (Join-Path $bin 'ax201_driver.exe') @('list',$device.InstanceId,$original,(Get-RecoverySection $snapshot $profile))
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
        try { Bind 'Native'; $reason += '; original driver binding restored' }
        catch { $reason += '; automatic recovery failed: ' + $_.Exception.Message + '. Recovery files retained at ' + $backups }
    }
    if (Test-Path $state) { @{ok=$false;message=$reason;trace=$trace} | ConvertTo-Json | Set-Content (Join-Path $state 'operation.json') -Encoding UTF8 }
    Write-Error $reason -ErrorAction Continue
    exit 1
}
