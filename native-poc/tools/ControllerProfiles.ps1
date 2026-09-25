# Shared, read-only compatibility policy. Dot-sourcing performs no device operations.
function Get-ControllerProfiles {
    $catalog = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'controller-profiles.json') -Raw | ConvertFrom-Json
    if ($catalog.schemaVersion -ne 1) { throw 'Unsupported controller catalog version' }
    $identities = @{}
    $names = @{}
    foreach ($profile in $catalog.profiles) {
        if ($profile.id -notmatch '^[a-z0-9-]+$' -or $names.ContainsKey($profile.id) -or
            $profile.vid -cnotmatch '^[0-9A-F]{4}$' -or $profile.pid -cnotmatch '^[0-9A-F]{4}$' -or
            $profile.backend -notin @('intel-legacy') -or
            $profile.validation -notin @('reference-tested','experimental') -or
            $profile.originalService -notmatch '^[A-Za-z0-9_]+$' -or
            $profile.legacyInfSection -notmatch '^[A-Za-z0-9_.-]+$' -or
            [string]::IsNullOrWhiteSpace($profile.originalProvider)) { throw 'Invalid or unimplemented controller profile' }
        $key = "$($profile.vid):$($profile.pid)"
        if ($identities.ContainsKey($key)) { throw 'Ambiguous controller profiles' }
        $identities[$key] = $true
        $names[$profile.id] = $true
        $profile
    }
}

function Find-ControllerProfile([string]$InstanceId, [object[]]$Profiles = @(Get-ControllerProfiles)) {
    # Exact physical USB identity; never match composite child interfaces or a partial PID.
    if ($InstanceId -notmatch '^USB\\VID_([0-9A-F]{4})&PID_([0-9A-F]{4})\\[^\\]+$') { return $null }
    $vendorId = $Matches[1]; $productId = $Matches[2]
    $Profiles | Where-Object { $_.vid -eq $vendorId -and $_.pid -eq $productId } | Select-Object -First 1
}

function Get-ControllerAssessment([object[]]$Devices) {
    $items = @($Devices | ForEach-Object {
        $profile = Find-ControllerProfile $_.id
        $vendorId = ''; $productId = ''
        if ($_.id -match '^USB\\VID_([0-9A-F]{4})&PID_([0-9A-F]{4})') { $vendorId = $Matches[1].ToUpperInvariant(); $productId = $Matches[2].ToUpperInvariant() }
        [pscustomobject][ordered]@{
            id=$_.id; name=$_.name; status=$_.status; service=$_.service; inf=$_.inf
            vid=$vendorId; pid=$productId; driverVersion=$_.driverVersion
            profile=$(if ($profile) { $profile.id } else { '' })
            support=$(if ($profile) { $profile.validation } else { 'unsupported' })
            reason=$(if ($profile) { $profile.evidence } else { 'No implemented and validated controller backend; driver changes blocked.' })
            eligible=($null -ne $profile -and $profile.validation -eq 'reference-tested')
        }
    })
    $eligible = @($items | Where-Object eligible)
    $selected = if ($eligible.Count -eq 1) { $eligible[0].id } else { '' }
    [ordered]@{
        devices=$items; selectedId=$selected; supported=($eligible.Count -eq 1)
        reason=$(if ($eligible.Count -eq 1) { $eligible[0].reason }
            elseif ($eligible.Count -gt 1) { 'Multiple supported controllers found; disconnect extra supported controllers before switching.' }
            elseif ($items.Count) { 'Detected controllers are unsupported or experimental; no driver will be changed.' }
            else { 'No Bluetooth controller detected; no driver will be changed.' })
    }
}

function Test-PhysicalBluetoothController($Device, [object[]]$Profiles = @(Get-ControllerProfiles)) {
    return (($Device.Class -eq 'Bluetooth' -and $Device.InstanceId -match '^(USB|PCI|ACPI|UART)\\') -or
        ($null -ne (Find-ControllerProfile $Device.InstanceId $Profiles)))
}

function Get-ControllerInventory {
    $profiles = @(Get-ControllerProfiles)
    $rows = @(Get-PnpDevice -PresentOnly | Where-Object {
        # Include physical Bluetooth radios and our managed WinUSB identity, not paired peripherals.
        Test-PhysicalBluetoothController $_ $profiles
    } | ForEach-Object {
        $properties = @(Get-PnpDeviceProperty -InstanceId $_.InstanceId -ErrorAction SilentlyContinue)
        [pscustomobject]@{
            id=$_.InstanceId; name=$_.FriendlyName; status=$_.Status
            service=($properties | Where-Object KeyName -eq 'DEVPKEY_Device_Service').Data
            inf=($properties | Where-Object KeyName -eq 'DEVPKEY_Device_DriverInfPath').Data
            driverVersion=($properties | Where-Object KeyName -eq 'DEVPKEY_Device_DriverVersion').Data
        }
    })
    Get-ControllerAssessment $rows
}

function Assert-ControllerSelection($Inventory, [string]$ExpectedInstanceId) {
    if (!$Inventory.supported) { throw $Inventory.reason }
    if ($ExpectedInstanceId -and $Inventory.selectedId -ne $ExpectedInstanceId) {
        throw 'Selected controller changed or was disconnected. No driver changed; refresh the device list.'
    }
}

function Get-RecoverySection($Snapshot, $Profile) {
    if ($Snapshot.SchemaVersion -and $Snapshot.SchemaVersion -notin @(1,2)) { throw 'Unsupported recovery metadata version' }
    if (!$Profile -or $Snapshot.InstanceId -notmatch '^USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}\\[^\\]+$' -or
        (Find-ControllerProfile $Snapshot.InstanceId).id -ne $Profile.id -or
        $Snapshot.Service -ne $Profile.originalService -or $Snapshot.DriverProvider -ne $Profile.originalProvider -or
        $Snapshot.Inf -notmatch '^oem\d+\.inf$') { throw 'Original driver backup does not match the controller profile' }
    if ($Snapshot.ProfileId -and $Snapshot.ProfileId -ne $Profile.id) { throw 'Backup profile mismatch' }
    # Old backups retain the tested legacy fallback. New backups record the actual installed section.
    if ($Snapshot.SchemaVersion -eq 2 -and !$Snapshot.DriverInfSection) { throw 'Recovery section missing from version 2 backup' }
    $section = if ($Snapshot.DriverInfSection) { $Snapshot.DriverInfSection } else { $Profile.legacyInfSection }
    if ($section -notmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid original driver section' }
    return $section
}

function Read-ControllerBackup([string]$Directory, [string]$InstanceId) {
    $root = (Resolve-Path -LiteralPath $Directory).Path
    $snapshot = Get-Content -LiteralPath (Join-Path $root 'snapshot.json') -Raw | ConvertFrom-Json
    if ($InstanceId -and $snapshot.InstanceId -ne $InstanceId) { throw 'Original driver backup does not match this device' }
    $null = Get-RecoverySection $snapshot (Find-ControllerProfile $snapshot.InstanceId)
    $hashes = Get-Content -LiteralPath (Join-Path $root 'hashes.json') -Raw | ConvertFrom-Json
    if (!$hashes -or @($hashes).Count -eq 0) { throw 'Empty backup hash manifest' }
    if ($snapshot.SchemaVersion -eq 2 -and
        (!(@($hashes.RelativePath) -contains 'snapshot.json') -or !(@($hashes.RelativePath) -contains $snapshot.Inf))) {
        throw 'Recovery metadata and original INF must be covered by backup hashes'
    }
    foreach ($entry in $hashes) {
        $file = [IO.Path]::GetFullPath((Join-Path $root $entry.RelativePath))
        if (!$file.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
            (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.SHA256) { throw 'Driver backup integrity check failed' }
    }
    return $snapshot
}
