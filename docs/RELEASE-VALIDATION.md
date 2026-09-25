# Release 2.0 validation

## Automated checks

On 2026-09-24 the installed release passed the desktop smoke cycle:
`firstReady=true`, `restored=true`, `secondReady=true`, `error=""`.
Both processes ran from `C:\Program Files\BluetoothHFP`; Wi-Fi remained Up.
See `native-poc/evidence/desktop-lifecycle.json`.

The generated Setup EXE was executed, and hashes of installed desktop/engine
binaries matched the release payload. Removal restored the Intel driver and
retained the exact pairing-key file. Reinstallation and an in-place update passed.
A deliberately corrupt payload was rejected before changing the installed EXE.
Installation/uninstallation guards reject running app/engine processes.
Terminating the desktop caused the engine to exit through broken-stdin shutdown.
See `native-poc/evidence/installer-acceptance.txt`.

The desktop's RTL interface was inspected on screen. This Flutter build emits
Windows accessibility-tree diagnostics in stderr; ordinary rendering and control
work, but screen-reader accessibility is not certified.

- Flutter analysis and unit/widget tests: address validation, structured codec
  events, disconnect state cleanup, overlapping settings saves, and blocking
  unsupported adapters in the UI.
- Native tests: USB descriptor validation, bounded mono/stereo audio rings,
  500 ppm clock drift and sinc response, LC3 encode/decode plus lost-frame path.
- PowerShell parser checks for all installer/driver lifecycle scripts.

The optional `bluetooth_hfp.exe --smoke` test uses the actual Dart controller,
platform channel, privileged driver helper, WinUSB/HCI engine and IPC. It starts
the engine, scans, stops, restores Windows Bluetooth, and starts the engine again.
The report is `%LOCALAPPDATA%\BluetoothHFP\desktop-smoke.json`. It changes the
selected controller's mode and must only be used on a backed-up test device.

## Prior hardware acceptance

See `native-poc/evidence/ACCEPTANCE.md` and `quality-success.txt`: actual pairing,
HFP, mSBC bidirectional calls, AAC media and media/call transitions were confirmed
on iPhone 12 Pro Max. Those logs validate the audio engine; they do not by themselves
validate every new desktop control or installer lifecycle.

## Limits

The release is unsigned and intended for the tested AX201 personal-use setup.
More controllers, Windows builds and phones require a new physical acceptance
matrix. Real suspend/resume, cold firmware bootstrap, multiday calls and device
removal under load are not certified. These limits are visible in the README;
no unsupported USB device is rebound merely because it is Intel branded.
