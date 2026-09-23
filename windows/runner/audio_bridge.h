#ifndef RUNNER_AUDIO_BRIDGE_H_
#define RUNNER_AUDIO_BRIDGE_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

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
  bool active() const;
  std::wstring error() const;

 private:
  void Run(const std::wstring& capture_id, const std::wstring& render_id,
           std::atomic<bool>& ready);
  void SetError(const std::wstring& value);

  std::atomic<bool> stop_{false};
  std::atomic<bool> incoming_ready_{false};
  std::atomic<bool> outgoing_ready_{false};
  std::thread incoming_;
  std::thread outgoing_;
  mutable std::mutex error_mutex_;
  std::wstring error_;
};

#endif
