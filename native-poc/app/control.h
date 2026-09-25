#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void control_init(void);
int control_read(char* command, unsigned capacity);
void control_event(const char* event, const char* value, int status);
void audio_controls(int mute, unsigned gain);
void audio_output_volume(unsigned gain);
void audio_apply_output(short* samples, unsigned count);
#ifdef __cplusplus
}
#endif
