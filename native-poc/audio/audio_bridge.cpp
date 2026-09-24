#include "audio_bridge.h"
#include "wasapi_internal.h"
#include <thread>
#include <cstdio>
#include <cstdlib>
namespace {
AudioState state;
std::thread capture, render;
}
extern "C" void audio_stop(void) {
    if (state.stop) SetEvent(state.stop);
    if (capture.joinable()) capture.join();
    if (render.joinable()) render.join();
    if (state.stop) { CloseHandle(state.stop); state.stop = nullptr; printf("[AUDIO] WASAPI stopped\n"); }
}
extern "C" void audio_start(unsigned rate) {
    audio_stop();
    if (rate != 8000 && rate != 16000) { printf("[AUDIO] Unsupported rate\n"); return; }
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
}
extern "C" void audio_render_write(const int16_t* samples, unsigned count) {
    int peak = 0;
    for (unsigned i = 0; i < count; ++i) peak = std::max(peak, std::abs(int(samples[i])));
    state.phone_peak = std::max(state.phone_peak.load(), peak);
    state.from_phone += count; state.playback.push(samples, count);
}
