#include "btstack.h"
#include "classic/hfp_codec.h"
#include "classic/btstack_cvsd_plc.h"
#include "classic/btstack_sbc_bluedroid.h"
#include "btstack_lc3_google.h"
#include "hfp_audio_codec.h"
#include "audio/audio_bridge.h"
#include <string.h>

static uint8_t active_codec;
static hfp_codec_t encoder;
static btstack_sbc_encoder_bluedroid_t encoder_state;
static btstack_sbc_decoder_bluedroid_t decoder_state;
static const btstack_sbc_decoder_t * decoder;
static btstack_cvsd_plc_state_t cvsd;
static btstack_lc3_decoder_google_t lc3_decoder_state;
static btstack_lc3_encoder_google_t lc3_encoder_state;
static const btstack_lc3_decoder_t* lc3_decoder;
static hfp_h2_sync_t h2;
static bool lc3_frame(bool bad, const uint8_t* frame, uint16_t length) {
    if (!bad && (frame == NULL || length < 60)) return false;
    int16_t pcm[240]; uint8_t corrupted = 0;
    lc3_decoder->decode_signed_16(&lc3_decoder_state, bad ? NULL : frame + 2,
        bad ? 1 : 0, pcm, 1, &corrupted);
    audio_render_write(pcm, 240);
    return !bad && !corrupted;
}
static void decoded(int16_t * data, int samples, int channels, int rate, void * context) {
    (void)context;
    if (channels == 1 && rate == 16000 && samples > 0) audio_render_write(data, (unsigned)samples);
}
unsigned hfp_audio_codec_start(uint8_t codec) {
    active_codec = codec;
    if (codec == HFP_CODEC_LC3_SWB) {
        const btstack_lc3_encoder_t* impl = btstack_lc3_encoder_google_init_instance(&lc3_encoder_state);
        hfp_codec_init_lc3_swb(&encoder, impl, &lc3_encoder_state);
        lc3_decoder = btstack_lc3_decoder_google_init_instance(&lc3_decoder_state);
        lc3_decoder->configure(&lc3_decoder_state, 32000, BTSTACK_LC3_FRAME_DURATION_7500US, 58);
        hfp_h2_sync_init(&h2, lc3_frame);
        return 32000;
    }
    if (codec == HFP_CODEC_CVSD) { btstack_cvsd_plc_init(&cvsd); return 8000; }
    if (codec == HFP_CODEC_MSBC) {
        decoder = btstack_sbc_decoder_bluedroid_init_instance(&decoder_state);
        decoder->configure(&decoder_state, SBC_MODE_mSBC, decoded, NULL);
        const btstack_sbc_encoder_t * implementation = btstack_sbc_encoder_bluedroid_init_instance(&encoder_state);
        hfp_codec_init_msbc_with_codec(&encoder, implementation, &encoder_state);
        return 16000;
    }
    active_codec = 0; return 0;
}
void hfp_audio_codec_receive(const uint8_t * packet, unsigned size) {
    if (size < 3 || size != (unsigned)packet[2] + 3) return;
    if (active_codec == HFP_CODEC_LC3_SWB) {
        hfp_h2_sync_process(&h2, (packet[1] & 0x30) != 0, packet + 3, (uint16_t)(size - 3));
    } else if (active_codec == HFP_CODEC_MSBC) {
        decoder->decode_signed_16(&decoder_state, (packet[1] >> 4) & 3, packet + 3, (uint16_t)(size - 3));
    } else if (active_codec == HFP_CODEC_CVSD) {
        if ((size - 3) % 2) return;
        int16_t input[128], output[128];
        const unsigned count = (size - 3) / 2;
        if (count > 128) return;
        for (unsigned i = 0; i < count; ++i) input[i] = (int16_t)little_endian_read_16(packet, 3 + 2 * i);
        btstack_cvsd_plc_process_data(&cvsd, (packet[1] & 0x30) != 0, input, (uint16_t)count, output);
        audio_render_write(output, count);
    }
}
void hfp_audio_codec_fill(uint8_t * payload, unsigned size) {
    memset(payload, 0, size);
    if (active_codec == HFP_CODEC_CVSD) {
        int16_t samples[128];
        if (size > sizeof(samples)) return;
        audio_capture_read(samples, size / 2);
        for (unsigned i = 0; i < size / 2; ++i) little_endian_store_16(payload, i * 2, (uint16_t)samples[i]);
    } else if (active_codec == HFP_CODEC_MSBC || active_codec == HFP_CODEC_LC3_SWB) {
        while (size) {
            if (hfp_codec_can_encode_audio_frame_now(&encoder)) {
                int16_t samples[240];
                const unsigned count = hfp_codec_num_audio_samples_per_frame(&encoder);
                if (count > 240) return;
                audio_capture_read(samples, count);
                hfp_codec_encode_audio_frame(&encoder, samples);
            }
            unsigned count = hfp_codec_num_bytes_available(&encoder);
            if (!count) return; // Fail closed rather than spin in the USB callback.
            if (count > size) count = size;
            hfp_codec_read_from_stream(&encoder, payload, (uint16_t)count);
            size -= count; payload += count;
        }
    }
}
void hfp_audio_codec_stop(void) {
    if (active_codec == HFP_CODEC_MSBC || active_codec == HFP_CODEC_LC3_SWB) hfp_codec_deinit(&encoder);
    active_codec = 0;
}
