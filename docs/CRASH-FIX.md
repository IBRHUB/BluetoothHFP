# Desktop crash investigation, 2026-09-25

Windows Application events recorded repeated access violations (`0xc0000005`)
in the installed `flutter_windows.dll` at offset `0x3c16a`. Its SHA256 matched
the Flutter SDK release DLL. Resolving the offset with that SDK's PDB identified
`flutter::AccessibilityBridge::CreateRemoveReparentedNodesUpdate + 186`.

This matches [Flutter issue 175041](https://github.com/flutter/flutter/issues/175041).
The [proposed engine fix](https://github.com/flutter/flutter/pull/190903) guards
a missing parent during accessibility tree updates; it is not included in our
current engine. Starting semantics earlier did not resolve the tree errors in
the desktop navigation test.

The Windows app therefore excludes the dynamic UI from the native semantics
tree by default. This is a compatibility workaround, not an engine repair.
Screen readers cannot read the app's controls in this mode. Mouse, keyboard
focus and shortcuts are unaffected. `--enable-accessibility` opts back into
the original behavior for testing after an engine update; on the current engine
it can reproduce the crash. Remove the workaround after a fixed engine passes
navigation, dropdown, dialog and screen-reader tests.

Other fixes handle background settings-write failures, broken engine stdin,
stderr errors and notifications after controller disposal. The Windows view is
detached before destruction so reentrant window messages cannot use it.

The layout targets a 540 by 960 logical-pixel client area. Windows scales it
with display DPI and caps the outer frame to the monitor work area when needed.
Connection, Audio and Settings have separate scrollable pages. The background
is black, with blue actions, red errors, yellow hints and green ready/connected
status.

Validation: Flutter analysis, nine unit/widget tests and a Windows release
build passed. Native desktop stress-test results are recorded separately in
`native-poc/evidence/desktop-crash-fix.txt`. This work does not repeat phone
call, driver-switching or long-duration audio validation.

The gray Logs panel was a separate UI exception: the expansion state and nested
scroll offset shared a PageStorage identifier, causing a bool-to-double cast.
Separate keys fix it; the widget test opens Logs, populates it and changes tabs.

The release icon subset lacked the Audio and Settings codepoints. The app uses
Flutter's official Material Icons constants, and release builds now include
the complete SDK icon font (`--no-tree-shake-icons`). Both icons were checked
visually in the built desktop. Contrast tests cover filled button text,
disabled text and selected/unselected navigation icons.
