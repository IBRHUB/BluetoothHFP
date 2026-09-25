# Bluetooth HFP for Windows 11

Flutter/Dart desktop controller for the native AX201 Bluetooth headset engine.
The internal USB Bluetooth controller is owned by WinUSB while the engine provides
HFP Hands-Free calls, A2DP music and WASAPI microphone/headphone audio.

## Install and use

Run `build/release/BluetoothHFP-2.0.0-Setup.exe`, then open **Bluetooth HFP**
from the Start menu. Installation itself does not replace Bluetooth drivers.
The first **Enable headset** operation exports and verifies the original Intel
driver before taking ownership. Windows asks for administrator permission only
for driver changes; the desktop/audio engine normally runs as the current user.

On iPhone, pair with **AX201 HFP Headset** and select it as the call/media output.
The app can scan, connect, disconnect, forget local pairing, reconnect, choose
Windows audio endpoints, mute/attenuate the microphone, adjust playback volume,
and show negotiated call/media codecs. Endpoint/codec changes require stopping
the headset first. Phone-side iOS pairing approvals and routing stay on iPhone.

Use **Restore Windows Bluetooth** in the app to return the controller to Windows.
There is also a Start-menu recovery shortcut that works without the Flutter UI.
Closing the app offers either restoration or leaving the controller in headset
mode with the engine stopped. Bluetooth peripherals using the same controller
are unavailable while the headset owns it. Wi-Fi is not rebound.

## Compatibility and tested quality

- Windows 11 x64, Intel Bluetooth USB `8087:0026` (AX201), one controller at a time.
- Reference hardware: iPhone 12 Pro Max, HyperX QuadCast S, FiiO K11.
- Verified calls: CVSD 8 kHz and mSBC 16 kHz, microphone and playback.
- Verified media: AAC-LC stereo 44.1 kHz / 256 kbit/s VBR; SBC fallback.
- LC3-SWB 32 kHz is offered and passes a local codec test; the reference iPhone
  chose mSBC. No LC3 phone interoperability claim.
- Other USB identities are blocked before driver changes, not labelled supported.
  Multiple matching controllers are rejected to avoid ambiguous ownership.
- Reconnect and suspend/resume handling are implemented; a real cold power cycle,
  long-duration stress and additional controller/phone combinations still need
  physical validation. Resident firmware or original Intel driver bootstrap is
  used; no native SFI/DDC firmware uploader is claimed.

## Data, updates and removal

- `%LOCALAPPDATA%\BluetoothHFP`: settings, private pairing keys and local runtime.
- `%ProgramData%\BluetoothHFP\backups`: original driver exports, hashes and recovery
  instructions. Only Administrators/SYSTEM can write this directory.
- Updates preserve both locations. The installer verifies its payload SHA256.
- Uninstall from Windows Installed Apps restores Bluetooth first. A recovery
  failure stops removal and retains the app. If the managed controller is absent,
  reconnect it before uninstalling. Settings and backups are intentionally retained.
- Diagnostic exports contain bounded text logs and status, not pairing-key files,
  packet dumps, or recorded audio. Exported Bluetooth addresses and device instance
  identities are redacted. The engine does not create raw HCI packet captures.

This is an unsigned personal-use build. See `native-poc/THIRD-PARTY.md` and the
bundled `licenses/` sources/notices before redistribution or commercial use.
No signing certificate, test-signing mode, or Secure Boot change is installed.

## Build

Visual Studio 2022 C++ Build Tools + Windows SDK, Flutter SDK matching pubspec,
Git and Windows PowerShell are required. No external installer compiler is needed.

```powershell
.\windows\package\Build-Release.ps1 -FlutterRoot C:\path\to\flutter
```

This builds and tests native code, analyzes/tests Flutter, builds the desktop,
packages dependencies and uses Windows IExpress for the setup executable.
`-SkipBuild -StageOnly` stages existing binaries; `-SkipBuild` packages them.
The final `build/release/` folder contains one setup executable and `SHA256.txt`.
Packaging inputs and the staging copy are removed after successful packaging.
Installer source remains in `windows/package/`. Native and Flutter build caches
remain available for incremental builds; they are not separate product releases.
The installer can be exercised without prompts using
`build\release\BluetoothHFP-2.0.0-Setup.exe /Q:A` from an elevated shell.

## Source layout

- `lib/`: Dart application and lifecycle controller.
- `windows/runner/`: small platform bridge for audio inventory, elevation, window lifecycle.
- `windows/package/`: verified installer, uninstall/recovery scripts, release builder.
- `native-poc/app/`: native engine entry point and versioned IPC.
- `native-poc/bluetooth/`: USB/HCI, HFP, A2DP and codecs.
- `native-poc/audio/`: WASAPI, bounded rings and high-quality drift interpolation.
- `native-poc/tests/`, `test/`, `integration_test/`: native, Dart and opt-in hardware tests.
- `native-poc/evidence/`: hardware acceptance snapshots, excluding private link keys.

Old WinRT Audio Gateway/Phone Link/wired audio experiments and MSIX registration
code have been removed from the active project. Original research/probe/recovery
tools remain for hardware diagnosis; they are not the desktop control path.
Retired local build artifacts and experiment notes are archived under
`.local/release-reference/retired-builds/`, excluded from version control and
release packaging. Original driver backups are kept separately under
`.local/ax201-backup/` and the protected ProgramData recovery folder.

See `docs/IPC.md` and `docs/RELEASE-VALIDATION.md` for protocol and validation details.
