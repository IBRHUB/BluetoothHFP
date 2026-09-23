#ifndef RUNNER_AUDIO_BRIDGE_H_
#define RUNNER_AUDIO_BRIDGE_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>

std::wstring InspectAudioDevices();
std::wstring InspectAudioEndpoint(const std::wstring& id);

// A capture-to-render stream in each direction. Windows owns the Bluetooth SCO
// transport; this class only opens its HFP audio endpoints through WASAPI.
class AudioBridge {
 public:
  AudioBridge() = default;
  ~AudioBridge();
  AudioBridge(const AudioBridge&) = delete;
  AudioBridge& operator=(const AudioBridge&) = delete;

  void Start(const std::wstring& phone_capture,
             const std::wstring& pc_output,
             const std::wstring& pc_input,
             const std::wstring& phone_render);
  void Stop();
  void StartMicrophone(const std::wstring& pc_input,
                       const std::wstring& phone_render);
  void StartTest(const std::wstring& pc_input, const std::wstring& pc_output);
  bool active() const;
  double peak() const { return peak_.load(); }
  bool microphone_active() const { return outgoing_ready_ && !stop_; }
  double microphone_peak() const { return microphone_peak_.load(); }
  int64_t microphone_frames() const { return microphone_frames_.load(); }
  std::wstring error() const;

 private:
  void Run(const std::wstring& capture_id, const std::wstring& render_id,
           std::atomic<bool>& ready, bool test = false);
  void SetError(const std::wstring& value);

  std::atomic<bool> stop_{false};
  std::atomic<bool> microphone_only_{false};
  std::atomic<bool> incoming_ready_{false};
  std::atomic<bool> outgoing_ready_{false};
  std::atomic<float> peak_{0};
  std::atomic<float> microphone_peak_{0};
  std::atomic<int64_t> microphone_frames_{0};
  std::thread incoming_;
  std::thread outgoing_;
  mutable std::mutex error_mutex_;
  std::wstring error_;
};

#endif
