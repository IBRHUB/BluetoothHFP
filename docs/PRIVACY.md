# Sharing the project

Use `windows/package/Export-Source.ps1` to create `build/share/BluetoothHFP-Source.zip`.
It includes source, tests and redacted documentation from an explicit allowlist.
It excludes Git history, build products, runtime state, driver backups and archives.
Bluetooth addresses in examples and evidence are synthetic. Device instance
suffixes, user profile paths and endpoint GUIDs in evidence have been redacted.
Hardware model names and USB vendor/product IDs describe compatibility, not secrets.

Do not publish the entire workspace or its `.local` directory. Historical Git
commits may contain the original device addresses and local paths; cleaning the
working tree does not erase those commits. The source ZIP has no Git history.
The filename review of history found no committed TLV pairing database or packet
capture, but this is not a guarantee that every historical blob is secret-free.

Active pairing keys stay in `%LOCALAPPDATA%\BluetoothHFP\link-keys.tlv` so the
existing phone connection continues to work. Legacy PoC state and packet captures
have been isolated in `%LOCALAPPDATA%\BluetoothHFP\private-legacy`; keep that folder
private. Raw HCI capture is disabled in the engine because HCI pairing events can
contain secret keys even when audio packets are filtered out.

Diagnostic exports omit device names and instance IDs and redact Bluetooth
addresses, endpoint GUIDs and the current Windows user profile path. Review any
manually collected log before sharing. Driver recovery backups remain private and
are not included in either the source ZIP or the installer.
