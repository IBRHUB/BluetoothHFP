#include "audio_bridge.h"

#include <audioclient.h>
#include <ks.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <functiondiscoverykeys_devpkey.h>
#include <endpointvolume.h>
#include <sstream>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct Format {
  UINT channels = 0;
  UINT rate = 0;
  UINT bits = 0;
  UINT valid_bits = 0;
  bool floating = false;
  UINT frame_bytes = 0;
};

bool Describe(const WAVEFORMATEX* wave, Format* format) {
  if (!wave || !wave->nChannels || !wave->nSamplesPerSec) return false;
  format->channels = wave->nChannels;
  format->rate = wave->nSamplesPerSec;
  format->bits = wave->wBitsPerSample;
  format->valid_bits = format->bits;
  format->frame_bytes = wave->nBlockAlign;
  if (wave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    format->floating = true;
  } else if (wave->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
             wave->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave);
    format->floating = extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    if (extended->SubFormat != KSDATAFORMAT_SUBTYPE_PCM && !format->floating)
      return false;
    format->valid_bits = extended->Samples.wValidBitsPerSample;
  } else if (wave->wFormatTag != WAVE_FORMAT_PCM) {
    return false;
  }
  return (format->floating && format->bits == 32) ||
         (!format->floating && (format->bits == 16 || format->bits == 24 ||
                                format->bits == 32));
}

float Decode(const BYTE* data, const Format& format) {
  if (format.floating) return *reinterpret_cast<const float*>(data);
  if (format.bits == 16)
    return static_cast<float>(*reinterpret_cast<const int16_t*>(data)) / 32768.0f;
  if (format.bits == 24) {
    int32_t sample = static_cast<int32_t>(data[0]) |
                     (static_cast<int32_t>(data[1]) << 8) |
                     (static_cast<int32_t>(data[2]) << 16);
    if (sample & 0x800000) sample |= ~0xffffff;
    return static_cast<float>(sample) / 8388608.0f;
  }
  return static_cast<float>(*reinterpret_cast<const int32_t*>(data)) / 2147483648.0f;
}

void Encode(BYTE* data, const Format& format, float value) {
  value = std::clamp(value, -1.0f, 1.0f);
  if (format.floating) {
    *reinterpret_cast<float*>(data) = value;
  } else if (format.bits == 16) {
    *reinterpret_cast<int16_t*>(data) = static_cast<int16_t>(
        std::lrint(value * (value < 0 ? 32768.0f : 32767.0f)));
  } else if (format.bits == 24) {
    const auto sample = static_cast<int32_t>(
        std::lrint(value * (value < 0 ? 8388608.0f : 8388607.0f)));
    data[0] = static_cast<BYTE>(sample);
    data[1] = static_cast<BYTE>(sample >> 8);
    data[2] = static_cast<BYTE>(sample >> 16);
  } else {
    *reinterpret_cast<int32_t*>(data) = static_cast<int32_t>(
        std::llround(static_cast<double>(value) *
                     (value < 0 ? 2147483648.0 : 2147483647.0)));
  }
}

std::wstring HResultMessage(HRESULT value) {
  wchar_t buffer[32];
  swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(value));
  return buffer;
}

struct Stream {
  ComPtr<IAudioClient> client;
  WAVEFORMATEX* wave = nullptr;
  Format format;
  UINT32 frames = 0;
  ~Stream() {
    if (client) client->Stop();
    if (wave) CoTaskMemFree(wave);
  }
};

HRESULT OpenStream(IMMDeviceEnumerator* enumerator, const std::wstring& id,
                   Stream* stream, bool communications) {
  ComPtr<IMMDevice> device;
  HRESULT hr = enumerator->GetDevice(id.c_str(), &device);
  if (FAILED(hr)) return hr;
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(stream->client.GetAddressOf()));
  if (FAILED(hr)) return hr;
  if (communications) {
    ComPtr<IAudioClient2> communication_client;
    hr = stream->client.As(&communication_client);
    if (FAILED(hr)) return hr;
    AudioClientProperties properties{};
    properties.cbSize = sizeof(properties);
    properties.eCategory = AudioCategory_Communications;
    hr = communication_client->SetClientProperties(&properties);
    if (FAILED(hr)) return hr;
  }
  hr = stream->client->GetMixFormat(&stream->wave);
  if (FAILED(hr) || !Describe(stream->wave, &stream->format))
    return FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
  hr = stream->client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0,
                                  stream->wave, nullptr);
  if (FAILED(hr)) return hr;
  return stream->client->GetBufferSize(&stream->frames);
}

}  // namespace

std::wstring InspectAudioDevices() {
  std::wostringstream report;
  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (FAILED(hr)) return L"Audio enumeration failed: " + HResultMessage(hr);
  ComPtr<IMMDeviceCollection> devices;
  hr = enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, &devices);
  if (FAILED(hr)) return L"Audio enumeration failed: " + HResultMessage(hr);
  UINT count = 0;
  devices->GetCount(&count);
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    if (FAILED(devices->Item(i, &device))) continue;
    ComPtr<IPropertyStore> properties;
    PROPVARIANT name;
    PropVariantInit(&name);
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
        SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
      report << name.pwszVal;
    }
    PropVariantClear(&name);
    DWORD state = 0;
    device->GetState(&state);
    report << L" | state=" << state;
    if (state == DEVICE_STATE_ACTIVE) {
      ComPtr<IAudioEndpointVolume> volume;
      if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
          reinterpret_cast<void**>(volume.GetAddressOf())))) {
        BOOL muted = FALSE;
        float level = 0;
        volume->GetMute(&muted);
        volume->GetMasterVolumeLevelScalar(&level);
        report << L" mute=" << muted << L" volume=" << level;
      }
      ComPtr<IAudioClient> client;
      hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
          reinterpret_cast<void**>(client.GetAddressOf()));
      if (SUCCEEDED(hr)) {
        WAVEFORMATEX* wave = nullptr;
        hr = client->GetMixFormat(&wave);
        if (SUCCEEDED(hr)) {
          report << L" rate=" << wave->nSamplesPerSec << L" channels=" << wave->nChannels
                 << L" bits=" << wave->wBitsPerSample << L" tag=" << wave->wFormatTag;
          CoTaskMemFree(wave);
        }
      }
      if (FAILED(hr)) report << L" audio-error=" << HResultMessage(hr);
    }
    report << L"\n";
  }
  return report.str();
}

AudioBridge::~AudioBridge() { Stop(); }

std::wstring InspectAudioEndpoint(const std::wstring& id) {
  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (FAILED(hr)) return L"Enumerator: " + HResultMessage(hr);
  ComPtr<IMMDevice> device;
  hr = enumerator->GetDevice(id.c_str(), &device);
  if (FAILED(hr)) return L"GetDevice: " + HResultMessage(hr);
  DWORD state = 0;
  device->GetState(&state);
  Stream stream;
  hr = OpenStream(enumerator.Get(), id, &stream, true);
  return L"Device state=" + std::to_wstring(state) + L"; open communications stream=" +
      HResultMessage(hr) + L"; rate=" + std::to_wstring(stream.format.rate) +
      L"; channels=" + std::to_wstring(stream.format.channels);
}

void AudioBridge::Start(const std::wstring& phone_capture,
                        const std::wstring& pc_output,
                        const std::wstring& pc_input,
                        const std::wstring& phone_render) {
  Stop();
  stop_ = false;
  microphone_only_ = false;
  incoming_ready_ = false;
  outgoing_ready_ = false;
  microphone_peak_ = 0;
  microphone_frames_ = 0;
  SetError(L"");
  incoming_ = std::thread([this, phone_capture, pc_output] {
    Run(phone_capture, pc_output, incoming_ready_);
  });
  outgoing_ = std::thread([this, pc_input, phone_render] {
    Run(pc_input, phone_render, outgoing_ready_);
  });
}

void AudioBridge::StartMicrophone(const std::wstring& pc_input,
                                   const std::wstring& phone_render) {
  Stop();
  microphone_only_ = true;
  microphone_peak_ = 0;
  microphone_frames_ = 0;
  SetError(L"");
  stop_ = false;
  outgoing_ = std::thread([this, pc_input, phone_render] {
    Run(pc_input, phone_render, outgoing_ready_);
  });
}

void AudioBridge::Stop() {
  stop_ = true;
  if (incoming_.joinable()) incoming_.join();
  if (outgoing_.joinable()) outgoing_.join();
  incoming_ready_ = false;
  outgoing_ready_ = false;
}

void AudioBridge::StartTest(const std::wstring& pc_input,
                            const std::wstring& pc_output) {
  Stop();
  SetError(L"");
  peak_ = 0;
  microphone_only_ = false;
  stop_ = false;
  outgoing_ready_ = true;
  incoming_ = std::thread([this, pc_input, pc_output] {
    Run(pc_input, pc_output, incoming_ready_, true);
  });
}

bool AudioBridge::active() const {
  return !stop_ && outgoing_ready_ && (microphone_only_ || incoming_ready_);
}

std::wstring AudioBridge::error() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return error_;
}

void AudioBridge::SetError(const std::wstring& value) {
  std::lock_guard<std::mutex> lock(error_mutex_);
  error_ = value;
}

void AudioBridge::Run(const std::wstring& capture_id,
                      const std::wstring& render_id,
                      std::atomic<bool>& ready, bool test) {
  const bool uplink = !test && &ready == &outgoing_ready_;
  const std::wstring direction = test ? L"Local audio test" : uplink
      ? L"Microphone -> iPhone" : L"iPhone -> headphones";
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com)) {
    SetError(direction + L": cannot initialize Windows audio (" + HResultMessage(com) + L").");
    stop_ = true;
    return;
  }
  {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    Stream capture;
    Stream render;
    std::wstring stage = L"create audio enumerator";
    if (SUCCEEDED(hr)) { stage = L"open capture device"; hr = OpenStream(enumerator.Get(), capture_id, &capture, !test); }
    if (SUCCEEDED(hr)) { stage = L"open playback device"; hr = OpenStream(enumerator.Get(), render_id, &render, !test); }
    ComPtr<IAudioCaptureClient> capture_service;
    ComPtr<IAudioRenderClient> render_service;
    if (SUCCEEDED(hr)) { stage = L"get capture service"; hr = capture.client->GetService(IID_PPV_ARGS(&capture_service)); }
    if (SUCCEEDED(hr)) { stage = L"get playback service"; hr = render.client->GetService(IID_PPV_ARGS(&render_service)); }
    if (SUCCEEDED(hr)) { stage = L"start playback"; hr = render.client->Start(); }
    if (SUCCEEDED(hr)) { stage = L"start capture"; hr = capture.client->Start(); }
    if (FAILED(hr)) {
      SetError(direction + L": failed to " + stage + L" (" + HResultMessage(hr) + L").");
      stop_ = true;
    } else {
      // Start() can succeed on an idle HF endpoint whose buffer never drains.
      // Report readiness only after capture and playback actually progress.
      ready = false;
      const auto started = std::chrono::steady_clock::now();
      auto last_capture = started;
      auto last_render = started;
      bool captured = false;
      uint64_t submitted = 0;
      std::deque<float> samples;
      double position = 0.0;
      const double step = static_cast<double>(capture.format.rate) /
                          static_cast<double>(render.format.rate);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!stop_) {
        if (test && std::chrono::steady_clock::now() >= deadline) break;
        UINT32 pending = 0;
        hr = capture_service->GetNextPacketSize(&pending);
        if (FAILED(hr)) break;
        while (pending > 0) {
          BYTE* data = nullptr;
          UINT32 frames = 0;
          DWORD flags = 0;
          hr = capture_service->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
          if (FAILED(hr)) break;
          float packet_peak = 0;
          for (UINT32 frame = 0; frame < frames; ++frame) {
            float mono = 0;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
              const BYTE* base = data + frame * capture.format.frame_bytes;
              for (UINT channel = 0; channel < capture.format.channels; ++channel)
                mono += Decode(base + channel * capture.format.bits / 8,
                               capture.format);
              mono /= capture.format.channels;
            }
            if (test && std::isfinite(mono) && std::abs(mono) > peak_)
              peak_ = std::abs(mono);
            if (std::isfinite(mono)) packet_peak = std::max(packet_peak, std::abs(mono));
            samples.push_back(mono);
          }
          hr = capture_service->ReleaseBuffer(frames);
          if (FAILED(hr)) break;
          if (frames > 0) {
            captured = true;
            last_capture = std::chrono::steady_clock::now();
            if (uplink) microphone_peak_ = std::max(packet_peak, microphone_peak_.load() * 0.95f);
          }
          hr = capture_service->GetNextPacketSize(&pending);
          if (FAILED(hr)) break;
        }
        if (FAILED(hr)) break;

        // Bound latency if the output cannot keep up with the input.
        const size_t max_samples = capture.format.rate / 5;
        while (samples.size() > max_samples) samples.pop_front();
        UINT32 padding = 0;
        hr = render.client->GetCurrentPadding(&padding);
        if (FAILED(hr)) break;
        UINT32 free_frames = render.frames - padding;
        if (free_frames > 0) {
          BYTE* output = nullptr;
          hr = render_service->GetBuffer(free_frames, &output);
          if (FAILED(hr)) break;
          for (UINT32 frame = 0; frame < free_frames; ++frame) {
            float sample = 0;
            // Consume any remaining downsampling debt before interpolation.
            while (position >= 1.0 && !samples.empty()) {
              samples.pop_front();
              position -= 1.0;
            }
            if (samples.size() >= 2 && position < 1.0) {
              sample = samples[0] +
                       (samples[1] - samples[0]) * static_cast<float>(position);
              position += step;
              while (position >= 1.0 && samples.size() > 1) {
                samples.pop_front();
                position -= 1.0;
              }
            }
            BYTE* base = output + frame * render.format.frame_bytes;
            for (UINT channel = 0; channel < render.format.channels; ++channel)
              Encode(base + channel * render.format.bits / 8,
                     render.format, sample);
          }
          hr = render_service->ReleaseBuffer(free_frames, 0);
          if (FAILED(hr)) break;
          submitted += free_frames;
          last_render = std::chrono::steady_clock::now();
          if (uplink) microphone_frames_ += free_frames;
        }
        ready = captured && submitted > render.frames;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_capture > std::chrono::seconds(3) ||
            now - last_render > std::chrono::seconds(3)) {
          SetError(direction +
              L": audio stopped progressing. " +
              (test ? std::wstring(L"Check the selected Windows audio devices.")
                    : microphone_only_
                    ? std::wstring(L"No progressing Bluetooth microphone route. Start recording on iPhone and select this PC in Audio Input if offered. Retrying automatically; Windows cannot force the iPhone app to select this input.")
                    : std::wstring(L"Select this PC during the iPhone call and check Control Center > app controls > Audio Input. Retrying automatically.")));
          stop_ = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (FAILED(hr) && !stop_) {
        SetError(direction + L": stream stopped (" + HResultMessage(hr) + L").");
        stop_ = true;
      }
      ready = false;
      if (uplink) microphone_peak_ = 0;
    }
  }
  CoUninitialize();
}
