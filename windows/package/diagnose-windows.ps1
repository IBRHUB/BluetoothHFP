param([ValidatePattern('^[0-9A-Fa-f]{12}$')][string]$PhoneAddress)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$package = Get-AppxPackage -Name BluetoothHFP.Desktop
if (!$package) { throw 'Register the package first: ./windows/package/package-windows.ps1 -Register' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BluetoothHfpActivation {
    [ComImport, Guid("2e941141-7f97-4756-ba1d-9decde894a3d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IActivation {
        void ActivateApplication([MarshalAs(UnmanagedType.LPWStr)] string app,
            [MarshalAs(UnmanagedType.LPWStr)] string args, uint options, out uint process);
    }
    public static uint Start(string app, string args) {
        object instance = Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("45BA127D-10A8-46EA-8AB7-56EA9078943C")));
        try { uint process; ((IActivation)instance).ActivateApplication(app, args, 0, out process); return process; }
        finally { Marshal.ReleaseComObject(instance); }
    }
}
'@
$report = Join-Path $repo 'build/connection-diagnostics.txt'
$arguments = '--diagnose "' + $report + '"'
if ($PhoneAddress) { $arguments += ' ' + $PhoneAddress }
$appProcess = [BluetoothHfpActivation]::Start(($package.PackageFamilyName + '!App'), $arguments)
$process = Get-Process -Id $appProcess -ErrorAction SilentlyContinue
if ($process) { $process.WaitForExit() }
Get-Content -LiteralPath $report
