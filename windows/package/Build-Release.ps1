[CmdletBinding()]
param([string]$FlutterRoot, [switch]$SkipBuild, [switch]$StageOnly)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Push-Location $repo
try {
    if (!$SkipBuild) {
        & (Join-Path $repo 'native-poc\tools\Build.ps1')
        & (Join-Path $repo 'native-poc\tools\Build-Headset.ps1')
        $flutter = if ($FlutterRoot) { Join-Path $FlutterRoot 'bin\flutter.bat' } else { (Get-Command flutter -ErrorAction Stop).Source }
        & $flutter analyze
        if ($LASTEXITCODE) { throw 'Flutter analysis failed' }
        & $flutter test
        if ($LASTEXITCODE) { throw 'Flutter tests failed' }
        # Ship the SDK's complete official icon font. Incremental subset assets
        # previously omitted the Audio and Settings glyphs in release builds.
        & $flutter build windows --release --no-tree-shake-icons
        if ($LASTEXITCODE) { throw 'Flutter build failed' }
    }
    $stage = Join-Path $repo 'build\release-stage'
    # This exact generated staging directory is disposable; backups never live here.
    if ([IO.Path]::GetFullPath($stage) -ne (Join-Path $repo 'build\release-stage')) { throw 'Invalid staging path' }
    if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item (Join-Path $repo 'build\windows\x64\runner\Release\*') $stage -Recurse -Force
    $engine = Join-Path $stage 'engine'
    $tools = Join-Path $engine 'tools'
    $licenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force $engine,$tools,$licenses | Out-Null
    Copy-Item (Join-Path $repo 'build\ax201-headset\Release\ax201_headset.exe') $engine
    foreach ($name in @('ax201_probe.exe','ax201_driver.exe')) { Copy-Item (Join-Path $repo "build\ax201-poc\Release\$name") $engine }
    foreach ($name in @('Controller.ps1','Uninstall.ps1')) { Copy-Item (Join-Path $PSScriptRoot $name) $tools }
    foreach ($name in @('Backup-Driver.ps1','Restore-Driver.ps1')) { Copy-Item (Join-Path $repo "native-poc\tools\$name") $tools }
    Copy-Item (Join-Path $repo 'native-poc\THIRD-PARTY.md') $licenses
    Copy-Item (Join-Path $repo 'README.md') $stage
    # Ship corresponding dependency sources and all upstream notices with the binary.
    foreach ($dependency in @('btstack','fdk-aac')) {
        & git -C (Join-Path $repo "build\ax201-research\$dependency") archive --format=zip "--output=$(Join-Path $licenses "$dependency-source.zip")" HEAD
        if ($LASTEXITCODE) { throw 'Dependency source archive failed' }
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory | Sort-Object Name -Descending | ForEach-Object {
        Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue
    } | Select-Object -First 1
    if (!$crt) { throw 'MSVC runtime redistribution folder not found' }
    Get-ChildItem $crt.FullName -Filter '*.dll' | ForEach-Object { Copy-Item $_.FullName $stage; Copy-Item $_.FullName $engine }
    foreach ($required in @('bluetooth_hfp.exe','flutter_windows.dll','engine\ax201_headset.exe','engine\ax201_probe.exe','engine\ax201_driver.exe','vcruntime140.dll')) {
        if (!(Test-Path (Join-Path $stage $required))) { throw "Missing release file: $required" }
    }
    if ($StageOnly) { Write-Output "[RELEASE] Staged at $stage"; return }
    $output = Join-Path $repo 'build\release'
    New-Item -ItemType Directory -Force $output | Out-Null
    $zip = Join-Path $output 'BluetoothHFP.zip'
    if (Test-Path $zip) { Remove-Item -LiteralPath $zip }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
    (Get-FileHash $zip).Hash | Set-Content (Join-Path $output 'payload.sha256') -Encoding ASCII
    Copy-Item (Join-Path $PSScriptRoot 'Install.ps1') $output -Force
    $setup = Join-Path $output 'BluetoothHFP-2.0.0-Setup.exe'
    # Windows' built-in IExpress packages the verified payload and installer.
    $sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=Install Bluetooth HFP 2.0? Administrator approval will be requested. Bluetooth drivers are changed only from the app.
DisplayLicense=
FinishMessage=
TargetName=$setup
FriendlyName=Bluetooth HFP 2.0 Setup
AppLaunched=powershell.exe -NoProfile -ExecutionPolicy Bypass -File Install.ps1
PostInstallCmd=<None>
AdminQuietInstCmd=powershell.exe -NoProfile -ExecutionPolicy Bypass -File Install.ps1 -Silent
UserQuietInstCmd=powershell.exe -NoProfile -ExecutionPolicy Bypass -File Install.ps1 -Silent
SourceFiles=SourceFiles
[SourceFiles]
SourceFiles0=$output\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
[Strings]
FILE0="Install.ps1"
FILE1="BluetoothHFP.zip"
FILE2="payload.sha256"
"@
    $sedPath = Join-Path $output 'setup.sed'
    $sed | Set-Content $sedPath -Encoding ASCII
    $builder = Start-Process -FilePath (Join-Path $env:windir 'System32\iexpress.exe') -ArgumentList @('/N','/Q','setup.sed') -WorkingDirectory $output -WindowStyle Hidden -PassThru -Wait
    if ($builder.ExitCode -ne 0 -or !(Test-Path $setup)) { throw "Installer packaging failed: $($builder.ExitCode)" }
    Get-FileHash $setup -Algorithm SHA256 | Format-List | Out-String | Set-Content (Join-Path $output 'SHA256.txt')
    # Keep one distributable. These inputs are embedded in the setup executable.
    foreach ($temporary in @('BluetoothHFP.zip','payload.sha256','Install.ps1','setup.sed')) {
        Remove-Item -LiteralPath (Join-Path $output $temporary) -Force
    }
    if ([IO.Path]::GetFullPath($stage) -ne (Join-Path $repo 'build\release-stage')) { throw 'Invalid staging path' }
    Remove-Item -LiteralPath $stage -Recurse -Force
    Write-Output "[RELEASE] Installer: $setup"
} finally { Pop-Location }
