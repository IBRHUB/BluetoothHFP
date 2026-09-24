# Compatibility audit and subsequent gates

**Historical audit, updated after hardware testing:** The ownership blocker
below was resolved with the signed inbox WinUSB package and an exact-instance
null-driver transition. USB, HCI, iPhone pairing, remote SDP, RFCOMM, SLC, eSCO,
CVSD and mSBC/WASAPI two-way audio have now passed. The user confirmed both
call-audio tests. Intel driver rollback and return to WinUSB were also verified.
A2DP SBC stereo at 44.1 kHz was subsequently added and the user confirmed YouTube
playback through PC headphones; AVRCP connects on the same controller.
See README.md and evidence/ for current state. Cold SFI loading remains
unimplemented; resident firmware was operational on this unit.

## Transports

| Candidate | Assessment |
|---|---|
| WinUSB | Windows supports control/bulk/interrupt and isochronous transfers. Actual open currently fails at WinUsb_Initialize, error 50. |
| libusb with WinUSB backend | Cannot bypass the installed driver. It is another user-mode API, not an ownership solution. No libusb device-open test was run. |
| BTstack windows-winusb | Contains USB HCI and SCO code already. The measured interfaces match its interface 0 / associated interface 1 approach. Actual transfers remain untested. |
| BTstack windows-winusb-intel | Adds Intel initialization. Its README names 8260/8265; inspect code and controller replies before assuming AX201 support. |
| Custom transport | Can enforce exact instance selection and Intel bootloader packet routing; still requires a suitable kernel driver binding. A custom kernel driver is not necessary merely to obtain ISO support. |

Microsoft documents [WinUSB access](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/using-winusb-api-to-communicate-with-a-usb-device),
[installation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/automatic-installation-of-winusb),
and [isochronous transfers starting with Windows 8.1](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/getting-set-up-to-use-windows-devices-usb).
The [libusb Windows notes](https://github.com/libusb/libusb/wiki/Windows) describe its driver backends.

## BTstack source audit

Inspected revision: `e38553977a25fb0b55b383c72c289be0975f422c`.
Local research checkout: `build/ax201-research/btstack` (not a build dependency).

In [hci_transport_h2_winusb.c](https://github.com/bluekitchen/btstack/blob/e38553977a25fb0b55b383c72c289be0975f422c/platform/windows/hci_transport_h2_winusb.c),
`ENABLE_SCO_OVER_HCI` enables associated-interface and WinUSB ISO buffer/transfer
code. It supports one SCO connection. Its device scan tries arbitrary USB
devices; integrate exact VID/PID **and instance** selection before using it here.

In [btstack_chipset_intel_firmware.c](https://github.com/bluekitchen/btstack/blob/e38553977a25fb0b55b383c72c289be0975f422c/chipset/intel/btstack_chipset_intel_firmware.c):

- Firmware naming includes HrP variant `0x13`; missing AX201 branding alone
  does not establish lack of support.
- `STATE_HANDLE_READ_VERSION_1` compares the entire event size with a return
  structure size, and `packet[1]` (event parameter length) with platform `0x37`.
  The Linux comparison instead uses return-parameter size and platform byte.
  This is a concrete parser defect to repair and regression-test before a cold
  firmware attempt; actual hardware impact here has not yet been observed.
- TLV mode is explicitly rejected. Other audit concerns include unchecked
  short firmware reads, command completion correlation, and a fixed boot address.
  Do not run this loader unmodified and claim reliable AX201 cold initialization.

These are static findings, not the cause of the current hardware failure:
execution has not reached this code. BTstack's licensing must be checked before
redistributing an integrated product; it is not vendored into this PoC.

## Intel firmware sequence to implement after USB access

Linux [btusb.c](https://github.com/torvalds/linux/blob/master/drivers/bluetooth/btusb.c)
explicitly lists `8087:0026` as `BTUSB_INTEL_COMBINED`. In bootloader mode it
routes Secure Send (`FC09`) through bulk OUT and accepts events on bulk IN.
Ordinary HCI commands use control OUT; normal events use interrupt IN.

From Linux [btintel.c](https://github.com/torvalds/linux/blob/master/drivers/bluetooth/btintel.c):

1. Read Intel version (`FC05`); validate status and distinguish legacy from TLV.
   Obtain the actual hardware revision rather than guessing from VID/PID.
2. Legacy firmware variant `06` denotes bootloader; `23` denotes operational.
   Read secure boot parameters (`FC0D`) when needed. Select `.sfi`/`.ddc` names
   from the reported revision. HrP uses `ibt-<variant>-<hw_revision>-<fw_revision>`.
   Some operational TLV replies require rereading legacy version information.
3. `.sfi` contains signed firmware. Validate its format, send authenticated
   header/key/signature/data fragments with `FC09`, and wait for vendor result.
   Derive the boot address from the image; do not blindly reuse a constant.
4. Issue Intel reset (`FC01`) and wait for bootup; re-read operational version.
5. `.ddc` contains configuration records, not executable firmware. Send validated
   length-prefixed records using `FC8B`; check each result. Apply the required
   event configuration and normal HCI initialization.

Firmware file selection and warm/cold behavior are still unknown on this unit.
An Intel Windows driver may already have loaded volatile firmware; that cannot
be relied upon after re-enumeration or a cold boot. No firmware was downloaded
to this device, and no guessed firmware blobs are included.

## Gate definitions (through call audio now passed)

1. **USB:** WinUSB open + validated target + interface 0 pipes. Then verify
   access to associated interface 1, without claiming an SCO link from descriptors.
2. **HCI:** correlated successful command replies, Intel initialization if needed,
   standard version/features/buffer queries and BD_ADDR. Only then log Controller
   ready. Handle timeout/cancellation, command credits and bootloader bulk events.
3. **HF:** integrate `hfp_hf_demo`; SDP Hands-Free `0x111E`, HFP profile `0x111E`,
   RFCOMM server and headset/audio Class of Device. Configure discoverability,
   pairing UI/confirmation and persistent link keys. No Audio Gateway record.
4. **Phone:** observe iPhone discovery/pairing, remote SDP result and successful
   RFCOMM connection, followed by HFP SLC negotiation. Each needs a real event.
5. **SCO:** obtain successful Synchronous Connection Complete with a valid handle;
   select appropriate USB alternate setting and verify ISO RX/TX packet progress.
   Start with CVSD, then negotiate/test mSBC only if both sides support it.
6. **Audio:** only after gate 5, add WASAPI capture/render, PCM conversion,
   codec handling, bounded ring buffers and adaptive clock-drift compensation.

The iPhone must participate in the pairing/audio-route test. Advertising an SDP
record or printing a demo log is not evidence that iOS accepted the headset.
