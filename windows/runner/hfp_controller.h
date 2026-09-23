#ifndef RUNNER_HFP_CONTROLLER_H_
#define RUNNER_HFP_CONTROLLER_H_

#include <flutter/encodable_value.h>

#include <string>
#include <chrono>

#include "audio_bridge.h"
#include "phone_transport.h"

class HfpController {
 public:
  HfpController();
  flutter::EncodableValue Snapshot();
  std::wstring SelectPhone(const std::string* id, bool connect_transport = true);
  std::wstring SelectInput(const std::string& id);
  std::wstring SelectOutput(const std::string& id);
  void Reconnect();
  std::wstring TestAudio();
  void Stop() { test_.Stop(); transport_.Stop(); bridge_.Stop(); }

 private:
  std::wstring phone_id_;
  std::wstring input_id_;
  std::wstring output_id_;
  std::wstring route_key_;
  AudioBridge bridge_;
  AudioBridge test_;
  std::chrono::steady_clock::time_point test_until_{};
  PhoneTransport transport_;
  std::chrono::steady_clock::time_point retry_at_{};
};

#endif
