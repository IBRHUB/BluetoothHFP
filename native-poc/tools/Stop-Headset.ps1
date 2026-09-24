[CmdletBinding()]
param([switch]$KeepWinUSB)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$run = Join-Path $repo '.local\hfp-run'
$processes = @(Get-Process ax201_headset -ErrorAction SilentlyContinue)
if ($processes.Count) {
    'quit' | Set-Content -LiteralPath (Join-Path $run 'command.txt') -Encoding ASCII
    foreach ($process in $processes) {
        if (-not $process.WaitForExit(10000)) { throw 'Graceful shutdown timed out; inspect log before changing driver' }
    }
}
if (-not $KeepWinUSB) { & (Join-Path $PSScriptRoot 'Set-BluetoothDriver.ps1') -Mode Intel }
Write-Output '[APP] Headset stopped'
