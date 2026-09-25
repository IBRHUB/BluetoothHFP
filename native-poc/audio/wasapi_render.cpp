#include "wasapi_internal.h"
#include <cstdio>
#include "app/control.h"
void render_worker(AudioState& state) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        check_audio(com, "Render COM");
        WasapiEndpoint endpoint(false, state.rate);
        ComPtr<IAudioRenderClient> render;
        check_audio(endpoint.client->GetService(IID_PPV_ARGS(&render)), "Get render client");
        BYTE* data = nullptr;
        check_audio(render->GetBuffer(endpoint.capacity, &data), "Prime render buffer");
        check_audio(render->ReleaseBuffer(endpoint.capacity, AUDCLNT_BUFFERFLAGS_SILENT), "Prime render release");
        check_audio(endpoint.client->Start(), "Start playback");
        printf("[AUDIO] WASAPI playback started rate=%u mono PCM16\n", state.rate);
        HANDLE waits[] = {state.stop, endpoint.event};
        ULONGLONG next_log = GetTickCount64() + 5000;
        for (;;) {
            const DWORD result = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (result == WAIT_OBJECT_0) break;
            if (result != WAIT_OBJECT_0 + 1) throw std::runtime_error("Playback event stalled");
            UINT32 padding = 0;
            check_audio(endpoint.client->GetCurrentPadding(&padding), "Read render padding");
            if (padding > endpoint.capacity) throw std::runtime_error("Invalid render padding");
            const UINT32 available = endpoint.capacity - padding;
            if (available) {
                check_audio(render->GetBuffer(available, &data), "Get render buffer");
                state.playback.pull(reinterpret_cast<int16_t*>(data), available);
                audio_apply_output(reinterpret_cast<int16_t*>(data), available);
                check_audio(render->ReleaseBuffer(available, 0), "Release render buffer");
                state.rendered += available;
            }
            if (GetTickCount64() >= next_log) {
                const auto tx = state.capture.stats(), rx = state.playback.stats();
                printf("[AUDIO] capture=%llu phoneRX=%llu render=%llu micPeak=%d phonePeak=%d TXq=%zu RXq=%zu under=%llu/%llu over=%llu/%llu\n",
                    (unsigned long long)state.captured.load(), (unsigned long long)state.from_phone.load(),
                    (unsigned long long)state.rendered.load(), state.mic_peak.exchange(0), state.phone_peak.exchange(0),
                    tx.queued, rx.queued, (unsigned long long)tx.underrun, (unsigned long long)rx.underrun,
                    (unsigned long long)tx.overrun, (unsigned long long)rx.overrun);
                next_log = GetTickCount64() + 5000;
            }
        }
    } catch (const std::exception& error) { control_event("audioError", error.what(), 1); printf("[AUDIO] Playback failed: %s\n", error.what()); SetEvent(state.stop); }
    if (SUCCEEDED(com)) CoUninitialize();
}
