[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source = Join-Path $repo 'build\ax201-research\btstack'
$revision = 'e38553977a25fb0b55b383c72c289be0975f422c'
if (-not (Test-Path (Join-Path $source '.git'))) {
    & git clone https://github.com/bluekitchen/btstack.git $source
    if ($LASTEXITCODE) { throw 'BTstack clone failed' }
    & git -C $source checkout --detach $revision
    if ($LASTEXITCODE) { throw 'BTstack checkout failed' }
}
if ((& git -C $source rev-parse HEAD) -ne $revision) { throw 'Unexpected BTstack revision' }
$aac = Join-Path $repo 'build\ax201-research\fdk-aac'
$aacRevision = '7c83d08002332b2730c845eec3497e6bf585dd28'
if (-not (Test-Path (Join-Path $aac '.git'))) {
    & git clone https://github.com/mstorsjo/fdk-aac.git $aac
    if ($LASTEXITCODE) { throw 'AAC dependency clone failed' }
    & git -C $aac checkout --detach $aacRevision
    if ($LASTEXITCODE) { throw 'AAC dependency checkout failed' }
}
if ((& git -C $aac rev-parse HEAD) -ne $aacRevision) { throw 'Unexpected AAC revision' }
$build = Join-Path $repo 'build\ax201-headset'
New-Item -ItemType Directory -Force $build | Out-Null
$transport = Get-Content (Join-Path $source 'platform\windows\hci_transport_h2_winusb.c') -Raw
$needle = 'static int usb_try_open_device(const char * device_path){'
if (-not $transport.Contains($needle)) { throw 'USB patch anchor changed' }
$replacement = @'
static int usb_try_open_device(const char * device_path){
    /* Require the selected physical USB instance when launched by the app. */
    if (strstr(device_path, "vid_8087&pid_0026#") == NULL) return 0;
    const char * selected = getenv("AX201_USB_INSTANCE");
    if (selected && selected[0]) {
        char match[512];
        if (strlen(selected) + 2 > sizeof(match)) return 0;
        strcpy_s(match, sizeof(match), selected);
        for (char * p = match; *p; ++p) {
            if (*p == '\\') *p = '#';
            else if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
        }
        strcat_s(match, sizeof(match), "#");
        if (strstr(device_path, match) == NULL) return 0;
    }
'@
$transport = $transport.Replace($needle, $replacement)
$transport = "#include <stdlib.h>`n#include <string.h>`n" + $transport
$transport = $transport.Replace('if (!usb_device_handle) goto exit_on_error;', 'if (usb_device_handle == INVALID_HANDLE_VALUE) goto exit_on_error;')
[IO.File]::WriteAllText((Join-Path $build 'hci_transport_h2_winusb.c'), $transport)
$hfp = Get-Content (Join-Path $source 'src\classic\hfp.c') -Raw
$needle = 'hfp_connection->rfcomm_mtu = rfcomm_event_channel_opened_get_max_frame_size(packet);'
if (-not $hfp.Contains($needle)) { throw 'HFP patch anchor changed' }
$hfp = $hfp.Replace($needle, $needle + "`n            printf(`"[HFP] RFCOMM connected\n`");")
$needle = 'case SDP_EVENT_QUERY_COMPLETE:'
$hfp = $hfp.Replace($needle, $needle + "`n            printf(`"[HFP] Remote SDP result: status=%u channel=%u\n`", sdp_event_query_complete_get_status(packet), hfp_connection->rfcomm_channel_nr);")
[IO.File]::WriteAllText((Join-Path $build 'hfp.c'), $hfp)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -Wno-dev -S (Join-Path $repo 'native-poc\btstack') -B $build -G 'Visual Studio 17 2022' -A x64 "-DBTSTACK_ROOT=$($source.Replace('\','/'))" "-DFDK_ROOT=$($aac.Replace('\','/'))"
if ($LASTEXITCODE) { throw 'Headset configure failed' }
& $cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE) { throw 'Headset build failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Headset codec tests failed' }
