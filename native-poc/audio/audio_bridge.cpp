#include "audio_bridge.h"
#include "wasapi_internal.h"
#include <thread>
#include <cstdio>
#include <cstdlib>
#include "app/control.h"
namespace {
AudioState state;
std::thread capture, render;
std::atomic<bool> muted{false};
std::atomic<unsigned> mic_gain{100}, output_gain{100};
}
extern "C" void audio_stop(void) {
    if (state.stop) SetEvent(state.stop);
    if (capture.joinable()) capture.join();
    if (render.joinable()) render.join();
    if (state.stop) { CloseHandle(state.stop); state.stop = nullptr; printf("[AUDIO] WASAPI stopped\n"); }
}
extern "C" void audio_start(unsigned rate) {
    audio_stop();
    if (rate != 8000 && rate != 16000 && rate != 32000) { printf("[AUDIO] Unsupported rate\n"); return; }
    state.rate = rate; state.capture.reset(rate); state.playback.reset(rate);
    state.captured = state.rendered = state.from_phone = 0;
    state.mic_peak = state.phone_peak = 0;
    state.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state.stop) { printf("[AUDIO] Cannot create stop event\n"); return; }
    try { capture = std::thread(capture_worker, std::ref(state)); render = std::thread(render_worker, std::ref(state)); }
    catch (const std::exception& error) { printf("[AUDIO] Cannot start workers: %s\n", error.what()); audio_stop(); }
}
extern "C" void audio_capture_read(int16_t* samples, unsigned count) {
    state.capture.pull(samples, count);
    if (muted) std::fill_n(samples, count, int16_t(0));
    else for (unsigned i=0; i<count; ++i) samples[i] = (int16_t)(int(samples[i]) * int(mic_gain.load()) / 100);
}
extern "C" void audio_controls(int mute, unsigned gain) { muted = mute != 0; mic_gain = std::min(gain, 100u); }
extern "C" void audio_output_volume(unsigned gain) { output_gain = std::min(gain, 127u); }
extern "C" void audio_apply_output(short* samples, unsigned count) {
    const unsigned gain = output_gain.load();
    for (unsigned i=0; i<count; ++i) samples[i] = (int16_t)(int(samples[i]) * int(gain) / 127);
}
extern "C" void audio_render_write(const int16_t* samples, unsigned count) {
    int peak = 0;
    for (unsigned i = 0; i < count; ++i) peak = std::max(peak, std::abs(int(samples[i])));
    state.phone_peak = std::max(state.phone_peak.load(), peak);
    state.from_phone += count; state.playback.push(samples, count);
}
