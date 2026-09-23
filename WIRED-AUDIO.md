# Wired setup: iPhone 12 Pro Max, iOS 17

## Selected design

Keep the existing HyperX QuadCast S connected to the PC and headphones on the
FiiO K11. Use a separate audio interface path between Windows and the iPhone.
The app sends only the selected microphone to that path and plays the phone's
return audio through the selected headphones. It does not capture Windows
system playback or create a virtual microphone inside iOS.

The inspected PC currently exposes HyperX, FiiO and SteelSeries Sonar endpoints;
no dedicated phone interface endpoint was identified. Virtual Sonar devices are
not evidence of a physical route to the iPhone.

## Hardware and cabling

Concrete baseline topology (does not require a special dual-host USB device):

1. Apple Lightning to USB 3 Camera Adapter, connected to power and the iPhone.
2. A powered, iOS 17 compatible USB audio interface with line input and stereo
   line output, attached to that adapter. Confirm iPhone support with its maker.
3. A separate Windows USB audio interface with line output and stereo line input.
4. Suitable line-level cables: PC interface output -> phone interface line input;
   phone interface stereo output -> PC interface stereo input.

Use line input mode and conservative levels. Do not feed a line output directly
into a headset microphone input. Disable direct monitoring/loopback paths that
feed either interface's input back into its output; the two physical directions
must stay separate. The existing FiiO remains the headphone output, not the
phone interface. A USB hub alone cannot share one interface between two hosts.

An interface explicitly supporting simultaneous PC/iPhone connections and
independent routing between them can replace the two-interface arrangement.
Merely having two USB sockets does not establish this capability.

Apple documents audio/MIDI accessory support with its powered adapter:
https://support.apple.com/en-us/111811

## App selection

| Control | Choose |
| --- | --- |
| Microphone | HyperX QuadCast S |
| Headphones | FiiO K11 |
| From phone | PC interface stereo line input |
| To phone | PC interface line output |

Switch to wired audio, select all four devices, then Start wired audio. Configure
the phone interface to send its physical line input to iOS and iOS playback to
its physical outputs. PC system sounds continue through their existing Windows
output. The app uses shared-mode audio and preserves a stereo phone return;
it downmixes the outgoing microphone to mono. Multichannel routing beyond a
stereo return is not provided. There is buffering and sample-rate conversion,
so this is not a promise of bit-perfect or zero-latency playback.

## Acceptance test on the phone

No phone call is needed. Test Voice Memos first, then each desired messaging app:

1. Begin a recording and speak into the HyperX while away from the iPhone mic.
2. Physically mute the HyperX, continue speaking, then unmute and speak again.
3. Play back the recording. The muted segment must be silent; Windows stream
   progress alone cannot establish that iOS used the external microphone.
4. Play a left/right stereo test on the iPhone and confirm both channels in the
   existing headphones. Confirm PC audio still plays there as before.
5. Check that phone playback is not sent back into the recorded microphone path.
6. Stop the bridge, unplug an interface, and confirm transmission stops. Restore
   the connection and explicitly Start again.

iOS and each recording app choose the available input route. This implementation
does not force every app to use an external input or bypass microphone access.
End-to-end recording, phone playback and latency require the physical setup.
