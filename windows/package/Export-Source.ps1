[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = Join-Path $repo 'build\share'
New-Item -ItemType Directory -Force $output | Out-Null
$zip = Join-Path $output 'BluetoothHFP-Source.zip'
# Explicit source allowlist: never copy the workspace or Git history.
$roots = @('lib','test','integration_test','docs','native-poc','windows\runner','windows\package')
$files = @($roots | ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $repo $_) -File -Recurse })
foreach ($name in @('README.md','.gitignore','.gitattributes','.metadata','analysis_options.yaml','pubspec.yaml','pubspec.lock','windows\CMakeLists.txt','windows\.gitignore','windows\flutter\CMakeLists.txt','windows\flutter\generated_plugin_registrant.cc','windows\flutter\generated_plugin_registrant.h','windows\flutter\generated_plugins.cmake')) {
    $files += Get-Item -LiteralPath (Join-Path $repo $name)
}
$allowed = @('.dart','.md','.txt','.json','.c','.cc','.cpp','.h','.cmake','.ps1','.yaml','.lock','.rc','.manifest','.ico','.gitignore','.gitattributes','.metadata')
foreach ($file in $files) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Source export refuses reparse points' }
    if ($file.Extension -notin $allowed -and $file.Name -notin @('.gitignore','.gitattributes','.metadata')) { throw "Unexpected source file: $($file.Name)" }
    if ($file.Extension -eq '.ico') { continue }
    $content = [IO.File]::ReadAllText($file.FullName)
    foreach ($match in [regex]::Matches($content,'(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b')) {
        if ($match.Value -notmatch '^(02:00:00:00:00:0[12]|AA:BB:CC:DD:EE:FF)$') { throw "Non-example Bluetooth address in $($file.Name)" }
    }
    if ($content -match '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----') { throw 'Private key in source export' }
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$archive = [IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in ($files | Sort-Object FullName -Unique)) {
        $relative = $file.FullName.Substring($repo.Length + 1).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
Write-Output "[SHARE] Source-only archive: $zip"
