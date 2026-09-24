# AX201 Windows Bluetooth headset PoC

Native C/C++ console project, independent of Flutter. The internal AX201 is
owned directly through WinUSB, with BTstack acting as Hands-Free Unit.

## Verified hardware results

- Intel USB 8087:0026: direct WinUSB access, HCI commands and associated SCO interface.
- iPhone discovery/pairing, remote SDP channel 8, RFCOMM and HFP SLC.
- eSCO RX/TX, CVSD 8 kHz and mSBC 16 kHz; user confirmed two-way call audio in both tests.
- WASAPI: HyperX QuadCast S microphone and FiiO K11 headphones on this machine.
- Original Intel driver rollback and return to WinUSB tested without reboot; Wi-Fi remained Up.
- A2DP Sink plus AVRCP: user confirmed YouTube in PC headphones; log proves SBC
  stereo at 44.1 kHz with decoded PCM delivered to FiiO K11.
- User confirmed media -> call with PC microphone -> media again. The log shows
  A2DP suspension, mSBC SCO with WASAPI at 16 kHz, then A2DP resuming at 44.1 kHz.
- Quality revision: iPhone selected AAC-LC stereo 44.1 kHz, 256 kbit/s VBR;
  calls selected mSBC 16 kHz. User confirmed clear playback and two-way calls.
  RAW capture/playback succeeded; recorded counters showed no queue underruns
  or overruns. LC3-SWB was offered but not selected by the phone.

## Build and run

From the repository root in elevated PowerShell, with Visual Studio 2022 C++
Build Tools, its CMake component and Windows SDK:

```powershell
.\native-poc\tools\Build.ps1
.\native-poc\tools\Build-Headset.ps1
.\native-poc\tools\Start-Headset.ps1
Get-Content .local\hfp-run\headset.log -Wait
```

On iPhone, connect to **AX201 HFP Headset**. For calls select it in the call Audio
menu. For YouTube/music select it as the media output in Control Center. If iOS
cached the earlier HFP-only services, forget the device and pair again once.

Start uses Auto codec negotiation (CVSD, mSBC, optional LC3-SWB);
`Start-Headset.ps1 -Codec mSBC` excludes experimental LC3-SWB, while `-Codec CVSD`
forces the narrowband fallback. A2DP advertises AAC-LC VBR up to 320 kbit/s
and SBC stereo at 44.1 or 48 kHz. The phone selects the actual codec and bitrate;
the first quality test negotiated AAC stereo, 44.1 kHz, 256 kbit/s VBR.
It does not start the
microphone. HFP starts capture only on a successful SCO connection. Call audio
has playback priority; media playback can resume after the call when iOS streams.
Windows default capture/playback endpoints are resolved when each stream starts.

## Stop and restore normal Windows Bluetooth

```powershell
.\native-poc\tools\Stop-Headset.ps1
```

This stops the stack gracefully and restores Intel on the exact USB instance.
Use `-KeepWinUSB` only to stop for rebuilding. Windows Bluetooth peripherals are
unavailable while WinUSB owns the controller. Wi-Fi is a separate PCI device.

Original backup, INF/SYS/CAT, SHA256 manifest and manual ROLLBACK.txt:
`.local/ax201-backup/20260924-053724-026/` (outside builds and Git).

Explicit recovery when the headset is stopped:

```powershell
.\native-poc\tools\Set-BluetoothDriver.ps1 -Mode Intel
.\native-poc\tools\Restore-Driver.ps1 -BackupDirectory .local\ax201-backup\20260924-053724-026 -VerifyOnly
```

The driver wrapper checks backup hashes and candidates before installing, and
attempts recovery if binding fails. It uses signed inbox Microsoft WinUSB and
preserves the Intel package. No custom kernel binary, test-signing, certificate
installation or Secure Boot change is involved. The helper uses a per-device
null-driver transition before binding the chosen driver. Do not delete OEM INFs.

## Audio and firmware implementation

HFP: CVSD uses Intel's controller conversion and PCM16 over USB; mSBC uses BTstack
SBC/H2 encoding and decoding. WASAPI shared mode resamples to the endpoint format.
Separate bounded PCM rings compensate clock drift using a 32-tap windowed-sinc
fractional-delay filter, preserving high-frequency detail better than linear
interpolation. Initial buffering is 40 ms for calls and 80 ms for media,
with bounded backlog and underrun/overrun counters.
A2DP preserves stereo channels through a separate frame-aligned ring and renderer.
WASAPI requests RAW processing and quality shared-mode resampling, with fallback
to default processing if RAW initialization fails. Native endpoint mix formats
and actual Bluetooth PCM rates are logged. RAW disables optional device effects;
it does not increase the Bluetooth codec's bandwidth or remove room noise.

AAC decoding uses pinned FDK AAC. LC3-SWB implements 32 kHz mono with 7.5 ms,
58-byte codec frames and H2 framing, using the bundled Google LC3 implementation.
An advertised codec is not proof of phone support: only a successful audio link
reporting codec=3 and an actual listening test validate LC3-SWB on this iPhone.
mSBC is codec=2 (16 kHz); CVSD is codec=1 (8 kHz). A 48 kHz Windows device format
cannot restore speech frequencies removed by the negotiated call codec.

Observed Intel version: platform 37, variant 13 (HrP), hw revision 0, firmware
variant 23 (operational), firmware revision 4. Matching legacy firmware stem:
ibt-19-0-4. No SFI/DDC upload was needed or performed. If HCI preflight fails, the
start script tries the original Intel driver once to initialize firmware, then
returns to WinUSB and rechecks. This fallback has not been tested across a real
cold power cycle; a native SFI/DDC loader is not implemented.

The transport deliberately selects this machine's exact USB instance:
USB\VID_8087&PID_0026\5&1A60D403&0&14. Porting requires new inventory and backup.
Long calls, suspend/resume and device removal are not exhaustively tested.
The first media run had 811 underrun frames during startup/early playback
(about 18 ms at 44.1 kHz) and no overrun at the recorded point; this is a working
PoC, not a claim of zero jitter under all load conditions.
HFP/A2DP do not override iOS decisions for alarms or every app/system sound.

## Logs and controls

Live log: `.local/hfp-run/headset.log`. Historical success logs are in `evidence/`.
`usb-probe.txt` is the original blocked attempt, superseded by winusb-success.txt.
Local link keys live in `.local/hfp-run/link-keys.tlv`, independently of Windows.
SCO and ACL payloads are filtered from the current packet logger; call/media
payloads are not recorded. Do not publish local key files.

```powershell
'connect 44:F2:1B:19:EC:0F' | Set-Content .local\hfp-run\command.txt -Encoding ASCII
'audio' | Set-Content .local\hfp-run\command.txt -Encoding ASCII
'audio-off' | Set-Content .local\hfp-run\command.txt -Encoding ASCII
```

The iPhone may reject HF-initiated audio when no call is active; selecting the
headset in the call UI worked. The program does not dial or answer automatically.

BTstack is pinned at e38553977a25fb0b55b383c72c289be0975f422c. Generated patches
restrict USB selection and log actual SDP/RFCOMM events. See THIRD-PARTY.md for
licensing and COMPATIBILITY.md for the initial firmware/transport source audit.
