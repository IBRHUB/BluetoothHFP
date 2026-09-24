#include "media_render.h"
#include "wasapi_internal.h"
#include <thread>
#include <cstdio>
namespace {
PcmRing<2> ring;
HANDLE stop_event = nullptr;
std::thread worker;
std::atomic<unsigned> volume{100};
std::atomic<uint64_t> decoded_frames{0};
void run(unsigned rate) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        check_audio(com, "Media COM");
        WasapiEndpoint endpoint(false, rate, 2);
        ComPtr<IAudioRenderClient> render;
        check_audio(endpoint.client->GetService(IID_PPV_ARGS(&render)), "Media render service");
        BYTE* data = nullptr;
        check_audio(render->GetBuffer(endpoint.capacity, &data), "Media prime");
        check_audio(render->ReleaseBuffer(endpoint.capacity, AUDCLNT_BUFFERFLAGS_SILENT), "Media prime release");
        check_audio(endpoint.client->Start(), "Media start");
        printf("[A2DP] WASAPI stereo playback started rate=%u\n", rate);
        HANDLE waits[] = {stop_event, endpoint.event};
        auto next_log = GetTickCount64() + 5000;
        uint64_t rendered = 0;
        for (;;) {
            const DWORD result = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (result == WAIT_OBJECT_0) break;
            if (result != WAIT_OBJECT_0 + 1) throw std::runtime_error("Media render stalled");
            UINT32 padding = 0;
            check_audio(endpoint.client->GetCurrentPadding(&padding), "Media padding");
            if (padding > endpoint.capacity) throw std::runtime_error("Invalid media padding");
            const auto count = endpoint.capacity - padding;
            if (count) {
                check_audio(render->GetBuffer(count, &data), "Media get buffer");
                auto pcm = reinterpret_cast<int16_t*>(data);
                ring.pull(pcm, count);
                const unsigned gain = volume.load();
                for (unsigned i = 0; i < count * 2; ++i) pcm[i] = static_cast<int16_t>(int(pcm[i]) * int(gain) / 127);
                check_audio(render->ReleaseBuffer(count, 0), "Media release buffer");
                rendered += count;
            }
            if (GetTickCount64() >= next_log) {
                const auto stats = ring.stats();
                printf("[A2DP] decoded=%llu rendered=%llu queue=%zu under=%llu over=%llu\n",
                    (unsigned long long)decoded_frames.load(), (unsigned long long)rendered, stats.queued,
                    (unsigned long long)stats.underrun, (unsigned long long)stats.overrun);
                next_log = GetTickCount64() + 5000;
            }
        }
    } catch (const std::exception& e) { printf("[A2DP] Playback failed: %s\n", e.what()); }
    if (SUCCEEDED(com)) CoUninitialize();
}
}
extern "C" void media_render_stop(void) {
    if (stop_event) SetEvent(stop_event);
    if (worker.joinable()) worker.join();
    if (stop_event) { CloseHandle(stop_event); stop_event = nullptr; printf("[A2DP] Playback stopped\n"); }
}
extern "C" void media_render_start(unsigned rate) {
    media_render_stop();
    if (rate != 44100 && rate != 48000) return;
    ring.reset(rate, 80); decoded_frames = 0;
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event) return;
    try { worker = std::thread(run, rate); }
    catch (const std::exception& e) { printf("[A2DP] Worker failed: %s\n", e.what()); media_render_stop(); }
}
extern "C" void media_render_write(const int16_t* pcm, unsigned frames) {
    if (!stop_event) return;
    ring.push(pcm, frames); decoded_frames += frames;
}
extern "C" void media_render_volume(unsigned value) { volume = std::min(value, 127u); }
