param([ValidateSet('Enable','Restore','Status')][string]$Action = 'Status')
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$backup = Join-Path $repo 'build/hfp-registration-backup.json'
$path = 'HKLM:\SYSTEM\CurrentControlSet\Control\Bluetooth\Audio\Hfp\HandsFree'
$name = 'BypassRegistration'
$key = Get-Item -LiteralPath $path
if ($Action -eq 'Enable') {
    # Keep the first pre-experiment state across repeated invocations.
    if (!(Test-Path -LiteralPath $backup)) {
        $exists = $key.GetValueNames() -contains $name
        $kind = if ($exists) { $key.GetValueKind($name).ToString() } else { 'DWord' }
        New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
        [pscustomobject]@{
            Path = $path; Name = $name; Existed = $exists
            Kind = $kind; Value = $key.GetValue($name)
            SavedAt = (Get-Date).ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath $backup -Encoding UTF8
    }
    New-ItemProperty -LiteralPath $path -Name $name -PropertyType DWord -Value 1 -Force | Out-Null
} elseif ($Action -eq 'Restore') {
    $saved = Get-Content -LiteralPath $backup -Raw | ConvertFrom-Json
    if ($saved.Path -ne $path -or $saved.Name -ne $name) { throw 'Unexpected backup target.' }
    if ($saved.Existed) {
        New-ItemProperty -LiteralPath $path -Name $name -PropertyType $saved.Kind -Value $saved.Value -Force | Out-Null
    } else {
        if ((Get-Item -LiteralPath $path).GetValueNames() -contains $name) {
            Remove-ItemProperty -LiteralPath $path -Name $name
        }
    }
}
$current = Get-Item -LiteralPath $path
[pscustomobject]@{
    Path = $path; Name = $name
    Exists = $current.GetValueNames() -contains $name
    Value = $current.GetValue($name)
    Backup = $backup
} | Format-List
