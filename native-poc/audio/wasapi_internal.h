#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <atomic>
#include <stdexcept>
#include "audio_ring_buffer.h"
using Microsoft::WRL::ComPtr;
void check_audio(HRESULT hr, const char* step);
struct AudioState {
    AudioRing capture, playback;
    HANDLE stop = nullptr;
    unsigned rate = 8000;
    std::atomic<uint64_t> captured{0}, rendered{0}, from_phone{0};
    std::atomic<int> mic_peak{0}, phone_peak{0};
};
struct WasapiEndpoint {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    HANDLE event = nullptr;
    UINT32 capacity = 0;
    WasapiEndpoint(bool capture, unsigned rate);
    ~WasapiEndpoint();
};
void capture_worker(AudioState& state);
void render_worker(AudioState& state);
