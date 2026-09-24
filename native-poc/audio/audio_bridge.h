#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void audio_start(unsigned sample_rate);
void audio_stop(void);
void audio_capture_read(int16_t * samples, unsigned count);
void audio_render_write(const int16_t * samples, unsigned count);
#ifdef __cplusplus
}
#endif
