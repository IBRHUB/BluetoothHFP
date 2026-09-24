#include "wasapi_internal.h"
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>
void check_audio(HRESULT hr, const char* step) {
    if (FAILED(hr)) { char error[180]; sprintf_s(error, "%s HRESULT=0x%08lx", step, static_cast<unsigned long>(hr)); throw std::runtime_error(error); }
}
WasapiEndpoint::WasapiEndpoint(bool capture, unsigned rate) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    check_audio(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "Enumerate audio");
    // Resolve defaults once per SCO link. Never silently switch endpoints mid-call.
    check_audio(enumerator->GetDefaultAudioEndpoint(capture ? eCapture : eRender, eConsole, &device), "Get default endpoint");
    ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
        PROPVARIANT name; PropVariantInit(&name);
        if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
            printf("[AUDIO] %s endpoint: %ls\n", capture ? "Capture" : "Playback", name.pwszVal);
        PropVariantClear(&name);
    }
    check_audio(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "Activate audio client");
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 1; format.nSamplesPerSec = rate;
    format.wBitsPerSample = 16; format.nBlockAlign = 2; format.nAvgBytesPerSec = rate * 2;
    check_audio(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        300000, 0, &format, nullptr), "Initialize shared PCM with resampling");
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) throw std::runtime_error("Create audio event failed");
    const HRESULT hr = client->SetEventHandle(event);
    if (FAILED(hr)) { CloseHandle(event); event = nullptr; check_audio(hr, "Set audio event"); }
    const HRESULT size_hr = client->GetBufferSize(&capacity);
    if (FAILED(size_hr)) { CloseHandle(event); event = nullptr; check_audio(size_hr, "Get buffer size"); }
}
WasapiEndpoint::~WasapiEndpoint() { if (client) client->Stop(); if (event) CloseHandle(event); }
