# Bluetooth HFP

The independent Windows C++ console experiment for direct AX201 USB ownership
is in [native-poc](native-poc/README.md). It currently verifies USB descriptors
and records the WinUSB driver-ownership blocker; HFP/SCO is not yet established.

A Flutter desktop interface for receiving iPhone media over Bluetooth and routing available iPhone call audio through a Windows PC microphone and output device. Media connection, call transport, and the actual microphone/audio route have separate status messages.

## Run

### Wired audio (iPhone 12 Pro Max / iOS 17)

The app now supports an explicit two-way WASAPI bridge through audio interfaces,
without Bluetooth pairing or an active call. See [WIRED-AUDIO.md](WIRED-AUDIO.md)
for the required Lightning hardware, cabling and phone-side acceptance test.
A charging/data cable directly between the phone and PC does not expose the
audio endpoints this feature requires. Phone-side reception remains unverified.

Select **Wired mode**, keep your PC **Microphone** and **Headphones**,
then select the interface **From phone** capture and **To phone** playback
endpoints. Press **Start**. Stop is explicit; changing an endpoint
stops wired routing, and a missing endpoint stops the bridge without substituting
another device. Reconnect the interface and press Start again. Endpoint choices
are remembered, but transmitting does not resume automatically after relaunch.
Two-channel phone audio preserves left/right channels when headphones expose
at least two channels; the microphone path is mono. Windows default audio output
is not changed. Device-internal routing and the iOS app determine the final path.

### Bluetooth

1. Install Flutter and Visual Studio with the **Desktop development with C++** workload.
2. Pair the iPhone with Windows Bluetooth.
3. Run `flutter build windows --release`, then `powershell -File windows/package/package-windows.ps1 -Register`. Local registration requires Windows Developer Mode. Launch **Bluetooth HFP** from Start or the generated `build/Launch Bluetooth HFP.lnk` so Windows supplies the app's package identity and declared phone capabilities. `flutter run -d windows` remains useful for UI work, but does not supply that identity.
4. Select the paired iPhone. The app opens the Windows A2DP receiver using `AudioPlaybackConnection` and separately requests call transport access using `PhoneLineTransportDevice`. Play media on iPhone and choose this PC as its Bluetooth output. **Media uses Windows audio output**, so select your headphones in **Sound settings**. The app's Input/Output selectors apply to the WASAPI call bridge.
5. Enable **Settings > Privacy & security > Phone calls**. If Windows denies transport access even with the installed package and permissions enabled, set up iPhone calling in **Phone Link**. This is an actual OS/API limitation; the app reports it instead of claiming that the microphone is working.
6. Select the PC microphone and call output, then start/transfer a call on iPhone and choose the PC. During the call, open iPhone **Control Center > app controls > Audio Input / Input** and select the PC if offered. Apple documents this selector in its [iPhone guide](https://support.apple.com/guide/iphone/iph8dc8a5c3c/ios). Repeat in the recording app when testing voice messages; available inputs depend on the app and active Bluetooth profile. Call routing turns green only after capture delivers packets and playback accepts more than its initial buffer in both directions. This verifies Windows stream progress, not remote reception. A stream that stops progressing for three seconds is closed and retried automatically. Test with a real call.
7. **Reconnect** retries media and call connection. **Stop routing** closes this app's media connection and audio streams and removes only call registrations created by this instance. It does not unpair the phone or remove Windows drivers.

Use **Test mic through headphones (5 seconds)** to test the selected microphone and output without a phone call. Speak and listen for your own voice; the meter retains the maximum captured level. This test does not record to disk or send audio to the phone. A microphone signal confirms local capture, not iPhone call support. The test is disabled while the call bridge is active. Successful Input/Output choices are remembered per Windows user (`HKCU\Software\BluetoothHFP`) instead of reverting to virtual communications defaults on every launch.

The script also builds `build/BluetoothHFP.msix`. This distribution artifact is unsigned; sign it before distributing it. For testing on this machine use the registered app, not a double-click on the unsigned MSIX. Do not move/delete `build/package` while its loose development package is registered. Unregister it with `Get-AppxPackage BluetoothHFP.Desktop | Remove-AppxPackage` when no longer needed.

## How it works

### Experimental microphone without a call

Select the iPhone and PC Input, then enable **Bluetooth microphone without a call**. Start recording in the iPhone app and select the PC in Audio Input if offered. This mode opens only PC capture → iPhone HF render; it does not require a PC output or phone capture stream. An idle/failed phone downlink therefore cannot stop the uplink. It does not dial, transfer a call, invoke Siri, or record a file locally. Disable the switch to restore two-way call routing; Stop routing also clears the mode.

This is an experiment using the existing Windows HFP endpoint, not a separate Bluetooth stack. It does not make a missing iPhone input appear or resolve a blocked call transport. The three-second progress watchdog and automatic retries remain enabled. A progressing Windows buffer is not proof of iPhone reception. Verify a recording by speaking, physically muting the PC mic, speaking again, and playing back the result. If the PC is absent from Audio Input or the buffer stalls, this mode has not solved the device/app limitation. End-to-end recording without a call remains unverified.

For a local diagnostic report of this mode, close the app and run `powershell -File windows/package/diagnose-windows.ps1 -PhoneAddress <12-hex-digit-address> -VoiceMode` after registering the updated build. This sends the selected PC microphone to the exposed Bluetooth endpoint for the 45-second observation window; start the iPhone recording yourself. The report includes `voiceMode`, `microphoneFrames`, and the stream status, but contains no recorded audio. Do not combine this option with `-TransferActiveCall`.

Apple requires the recording app to allow [Bluetooth HFP input](https://developer.apple.com/documentation/avfaudio/avaudiosession/categoryoptions-swift.struct/allowbluetoothhfp). A separate Windows HFP implementation would also need access to SCO through an appropriate [Bluetooth profile driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/using-the-bluetooth-driver-stack); sending PCM over RFCOMM is insufficient.

For the local Windows HFP registration bypass experiment, its observed results, and rollback instructions, see [HFP-EXPERIMENT.md](HFP-EXPERIMENT.md). The registry change is experimental and machine-wide; call-transport connection success does not prove microphone delivery.

**Use PC for active call** enumerates Windows phone lines, matches the selected phone's transport ID, and requests `PhoneCall.ChangeAudioDeviceAsync(LocalDevice)` for a talking call. It does not dial, answer, or unmute. Windows must expose both the phone line and the call to this app. A voice message is not a phone call and cannot be transferred with this API. Failures and the absence of a matching line/call are reported in the call status text.

Building this feature requires a Windows 11 build host (or Windows 20348+ metadata supplied via the CMake `HFP_METADATA` path) and the SDK `cppwinrt.exe` tool. CMake generates WinRT projections into the build directory; the installed 19041 SDK alone lacks the newer call-transfer declarations. The app checks runtime availability before requesting transfer.

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

The window owns its controller only between `OnCreate` and `OnDestroy`. This matters because `Win32Window::Create` invokes `Destroy` before the first window exists; stopping a controller constructed as a direct window member at that point previously killed the Bluetooth worker before the user could connect.

Run the opt-in Windows integration test with one paired test phone:

```powershell
flutter test integration_test/device_flow_test.dart -d windows --dart-define=HFP_HARDWARE_TEST=true --dart-define=HFP_TEST_INPUT=HyperX --dart-define=HFP_TEST_OUTPUT=FiiO
```

Replace the device-name fragments for your hardware. The test launches the real window, selects the physical devices, opens a five-second local microphone-to-output stream, checks that it closes, and verifies that Bluetooth progresses beyond the initial connecting state. It does not dial or answer a call. On the development machine the local test captured a nonzero HyperX signal and opened the FiiO render stream; media also connected. Call transport access remained denied by Windows. Hearing the output and completing a phone call still require a human check.

`powershell -File windows/package/diagnose-windows.ps1` checks the registered app's package identity and current devices. Add `-PhoneAddress <12-hex-digit-address>` to attempt a connection for 45 seconds without dialing or answering. The local report is `build/connection-diagnostics.txt`; media success and denied call access are recorded separately. The hardware smoke test on the development machine opened media successfully, but Windows returned denied call access. This is not an end-to-end call-audio pass.

API references: [Bluetooth media reception](https://learn.microsoft.com/en-us/windows/apps/develop/media-playback/enable-remote-audio-playback), [PhoneLineTransportDevice](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.calls.phonelinetransportdevice), and [call access capability](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.calls.phonelinetransportdevice.requestaccessasync).
