#include "audio_bridge.h"

#include <audioclient.h>
#include <ks.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <wrl/client.h>

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
                   Stream* stream) {
  ComPtr<IMMDevice> device;
  HRESULT hr = enumerator->GetDevice(id.c_str(), &device);
  if (FAILED(hr)) return hr;
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(stream->client.GetAddressOf()));
  if (FAILED(hr)) return hr;
  hr = stream->client->GetMixFormat(&stream->wave);
  if (FAILED(hr) || !Describe(stream->wave, &stream->format))
    return FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
  hr = stream->client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0,
                                  stream->wave, nullptr);
  if (FAILED(hr)) return hr;
  return stream->client->GetBufferSize(&stream->frames);
}

}  // namespace

AudioBridge::~AudioBridge() { Stop(); }

void AudioBridge::Start(const std::wstring& phone_capture,
                        const std::wstring& pc_output,
                        const std::wstring& pc_input,
                        const std::wstring& phone_render) {
  Stop();
  stop_ = false;
  incoming_ready_ = false;
  outgoing_ready_ = false;
  SetError(L"");
  incoming_ = std::thread([this, phone_capture, pc_output] {
    Run(phone_capture, pc_output, incoming_ready_);
  });
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
  stop_ = false;
  outgoing_ready_ = true;
  incoming_ = std::thread([this, pc_input, pc_output] {
    Run(pc_input, pc_output, incoming_ready_, true);
  });
}

bool AudioBridge::active() const { return incoming_ready_ && outgoing_ready_; }

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
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com)) {
    SetError(L"Cannot initialize Windows audio.");
    return;
  }
  {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    Stream capture;
    Stream render;
    if (SUCCEEDED(hr)) hr = OpenStream(enumerator.Get(), capture_id, &capture);
    if (SUCCEEDED(hr)) hr = OpenStream(enumerator.Get(), render_id, &render);
    ComPtr<IAudioCaptureClient> capture_service;
    ComPtr<IAudioRenderClient> render_service;
    if (SUCCEEDED(hr)) hr = capture.client->GetService(IID_PPV_ARGS(&capture_service));
    if (SUCCEEDED(hr)) hr = render.client->GetService(IID_PPV_ARGS(&render_service));
    if (SUCCEEDED(hr)) hr = render.client->Start();
    if (SUCCEEDED(hr)) hr = capture.client->Start();
    if (FAILED(hr)) {
      SetError(std::wstring(test ? L"Cannot open the selected microphone/output ("
                                : L"Cannot open a call audio endpoint (") +
               HResultMessage(hr) + L").");
      stop_ = true;
    } else {
      ready = true;
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
            samples.push_back(mono);
          }
          hr = capture_service->ReleaseBuffer(frames);
          if (FAILED(hr)) break;
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
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (FAILED(hr) && !stop_) {
        SetError(L"Call audio stopped (" + HResultMessage(hr) + L").");
        stop_ = true;
      }
      ready = false;
    }
  }
  CoUninitialize();
}
