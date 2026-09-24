# AX201 native Windows console PoC — stage 1

This is a separate C++17/MSVC project. It does not require Flutter, use the
Windows Bluetooth APIs, capture a microphone, or simulate HFP success.

**Current result: USB descriptors verified; direct WinUSB access blocked by the
installed Intel/BTHUSB driver. HCI, pairing, HFP and SCO connections are NOT
implemented or verified.** The stage gate deliberately stops here.

## Build and reproduce

From the repository root, on Windows 11 with Visual Studio 2022 C++ Build Tools,
Windows SDK and the Visual Studio CMake component:

```powershell
powershell -NoProfile -File native-poc\tools\Build.ps1
powershell -NoProfile -File native-poc\tools\Backup-Driver.ps1
& .\build\ax201-poc\Release\ax201_probe.exe --probe
$LASTEXITCODE
```

`--probe` reads PnP properties, attempts CreateFile/WinUsb_Initialize on the
specific `USB\VID_8087&PID_0026` device, and reads configuration descriptors
through USB hub IOCTLs. It does not replace a driver, reset a controller, select
an alternate setting, send HCI commands, or download firmware. The hub handle
requires write access for Windows IOCTL dispatch, but the requests are reads.

Exit codes: `0` = WinUSB handle, target identity and HCI endpoint layout verified;
`2` = target interface absent; `3` = direct ownership/open blocked;
`4` = descriptor/query failure; `64` = invalid CLI. Exit 0 does **not** establish
controller readiness or an audio link. A descriptor-only success is not enough.

## Observed on this machine, 2026-09-24

The executable built in Release x64 with MSVC `/W4 /WX`. Descriptor validation
tests passed. Hardware log: [evidence/usb-probe.txt](evidence/usb-probe.txt).

| Item | Actual observation |
|---|---|
| Device | Intel Wireless Bluetooth, USB `8087:0026`, revision `0002` |
| Instance | `USB\VID_8087&PID_0026\5&1A60D403&0&14` |
| Driver | Intel `22.80.0.4`, published `oem84.inf`, original `ibtusb.inf` |
| Ownership | Function service `BTHUSB`, lower filter `ibtusb` |
| Open result | CreateFile succeeded; WinUsb_Initialize failed with `50`, ERROR_NOT_SUPPORTED |
| USB | Hub port 14, full speed (speed enum 1) |
| Interface 0 / alt 0 | Event interrupt IN `81`; ACL bulk OUT `02`, IN `82`; all max packet 64 |
| Interface 1 / alt 0 | SCO isochronous OUT `03`, IN `83`, zero bandwidth |
| Interface 1 / alt 1–6 | Same SCO endpoints, max packet 9 / 17 / 25 / 33 / 49 / 63, interval 1 |
| Wi-Fi | Separate PCI PnP instance; AX201 adapter remained Up |

Thus the hardware exposes the USB layout needed for standard HCI plus SCO.
This is evidence of endpoints, **not** evidence that a WinUSB isochronous stream,
firmware initialization, CVSD or mSBC works on this unit.

## Recovery prepared before any binding change

The actual backup is under:

`.local/ax201-backup/20260924-053724-026/`

It contains the original INF, complete exported Intel INF/SYS/CAT package,
SHA256 manifest, PnP properties, Wi-Fi identity, `ROLLBACK.txt`, and a standalone
`Restore-Driver.ps1`. These machine-specific backups stay outside Git and outside
the build directory so build cleanup does not remove the recovery package.

```powershell
powershell -NoProfile -File native-poc\tools\Restore-Driver.ps1 `
  -BackupDirectory .local\ax201-backup\20260924-053724-026 -VerifyOnly
```

Verification passed for hashes, Bluetooth provider/version/service/filters and
Wi-Fi PnP state. **Driver replacement and subsequent recovery have not been
exercised.** The restore script can stage the original package; it does not
pretend that `pnputil /add-driver` forces a lower-ranked driver. The concrete
per-device recovery is Device Manager → exact instance → Update driver → Browse
→ Let me pick → Have Disk → exported `ibtusb.inf`, followed by verification.

No driver changes were made. No Wi-Fi device or USB parent was disabled.

## Precisely what blocks the next run

WinUSB needs a WinUSB driver binding. Merely linking winusb.lib or libusb does
not detach `BTHUSB`/`ibtusb`. The installed Windows `winusb.inf` targets
`USB\MS_COMP_WINUSB` (and other special classes), not this device's Bluetooth
compatible IDs. A matching, trusted driver-binding package or a deliberate
per-device signed-driver selection is still needed; this repo does not include
an installed or validated replacement package. Do not treat the backup as proof
that an arbitrary driver switch is safe.

The next hardware operation must bind only the saved USB instance to WinUSB,
preserving both USB interfaces, then rerun `--probe`. A normal Intel Bluetooth
driver cannot share HCI ownership with a second host stack. Windows Bluetooth
peripherals will be unavailable while the controller is owned by this PoC.

See [COMPATIBILITY.md](COMPATIBILITY.md) for the source audit and firmware work
needed after that USB gate passes. There are intentionally no placeholder
`hfp_hf` or WASAPI success logs.
