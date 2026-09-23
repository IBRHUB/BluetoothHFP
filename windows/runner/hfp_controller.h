#ifndef RUNNER_HFP_CONTROLLER_H_
#define RUNNER_HFP_CONTROLLER_H_

#include <flutter/encodable_value.h>

#include <string>

#include "audio_bridge.h"

class HfpController {
 public:
  flutter::EncodableValue Snapshot();
  std::wstring ConnectPhone(const std::string* id);
  std::wstring SelectInput(const std::string& id);
  std::wstring SelectOutput(const std::string& id);
  void Stop() { bridge_.Stop(); }

 private:
  std::wstring phone_id_;
  std::wstring input_id_;
  std::wstring output_id_;
  std::wstring route_key_;
  AudioBridge bridge_;
};

#endif
