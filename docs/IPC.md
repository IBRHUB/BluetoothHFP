# Desktop/engine protocol v1

The Dart application starts `engine/ax201_headset.exe` with redirected stdin,
stdout and stderr. No command file is used by the desktop. `AX201_IPC=1` enables
events, `AX201_USB_INSTANCE` selects the exact inspected USB function,
`AX201_CAPTURE`/`AX201_RENDER` select IMMDevice IDs (empty means default), and
`AX201_HFP_CODEC` is Auto, mSBC or CVSD.

Requests are bounded UTF-8 newline-delimited command records (at most 511 bytes):

```
scan
connect AA:BB:CC:DD:EE:FF
disconnect
forget AA:BB:CC:DD:EE:FF
mic 0 100
volume 100
audio
audio-off
quit
```

`mic` takes mute 0/1 and gain 0..100 percent. `volume` is local gain 0..127,
applied in addition to the phone's AVRCP volume. Forget requires disconnection.
The application validates addresses before sending. No shell command execution
is exposed through this channel. Windows driver operations are separate,
allowlisted elevated operations with completion/exit-code checks.

Responses prefixed `@` are JSON objects:

```json
{"version":1,"event":"call","value":"codec","status":2}
```

Events: ready, peer, paired, hfp, disconnected, call, mediaCodec, media,
device, scanComplete, forgotten, command, error, audioError.
Most status fields are zero on success. `call/codec` uses codec IDs 1/2/3;
negative values indicate HFP errors. `media/playing` carries the PCM rate.
Text diagnostics have no `@` prefix and are never parsed to infer connection state.
Private pairing keys and packet dumps are not diagnostic exports.

The native run loop consumes requests on its timer; USB/profile operations stay
on that thread. Audio workers run separately. Stdout JSON records are emitted in
one locked stdio call. Broken stdin triggers graceful shutdown (including after a
desktop crash). A named mutex prevents a second engine in the same session.
The frontend serializes lifecycle operations and settings writes. If stopping
times out, it refuses to rebind the controller rather than killing a live USB owner.

The older developer console may still use command.txt in its own working folder;
it is retained for reproduction of historical hardware tests, not used by the app.
