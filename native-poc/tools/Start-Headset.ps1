[CmdletBinding()]
param([ValidateSet('Auto','CVSD')][string]$Codec = 'Auto')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$exe = Join-Path $repo 'build\ax201-headset\Release\ax201_headset.exe'
if (-not (Test-Path $exe)) { throw 'Run Build-Headset.ps1 first' }
if (Get-Process ax201_headset -ErrorAction SilentlyContinue) { throw 'Headset is already running' }
$instance = 'USB\VID_8087&PID_0026\5&1A60D403&0&14'
$service = (Get-PnpDeviceProperty -InstanceId $instance -KeyName DEVPKEY_Device_Service).Data
if ($service -ne 'WINUSB') { & (Join-Path $PSScriptRoot 'Set-BluetoothDriver.ps1') -Mode WinUSB }
$run = Join-Path $repo '.local\hfp-run'
New-Item -ItemType Directory -Force $run | Out-Null
& (Join-Path $repo 'build\ax201-poc\Release\ax201_probe.exe') --hci | Tee-Object (Join-Path $run 'hci-preflight.log')
if ($LASTEXITCODE) {
    # No guessed SFI writes. Use the preserved Intel driver as a firmware bootstrap,
    # then return exclusive USB ownership to our host stack and verify real replies.
    Write-Output '[FW] HCI preflight failed; initializing with original Intel driver once'
    & (Join-Path $PSScriptRoot 'Set-BluetoothDriver.ps1') -Mode Intel
    Start-Sleep -Seconds 2
    & (Join-Path $PSScriptRoot 'Set-BluetoothDriver.ps1') -Mode WinUSB
    & (Join-Path $repo 'build\ax201-poc\Release\ax201_probe.exe') --hci | Tee-Object (Join-Path $run 'hci-preflight-retry.log')
    if ($LASTEXITCODE) {
        & (Join-Path $PSScriptRoot 'Set-BluetoothDriver.ps1') -Mode Intel
        throw 'HCI still failed after Intel bootstrap; original binding restored'
    }
}
if (Test-Path (Join-Path $run 'command.txt')) { Remove-Item -LiteralPath (Join-Path $run 'command.txt') }
$previousCodec = $env:AX201_HFP_CODEC
$env:AX201_HFP_CODEC = $Codec
try {
$process = Start-Process -FilePath $exe -WorkingDirectory $run -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $run 'headset.log') -RedirectStandardError (Join-Path $run 'stderr.log') -PassThru
} finally { $env:AX201_HFP_CODEC = $previousCodec }
$process.Id | Set-Content (Join-Path $run 'pid.txt')
Write-Output "[APP] Started PID=$($process.Id); log: $run\headset.log"
Write-Output '[APP] On iPhone, pair with AX201 HFP Headset.'
