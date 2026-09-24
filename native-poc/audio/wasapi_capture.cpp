#include "wasapi_internal.h"
#include <cstdio>
#include <cstdlib>
void capture_worker(AudioState& state) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        check_audio(com, "Capture COM");
        WasapiEndpoint endpoint(true, state.rate);
        ComPtr<IAudioCaptureClient> capture;
        check_audio(endpoint.client->GetService(IID_PPV_ARGS(&capture)), "Get capture client");
        check_audio(endpoint.client->Start(), "Start capture");
        printf("[AUDIO] WASAPI capture started rate=%u mono PCM16\n", state.rate);
        HANDLE waits[] = {state.stop, endpoint.event};
        for (;;) {
            const DWORD result = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (result == WAIT_OBJECT_0) break;
            if (result != WAIT_OBJECT_0 + 1) throw std::runtime_error("Capture event stalled");
            UINT32 frames = 0;
            check_audio(capture->GetNextPacketSize(&frames), "Get capture packet size");
            while (frames) {
                BYTE* bytes = nullptr; DWORD flags = 0;
                check_audio(capture->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr), "Get capture packet");
                const int16_t* pcm = flags & AUDCLNT_BUFFERFLAGS_SILENT ? nullptr : reinterpret_cast<int16_t*>(bytes);
                int peak = 0;
                if (pcm) for (UINT32 i = 0; i < frames; ++i) peak = std::max(peak, std::abs(int(pcm[i])));
                state.mic_peak = std::max(state.mic_peak.load(), peak);
                state.capture.push(pcm, frames); state.captured += frames;
                check_audio(capture->ReleaseBuffer(frames), "Release capture packet");
                check_audio(capture->GetNextPacketSize(&frames), "Get capture packet size");
            }
        }
    } catch (const std::exception& error) { printf("[AUDIO] Capture failed: %s\n", error.what()); SetEvent(state.stop); }
    if (SUCCEEDED(com)) CoUninitialize();
}
