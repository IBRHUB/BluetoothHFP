#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void media_render_start(unsigned rate);
void media_render_stop(void);
void media_render_write(const int16_t* pcm, unsigned frames);
void media_render_volume(unsigned volume);
#ifdef __cplusplus
}
#endif
