# Bluetooth HFP

A Flutter desktop interface for receiving iPhone media over Bluetooth and routing available iPhone call audio through a Windows PC microphone and output device. Media connection, call transport, and the actual microphone/audio route have separate status messages.

## Run

1. Install Flutter and Visual Studio with the **Desktop development with C++** workload.
2. Pair the iPhone with Windows Bluetooth.
3. Run `flutter build windows --release`, then `powershell -File windows/package/package-windows.ps1 -Register`. Local registration requires Windows Developer Mode. Launch **Bluetooth HFP** from Start or the generated `build/Launch Bluetooth HFP.lnk` so Windows supplies the app's package identity and declared phone capabilities. `flutter run -d windows` remains useful for UI work, but does not supply that identity.
4. Select the paired iPhone. The app opens the Windows A2DP receiver using `AudioPlaybackConnection` and separately requests call transport access using `PhoneLineTransportDevice`. Play media on iPhone and choose this PC as its Bluetooth output. **Media uses Windows audio output**, so select your headphones in **Sound settings**. The app's Input/Output selectors apply to the WASAPI call bridge.
5. Enable **Settings > Privacy & security > Phone calls**. If Windows denies transport access even with the installed package and permissions enabled, set up iPhone calling in **Phone Link**. This is an actual OS/API limitation; the app reports it instead of claiming that the microphone is working.
6. Select the PC microphone and call output, then start/transfer a call on iPhone and choose the PC. Call routing turns green only after both WASAPI directions open. That verifies stream initialization, not that the remote participant hears audio. Test both directions with a real call.
7. **Reconnect** retries media and call connection. **Stop routing** closes this app's media connection and audio streams and removes only call registrations created by this instance. It does not unpair the phone or remove Windows drivers.

The script also builds `build/BluetoothHFP.msix`. This distribution artifact is unsigned; sign it before distributing it. For testing on this machine use the registered app, not a double-click on the unsigned MSIX. Do not move/delete `build/package` while its loose development package is registered. Unregister it with `Get-AppxPackage BluetoothHFP.Desktop | Remove-AppxPackage` when no longer needed.

## How it works

Flutter/Dart owns the interface. The C++ runner enumerates paired phones and audio endpoints. A cancellable MTA worker matches WinRT device interfaces by Bluetooth address/container identity, opens media reception, requests call access, registers the app if permitted, and attempts call transport connection. The call bridge opens two WASAPI shared-mode streams once the phone's hands-free capture/render endpoints are available:

```text
iPhone HF Audio capture  →  selected PC output
selected PC microphone   →  iPhone HF Audio render
```

Windows handles HFP and SCO transport. Selecting a phone now requests media and call connections through Windows; the app does not implement an independent HFP stack or bypass a denied permission. Pairing or a successful call transport request alone is not proof of an active call audio path. Endpoint discovery currently uses Windows friendly-name conventions for HF Audio; drivers using different/localized names may need additional matching support. Audio initialization failures retry at ten-second intervals. Failure of either call stream stops the other stream too.

Media uses A2DP; calls use HFP. A2DP has no microphone path. This app cannot force every iPhone app, voice recorder, or voice message feature to use the PC microphone. Actual app behavior depends on iOS audio routing. The media API owns playback routing, so the Output selector cannot redirect media independently of Windows sound settings. There is no automatic dial/answer/hang-up implementation.

The two streams use the endpoint mix formats and convert between PCM and float with a bounded mono buffer and linear sample-rate conversion. This keeps the app self-contained but does not provide echo cancellation; headphones are recommended.

Relevant Windows documentation: [Phone Link iPhone setup](https://support.microsoft.com/en-us/windows/apps/phonelink/phone-link-requirements-and-setup), [Windows HFP accessory roles](https://learn.microsoft.com/en-us/windows-hardware/design/accessory-guidelines/bluetooth-accessory-guidelines/bluetooth-accessory-guidelines-classic-audio), [Bluetooth service state](https://learn.microsoft.com/en-us/windows/win32/api/bluetoothapis/nf-bluetoothapis-bluetoothsetservicestate), and [WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi).

## Verification

Run `flutter analyze`, `flutter test`, and `flutter build windows`. A physical iPhone call is required to verify the Bluetooth HFP and SCO path.

`powershell -File windows/package/diagnose-windows.ps1` checks the registered app's package identity and current devices. Add `-PhoneAddress <12-hex-digit-address>` to attempt a connection for 45 seconds without dialing or answering. The local report is `build/connection-diagnostics.txt`; media success and denied call access are recorded separately. The hardware smoke test on the development machine opened media successfully, but Windows returned denied call access. This is not an end-to-end call-audio pass.

API references: [Bluetooth media reception](https://learn.microsoft.com/en-us/windows/apps/develop/media-playback/enable-remote-audio-playback), [PhoneLineTransportDevice](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.calls.phonelinetransportdevice), and [call access capability](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.calls.phonelinetransportdevice.requestaccessasync).
