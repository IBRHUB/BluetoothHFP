#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void control_init(void);
int control_read(char* command, unsigned capacity);
void control_event(const char* event, const char* value, int status);
void audio_controls(int mute, unsigned gain);
#ifdef __cplusplus
}
#endif
