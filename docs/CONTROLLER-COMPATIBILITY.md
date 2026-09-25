# Controller compatibility foundation

Implemented on 2026-09-25. This is infrastructure for additional controllers, not
a claim that any additional chipset has passed physical acceptance.

## One compatibility catalog

`native-poc/tools/controller-profiles.json` is consumed by PowerShell and compiled
by both native CMake projects. Each entry identifies a USB VID/PID, an implemented
initialization backend, driver recovery policy and validation level. The existing
`8087:0026` Intel legacy backend remains the only enabled profile. Its original
reset/version/features/buffer/address command ordering is retained; the standard
manufacturer response must now also match Intel before vendor commands are sent.

`reference-tested` means evidence exists on the reference setup, not certification
of every unit with that USB identity. `experimental` entries are visible but are
excluded from native transport/driver permissions and automatic selection.
Unknown physical radios are listed as unsupported. Merely adding an entry cannot
implement a missing firmware loader, USB transport layout or chipset backend.

## Device identity and recovery

- Discovery lists physical radios separately from paired Bluetooth peripherals.
  Unrelated unsupported radios can coexist with one enabled controller.
- Multiple enabled controllers remain blocked. This release does not implement
  multi-controller operation or user selection among several enabled radios.
- The desktop uses `selectedId`, not the first enumeration result. Elevation
  carries that identity as `ExpectedInstanceId`; a changed selection is rejected
  before a driver binding operation. Native matching rejects partial product IDs
  and composite child interfaces. The probe rejects ambiguous enabled controllers.
- Backup schema 2 records the actual original INF section, profile, provider and
  service. Snapshot and original INF are included in the hash manifest alongside
  exported driver files. Controller switching and recovery share one validator.
- Existing AX201 backups without schema 2 retain their tested `ibtusb` fallback.
  New standalone backups contain their policy/catalog dependencies. Existing
  standalone recovery scripts remain untouched.
- Recovery still requires a supported, present controller and an exported OEM
  INF. Inbox-driver recovery and arbitrary vendor recovery are not implemented.
  Matching failures and missing/corrupt backups stop the operation.

## Checks and limits

`native-poc/tests/controller_policy_tests.ps1` exercises mixed/ambiguous radios,
experimental gating, virtual-device filtering, legacy/new recovery metadata,
changed metadata/INF, incomplete manifests and disconnected recovery using PnP
test doubles. It does not invoke real driver changes. Native tests cover exact
identity/path matching; Dart tests cover selection independent of discovery order
and invalid/duplicate selections. Existing audio/codec tests continue to run.

Read-only inspection identified the reference controller correctly. A fresh export
of its original driver passed `Restore-Driver.ps1 -VerifyOnly`; this verifies backup
integrity and the existing binding, not a new driver-switch cycle or call test.
No new chipset, cold boot, suspend/resume, physical unplug under load or radio
audio behavior is certified by these tests.

Next stages require dedicated chipset initialization/firmware implementations,
transport capability negotiation, broader audio endpoint recovery and physical
acceptance reports before promoting additional profiles. Keep Windows 11 x64 as
the release target until other operating system/architecture combinations pass.
