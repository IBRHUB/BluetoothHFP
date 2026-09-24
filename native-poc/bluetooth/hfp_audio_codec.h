#pragma once
#include <stdint.h>
unsigned hfp_audio_codec_start(uint8_t codec);
void hfp_audio_codec_receive(const uint8_t * packet, unsigned size);
void hfp_audio_codec_fill(uint8_t * payload, unsigned size);
void hfp_audio_codec_stop(void);
