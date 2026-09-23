# Bluetooth HFP

A minimal Flutter desktop interface for routing an iPhone call through a Windows PC microphone and output device. The UI has three selectors: Bluetooth, Input, and Output.

## Run

1. Install Flutter and Visual Studio with the **Desktop development with C++** workload.
2. Pair the iPhone with Windows Bluetooth. Enable calling support on the PC so Windows creates the iPhone **Hands-Free HF Audio** endpoints during a call. On many Windows 11 systems, this requires setting up the iPhone in **Phone Link** and transferring the call to the PC.
3. Run `flutter run -d windows`.
4. Select the paired iPhone, PC microphone, and PC headphones or speakers. Start or transfer a call on the iPhone. The status turns green only after both WASAPI directions open. Use **Stop routing** in the Bluetooth menu to close this app's audio streams.

## How it works

Flutter/Dart owns the interface and state changes. The small C++ Windows runner uses Bluetooth APIs to enumerate paired phones and read their connection status. It uses Windows Core Audio to find the iPhone's hands-free capture and render endpoints, then opens two WASAPI shared-mode streams:

```text
iPhone HF Audio capture  →  selected PC output
selected PC microphone   →  iPhone HF Audio render
```

Windows handles HFP and SCO transport. Selecting a phone in this app does not establish a Bluetooth radio connection; **Stop routing** closes this app's streams without removing the phone's HFP driver. The app does not implement an independent Bluetooth HFP stack and cannot make Windows expose phone call endpoints when its Bluetooth driver or phone integration does not provide them. Pairing alone is not proof that a call audio path exists. The status line reports when the phone is paired but the call endpoints are unavailable.

The two streams use the endpoint mix formats and convert between PCM and float with a bounded mono buffer and linear sample-rate conversion. This keeps the app self-contained but does not provide echo cancellation; headphones are recommended.

Relevant Windows documentation: [Phone Link iPhone setup](https://support.microsoft.com/en-us/windows/apps/phonelink/phone-link-requirements-and-setup), [Windows HFP accessory roles](https://learn.microsoft.com/en-us/windows-hardware/design/accessory-guidelines/bluetooth-accessory-guidelines/bluetooth-accessory-guidelines-classic-audio), [Bluetooth service state](https://learn.microsoft.com/en-us/windows/win32/api/bluetoothapis/nf-bluetoothapis-bluetoothsetservicestate), and [WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi).

## Verification

Run `flutter analyze`, `flutter test`, and `flutter build windows`. A physical iPhone call is required to verify the Bluetooth HFP and SCO path.
