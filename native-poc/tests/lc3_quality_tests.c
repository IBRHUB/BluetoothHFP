#include "btstack_lc3_google.h"
#include <math.h>
#include <stdio.h>

// Exercise the optional speech codec even when the connected phone picks mSBC.
int main(void) {
    static btstack_lc3_encoder_google_t enc;
    static btstack_lc3_decoder_google_t dec;
    const btstack_lc3_encoder_t* encoder = btstack_lc3_encoder_google_init_instance(&enc);
    const btstack_lc3_decoder_t* decoder = btstack_lc3_decoder_google_init_instance(&dec);
    if (encoder->configure(&enc, 32000, BTSTACK_LC3_FRAME_DURATION_7500US, 58) ||
        decoder->configure(&dec, 32000, BTSTACK_LC3_FRAME_DURATION_7500US, 58)) return 1;
    int16_t input[240], output[240];
    uint8_t frame[58], corrupt = 0;
    double energy = 0;
    for (int n = 0; n < 80; ++n) {
        for (int i = 0; i < 240; ++i)
            input[i] = (int16_t)(12000 * sin(6.283185307179586 * 1000 * (n * 240 + i) / 32000));
        if (encoder->encode_signed_16(&enc, input, 1, frame)) return 2;
        if (decoder->decode_signed_16(&dec, frame, 0, output, 1, &corrupt) || corrupt) return 3;
        if (n > 10) for (int i = 0; i < 240; ++i) energy += (double)output[i] * output[i];
    }
    const double rms = sqrt(energy / (69 * 240));
    if (rms < 6000 || rms > 11000) return 4;
    decoder->decode_signed_16(&dec, NULL, 1, output, 1, &corrupt);
    printf("LC3-SWB 32kHz/7.5ms/58-byte encode/decode and lost-frame path passed; RMS=%.1f\n", rms);
    return 0;
}
