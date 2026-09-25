$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\ControllerProfiles.ps1')
function Assert($condition, [string]$message) { if (!$condition) { throw $message } }
function Reject([scriptblock]$action, [string]$message) {
    $rejected = $false
    try { & $action | Out-Null } catch { $rejected = $true }
    Assert $rejected $message
}
function Radio([string]$id, [string]$service = 'BTHUSB') {
    [pscustomobject]@{id=$id; name='test radio'; status='OK'; service=$service; inf='oem1.inf'; driverVersion='test'}
}
$reference = Radio 'USB\VID_8087&PID_0026\REFERENCE' 'WINUSB'
$unknown = Radio 'USB\VID_0BDA&PID_8771\OTHER'
$assessment = Get-ControllerAssessment @($unknown,$reference)
Assert $assessment.supported 'An unrelated radio must not disable the reference controller'
Assert ($assessment.selectedId -eq $reference.id) 'Never select the first unrelated device'
Assert ($assessment.devices[0].support -eq 'unsupported') 'Unknown radio must remain blocked'
Assert ($assessment.devices[1].support -eq 'reference-tested') 'Reference evidence must be preserved'
Assert-ControllerSelection $assessment $reference.id
Reject { Assert-ControllerSelection $assessment 'USB\VID_8087&PID_0026\REMOVED' } 'Device replacement during elevation must be rejected'
Reject { Assert-ControllerSelection (Get-ControllerAssessment @()) $reference.id } 'Disconnect before elevation must be rejected'
Assert (!(Get-ControllerAssessment @()).supported) 'No radio must block enable'
Assert (!(Get-ControllerAssessment @($unknown)).supported) 'Unknown radio must block enable'
Assert (!(Get-ControllerAssessment @($reference,(Radio 'USB\VID_8087&PID_0026\SECOND'))).supported) 'Ambiguous radios must block enable'
foreach ($id in @('USB\VID_8087&PID_00260\X','USB\VID_8087&PID_0026&MI_00\X','USB\VID_8087&PID_0026\','USB\VID_8087&PID_0026\X\Y','PCI\VID_8087&PID_0026\X')) {
    Assert ($null -eq (Find-ControllerProfile $id)) "Partial or child identity accepted: $id"
}
$profile = Find-ControllerProfile $reference.id
foreach ($virtual in @('BTHLEDEVICE\SERVICE\X','BTHENUM\PHONE\X','BTH\MS_BTHBRB\X','SWD\RADIO\X')) {
    Assert (!(Test-PhysicalBluetoothController ([pscustomobject]@{InstanceId=$virtual; Class='Bluetooth'}))) 'Paired peripherals must not appear as controllers'
}
Assert (Test-PhysicalBluetoothController ([pscustomobject]@{InstanceId=$unknown.id; Class='Bluetooth'})) 'Physical unknown radios must remain visible'
Assert (Test-PhysicalBluetoothController ([pscustomobject]@{InstanceId=$reference.id; Class='USBDevice'})) 'Managed WinUSB radios must remain visible'
Assert ($null -ne (Find-ControllerProfile $reference.id.ToLowerInvariant())) 'Identity matching must ignore case'
$experimental = $profile | Select-Object *
$experimental.validation = 'experimental'
# Test the actual assessment path with a synthetic catalog, without touching disk or PnP.
& {
    function Get-ControllerProfiles { $experimental }
    Assert (!(Get-ControllerAssessment @($reference)).supported) 'Experimental profiles must not authorize driver changes'
}
$snapshot = [pscustomobject]@{
    InstanceId=$reference.id; Inf='oem1.inf'; Service='BTHUSB'; DriverProvider='Intel Corporation'
    DriverInfSection=$null; ProfileId=$null; SchemaVersion=$null
}
Assert ((Get-RecoverySection $snapshot $profile) -eq 'ibtusb') 'Legacy backups must remain recoverable'
$snapshot.SchemaVersion=2; $snapshot.ProfileId=$profile.id; $snapshot.DriverInfSection='ibtusb.NTamd64'
Assert ((Get-RecoverySection $snapshot $profile) -eq 'ibtusb.NTamd64') 'Use the recorded driver section'
$snapshot.DriverInfSection=$null
Reject { Get-RecoverySection $snapshot $profile } 'New backups must not silently guess a missing section'
$snapshot.DriverInfSection='ibtusb'; $snapshot.InstanceId=$unknown.id
Reject { Get-RecoverySection $snapshot $profile } 'Reject cross-controller backups'
$snapshot.InstanceId=$reference.id; $snapshot.DriverProvider='Other vendor'
Reject { Get-RecoverySection $snapshot $profile } 'Reject wrong driver provider'
Write-Output '[PASS] Controller discovery, isolation, experimental gate and recovery metadata'

# Exercise real backup validation/recovery code using files and PnP test doubles.
# No driver helper, PnP mutation, firmware command or real driver store is invoked.
$testParent = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\build'))
$testRoot = Join-Path $testParent ('compatibility-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $testRoot 'driver-package') -Force | Out-Null
try {
    $snapshot.DriverProvider='Intel Corporation'
    $snapshot | Add-Member DriverVersion 'test' -Force
    $snapshot | Add-Member LowerFilters @() -Force
    $snapshot | Add-Member Wifi @() -Force
    function Save-TestBackup {
        $snapshot | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $testRoot 'snapshot.json') -Encoding UTF8
        'original inf' | Set-Content (Join-Path $testRoot 'oem1.inf')
        'original package' | Set-Content (Join-Path $testRoot 'driver-package\driver.inf')
        @('snapshot.json','oem1.inf','driver-package\driver.inf') | ForEach-Object {
            [pscustomobject]@{ RelativePath=$_; SHA256=(Get-FileHash (Join-Path $testRoot $_)).Hash }
        } | ConvertTo-Json | Set-Content (Join-Path $testRoot 'hashes.json')
    }
    Save-TestBackup
    Assert ((Read-ControllerBackup $testRoot $reference.id).DriverInfSection -eq 'ibtusb') 'Valid backup must load'
    Reject { Read-ControllerBackup $testRoot $unknown.id } 'Wrong physical instance must reject recovery'
    'changed inf' | Set-Content (Join-Path $testRoot 'oem1.inf')
    Reject { Read-ControllerBackup $testRoot } 'Changed recovery INF must be detected'
    Save-TestBackup
    $snapshot.DriverInfSection='anotherSection'
    $snapshot | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $testRoot 'snapshot.json') -Encoding UTF8
    Reject { Read-ControllerBackup $testRoot } 'Changed recovery metadata must be detected'
    $snapshot.DriverInfSection='ibtusb'
    Save-TestBackup
    @([pscustomobject]@{RelativePath='..\outside.inf'; SHA256='wrong'}) | ConvertTo-Json | Set-Content (Join-Path $testRoot 'hashes.json')
    Reject { Read-ControllerBackup $testRoot } 'Incomplete manifest must reject recovery'
    Save-TestBackup
    & {
        function Get-PnpDevice { [pscustomobject]@{ Status='OK' } }
        function Get-PnpDeviceProperty {
            @(
                [pscustomobject]@{KeyName='DEVPKEY_Device_Service'; Data='BTHUSB'}
                [pscustomobject]@{KeyName='DEVPKEY_Device_DriverVersion'; Data='test'}
                [pscustomobject]@{KeyName='DEVPKEY_Device_DriverProvider'; Data='Intel Corporation'}
                [pscustomobject]@{KeyName='DEVPKEY_Device_LowerFilters'; Data=@()}
            )
        }
        function pnputil.exe { throw 'VerifyOnly must never stage a driver' }
        & (Join-Path $PSScriptRoot '..\tools\Restore-Driver.ps1') -BackupDirectory $testRoot -VerifyOnly
        function Get-PnpDevice { throw 'Device disconnected' }
        Reject { & (Join-Path $PSScriptRoot '..\tools\Restore-Driver.ps1') -BackupDirectory $testRoot -VerifyOnly } 'Disconnected controller must not report successful recovery'
    }
    Write-Output '[PASS] Recovery verification, disconnected device, changed metadata/INF and incomplete backups'
} finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    if (!$resolvedTestRoot.StartsWith($testParent.TrimEnd('\') + '\compatibility-test-', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid test cleanup directory' }
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
