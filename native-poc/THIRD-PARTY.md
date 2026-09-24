# Dependencies

The headset links BlueKitchen BTstack revision
`e38553977a25fb0b55b383c72c289be0975f422c`, fetched into the build tree.
Its license and bundled third-party notices remain in that checkout, including
the generated transport/profile copies. BTstack permits personal non-commercial
use under its stated conditions; commercial use requires appropriate licensing.
Review its LICENSE and bundled notices before redistributing binaries.

HFP/A2DP setup follows the public APIs demonstrated by hfp_hf_demo,
sco_demo_util and a2dp_sink_demo. BTstack provides profile protocols, SBC/mSBC,
H2 framing, CVSD concealment and USB SCO scheduling. This project implements
WASAPI capture/render, bounded queues, drift compensation and driver lifecycle.

Upstream: https://github.com/bluekitchen/btstack

AAC decoding links the Fraunhofer FDK AAC library, fetched from
https://github.com/mstorsjo/fdk-aac at
`7c83d08002332b2730c845eec3497e6bf585dd28`. Its NOTICE and source license
headers are preserved in the checkout. Review the Fraunhofer license and
patent licensing provisions before redistribution; BTstack's license does
not cover this dependency.

Optional HFP LC3-SWB uses Google's LC3 implementation bundled in the pinned
BTstack checkout, under Apache-2.0. Preserve its copyright and license notices.
