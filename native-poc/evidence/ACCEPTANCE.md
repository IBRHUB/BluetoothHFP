# Hardware acceptance, 2026-09-24

Privacy: device addresses in these evidence files are synthetic replacements;
local instance suffixes, profile paths and endpoint GUIDs are redacted.

User confirmed during the live tests:

1. AX201 HFP Headset discovered and paired on iPhone 12 Pro Max.
2. HFP CVSD call audio works in both directions.
3. Updated mSBC call audio works in both directions; controller log reports codec 2.
4. YouTube audio plays through PC headphones after adding A2DP.
5. Switching from YouTube to a call retains PC microphone and headphones;
   video audio works again after the call.

Hardware: internal Intel USB 8087:0026, HyperX QuadCast S capture, FiiO K11 render.
No external Bluetooth dongle. Wi-Fi remained Up. Intel driver restoration was
exercised and verified before returning to WinUSB for the live headset.

See a2dp-call-transition.txt for actual media/call/resume events. These are short
functional acceptance tests, not a long-duration latency/jitter certification.

## Quality revision

User confirmed clear media playback and two-way call audio with this revision.
The new live log (`quality-success.txt`) shows:

- AAC-LC selected by iPhone: stereo 44.1 kHz, 256000 bit/s, VBR.
- AAC decodeErrors=0 and media under=0/over=0 in the captured sample.
- Call SCO codec=2 (mSBC), capture/render PCM16 mono 16000 Hz.
- RAW processing accepted on HyperX QuadCast S and FiiO K11.
- Approximately 25 seconds of call counters with under=0/0 and over=0/0.
- LC3-SWB was offered but not selected. No 32 kHz phone interoperability claim.

Local tests pass: bounded ring, mono/stereo alignment, 500 ppm clock drift,
sinc response within 2% at 0.4 cycles/sample, and LC3 32 kHz encode/decode
plus lost-frame invocation. LC3 roundtrip is a synthetic test, not a phone test.
See `quality-tests.txt`. The earlier media/call/resume test remains in
`a2dp-call-transition.txt`; user's current confirmation also covers those flows.
