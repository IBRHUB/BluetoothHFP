#include "hfp_controller.h"

#include <windows.h>
#include <appmodel.h>
#include <bluetoothapis.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <setupapi.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#include "utils.h"

using Microsoft::WRL::ComPtr;

namespace {

std::wstring LoadDevice(const wchar_t* name) {
  wchar_t value[2048]{};
  DWORD bytes = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\BluetoothHFP", name,
                   RRF_RT_REG_SZ, nullptr, value, &bytes) != ERROR_SUCCESS) return L"";
  return value;
}

void SaveDevice(const wchar_t* name, const std::wstring& id) {
  RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\BluetoothHFP", name, REG_SZ,
                 id.c_str(), static_cast<DWORD>((id.size() + 1) * sizeof(wchar_t)));
}

struct Device {
  std::wstring id;
  std::wstring name;
  bool connected = false;
};

std::wstring FromUtf8(const std::string& value) {
  if (value.empty()) return L"";
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
  if (!length) return L"";
  std::wstring output(length, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), output.data(), length);
  return output;
}

std::wstring Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  return value;
}

std::wstring Address(BLUETOOTH_ADDRESS address) {
  wchar_t result[13];
  swprintf_s(result, L"%012llX", address.ullLong);
  return result;
}

bool IsPhone(const BLUETOOTH_DEVICE_INFO& info) {
  const auto major = (info.ulClassofDevice >> 8) & 0x1f;
  const std::wstring name = Lower(info.szName);
  return major == 2 || name.find(L"iphone") != std::wstring::npos;
}

std::vector<Device> Phones() {
  std::vector<Device> result;
  BLUETOOTH_DEVICE_SEARCH_PARAMS search = {};
  search.dwSize = sizeof(search);
  search.fReturnAuthenticated = TRUE;
  search.fReturnRemembered = TRUE;
  search.fReturnConnected = TRUE;
  BLUETOOTH_DEVICE_INFO info = {};
  info.dwSize = sizeof(info);
  HBLUETOOTH_DEVICE_FIND find = BluetoothFindFirstDevice(&search, &info);
  if (!find) return result;
  do {
    if (IsPhone(info) && info.fAuthenticated) {
      result.push_back({Address(info.Address), info.szName, info.fConnected != FALSE});
    }
    info = {};
    info.dwSize = sizeof(info);
  } while (BluetoothFindNextDevice(find, &info));
  BluetoothFindDeviceClose(find);
  return result;
}

std::vector<Device> Endpoints(EDataFlow flow) {
  std::vector<Device> result;
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator)))) return result;
  ComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE,
                                             &collection))) return result;
  UINT count = 0;
  collection->GetCount(&count);
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> endpoint;
    if (FAILED(collection->Item(i, &endpoint))) continue;
    LPWSTR id = nullptr;
    if (FAILED(endpoint->GetId(&id))) continue;
    ComPtr<IPropertyStore> properties;
    PROPVARIANT name;
    PropVariantInit(&name);
    if (SUCCEEDED(endpoint->OpenPropertyStore(STGM_READ, &properties)) &&
        SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
        name.vt == VT_LPWSTR) {
      result.push_back({id, name.pwszVal, false});
    }
    PropVariantClear(&name);
    CoTaskMemFree(id);
  }
  // Windows can omit phone-side HF endpoints from EnumAudioEndpoints even
  // while Phone Link is using them. Their present AudioEndpoint PnP instances
  // carry valid MMDevice IDs which GetDevice can open through the public API.
  // Do not enable devices or modify driver/registry properties to reveal them.
  constexpr GUID audio_endpoint_class = {0xc166523c, 0xfe0c, 0x4a94,
      {0xa5, 0x86, 0xf1, 0xa8, 0x0c, 0xfb, 0xbf, 0x3e}};
  HDEVINFO pnp = SetupDiGetClassDevsW(&audio_endpoint_class, nullptr, nullptr, DIGCF_PRESENT);
  if (pnp == INVALID_HANDLE_VALUE) return result;
  SP_DEVINFO_DATA info{};
  info.cbSize = sizeof(info);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(pnp, i, &info); ++i) {
    wchar_t instance[512]{};
    if (!SetupDiGetDeviceInstanceIdW(pnp, &info, instance, 512, nullptr)) continue;
    constexpr wchar_t prefix[] = L"SWD\\MMDEVAPI\\";
    constexpr size_t prefix_length = (sizeof(prefix) / sizeof(wchar_t)) - 1;
    if (_wcsnicmp(instance, prefix, prefix_length) != 0) continue;
    ComPtr<IMMDevice> endpoint;
    if (FAILED(enumerator->GetDevice(instance + prefix_length, &endpoint))) continue;
    DWORD state = 0;
    if (FAILED(endpoint->GetState(&state)) || state != DEVICE_STATE_ACTIVE) continue;
    ComPtr<IMMEndpoint> flow_info;
    EDataFlow endpoint_flow = eAll;
    if (FAILED(endpoint.As(&flow_info)) || FAILED(flow_info->GetDataFlow(&endpoint_flow)) ||
        endpoint_flow != flow) continue;
    ComPtr<IPropertyStore> properties;
    PROPVARIANT name;
    PropVariantInit(&name);
    if (SUCCEEDED(endpoint->OpenPropertyStore(STGM_READ, &properties)) &&
        SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR &&
        Lower(name.pwszVal).find(L"hf audio") != std::wstring::npos) {
      LPWSTR id = nullptr;
      if (SUCCEEDED(endpoint->GetId(&id))) {
        const bool duplicate = std::any_of(result.begin(), result.end(),
            [&](const Device& device) { return _wcsicmp(device.id.c_str(), id) == 0; });
        if (!duplicate) result.push_back({id, name.pwszVal, false});
        CoTaskMemFree(id);
      }
    }
    PropVariantClear(&name);
  }
  SetupDiDestroyDeviceInfoList(pnp);
  return result;
}

bool Contains(const std::vector<Device>& devices, const std::wstring& id) {
  return std::any_of(devices.begin(), devices.end(),
                     [&](const Device& device) { return device.id == id; });
}

std::vector<Device> PcEndpoints(const std::vector<Device>& endpoints) {
  std::vector<Device> result;
  for (const auto& endpoint : endpoints) {
    // Windows labels the phone-side endpoints "Hands-Free HF Audio". PC
    // Bluetooth headsets are usually "Hands-Free AG Audio" and stay eligible.
    const auto name = Lower(endpoint.name);
    if (name.find(L"hf audio") == std::wstring::npos &&
        name.find(L"a2dp snk") == std::wstring::npos)
      result.push_back(endpoint);
  }
  return result;
}

std::wstring DefaultId(EDataFlow flow) {
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator)))) return L"";
  ComPtr<IMMDevice> endpoint;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eCommunications,
                                                  &endpoint))) return L"";
  LPWSTR id = nullptr;
  if (FAILED(endpoint->GetId(&id))) return L"";
  std::wstring result(id);
  CoTaskMemFree(id);
  return result;
}

std::wstring PhoneEndpoint(const std::vector<Device>& endpoints,
                           const std::wstring& phone_name,
                           bool only_phone) {
  const std::wstring phone = Lower(phone_name);
  for (const auto& endpoint : endpoints) {
    const std::wstring name = Lower(endpoint.name);
    if (name.find(phone) != std::wstring::npos &&
        (name.find(L"hands-free") != std::wstring::npos ||
         name.find(L"hf audio") != std::wstring::npos)) return endpoint.id;
  }
  // Some drivers omit the friendly phone name. This is safe only when there is
  // one paired phone selected and exactly one HF Audio endpoint of this kind.
  if (only_phone) {
    std::wstring candidate;
    for (const auto& endpoint : endpoints) {
      const std::wstring name = Lower(endpoint.name);
      if (name.find(L"hf audio") == std::wstring::npos) continue;
      if (!candidate.empty()) return L"";
      candidate = endpoint.id;
    }
    return candidate;
  }
  return L"";
}

flutter::EncodableList ToList(const std::vector<Device>& devices,
                               const std::wstring& omit = L"") {
  flutter::EncodableList list;
  for (const auto& device : devices) {
    if (device.id == omit) continue;
    list.push_back(flutter::EncodableMap{
        {flutter::EncodableValue("id"),
         flutter::EncodableValue(Utf8FromUtf16(device.id.c_str()))},
        {flutter::EncodableValue("name"),
         flutter::EncodableValue(Utf8FromUtf16(device.name.c_str()))},
    });
  }
  return list;
}

flutter::EncodableValue NullableId(const std::wstring& value) {
  return value.empty() ? flutter::EncodableValue() :
      flutter::EncodableValue(Utf8FromUtf16(value.c_str()));
}

}  // namespace

HfpController::HfpController()
    : input_id_(LoadDevice(L"Input")), output_id_(LoadDevice(L"Output")),
      wired_capture_(LoadDevice(L"WiredCapture")), wired_render_(LoadDevice(L"WiredRender")) {}

flutter::EncodableValue HfpController::Snapshot() {
  const auto phones = wired_mode_ ? std::vector<Device>{} : Phones();
  const auto captures = Endpoints(eCapture);
  const auto renders = Endpoints(eRender);
  const auto pc_captures = PcEndpoints(captures);
  const auto pc_renders = PcEndpoints(renders);
  if (input_id_.empty()) input_id_ = DefaultId(eCapture);
  if (output_id_.empty()) output_id_ = DefaultId(eRender);
  if (!wired_mode_ && !Contains(pc_captures, input_id_))
    input_id_ = pc_captures.empty() ? L"" : pc_captures.front().id;
  if (!wired_mode_ && !Contains(pc_renders, output_id_))
    output_id_ = pc_renders.empty() ? L"" : pc_renders.front().id;

  Device selected;
  for (const auto& phone : phones) {
    if (phone.id == phone_id_) selected = phone;
  }
  if (!phone_id_.empty() && selected.id.empty()) {
    phone_id_.clear();
    transport_.Select(L"");
  }

  std::wstring phone_capture;
  std::wstring phone_render;
  if (selected.connected) {
    phone_capture = PhoneEndpoint(captures, selected.name, phones.size() == 1);
    phone_render = PhoneEndpoint(renders, selected.name, phones.size() == 1);
  }
  std::wstring key;
  if (voice_mode_ && selected.connected && !input_id_.empty() && !phone_render.empty()) {
    key = L"voice|" + input_id_ + L"|" + phone_render;
  } else if (!voice_mode_ && selected.connected && !input_id_.empty() && !output_id_.empty() &&
      !phone_capture.empty() && !phone_render.empty()) {
    key = phone_capture + L"|" + output_id_ + L"|" + input_id_ + L"|" +
          phone_render;
  }
  const auto now = std::chrono::steady_clock::now();
  const bool wired_available = Contains(pc_captures, wired_capture_) &&
      Contains(pc_renders, wired_render_) && Contains(pc_captures, input_id_) &&
      Contains(pc_renders, output_id_) && input_id_ != wired_capture_ &&
      output_id_ != wired_render_;
  if (wired_mode_) {
    phone_capture = wired_capture_;
    phone_render = wired_render_;
    key.clear();
    if (wired_running_ && !wired_available) wired_running_ = false;
    if (wired_running_) key = L"wired|" + phone_capture + L"|" + output_id_ +
        L"|" + input_id_ + L"|" + phone_render;
  }
  const bool testing = now < test_until_;
  if (testing) key.clear();
  if (key != route_key_ || (!key.empty() && !bridge_.active() &&
                           !bridge_.error().empty() && now >= retry_at_)) {
    bridge_.Stop();
    route_key_ = key;
    if (!key.empty()) {
      if (voice_mode_) bridge_.StartMicrophone(input_id_, phone_render);
      else bridge_.Start(phone_capture, output_id_, input_id_, phone_render, wired_mode_);
    }
    retry_at_ = now + std::chrono::seconds(10);
  }

  std::wstring message;
  if (bridge_.active())
    message = L"Windows audio streams are progressing. Confirm the other caller hears your PC microphone.";
  else if (selected.id.empty())
    message = L"Pair your iPhone in Windows Bluetooth settings, then select it.";
  else if (!selected.connected)
    message = L"Waiting for the iPhone Bluetooth connection.";
  else if (input_id_.empty() || output_id_.empty())
    message = L"Select a PC microphone and output device.";
  else if (phone_capture.empty() || phone_render.empty())
    message = L"This app is not routing call audio. Phone Link may be handling calls; no usable phone audio endpoints are currently exposed to this app.";
  else if (!bridge_.error().empty()) message = bridge_.error();
  else message = L"Opening call audio…";

  if (voice_mode_) {
    if (testing) message = L"Bluetooth microphone paused during the local audio test.";
    else if (selected.id.empty()) message = L"Select a paired iPhone for the Bluetooth microphone experiment.";
    else if (!selected.connected) message = L"Waiting for the iPhone Bluetooth connection.";
    else if (input_id_.empty()) message = L"Select a PC microphone.";
    else if (phone_render.empty()) message = L"No Bluetooth microphone endpoint is available. Start recording on iPhone and select this PC in Audio Input if listed. Windows cannot force it to appear.";
    else if (!bridge_.error().empty()) message = bridge_.error();
    else if (bridge_.active()) message = L"Windows microphone buffers are progressing. This does not confirm iPhone reception: play back a test recording and verify the source by muting the PC microphone.";
    else message = L"Opening Bluetooth microphone without a call. Waiting for audio buffer progress.";
  }

  // Exclude the selected phone endpoints from PC input/output choices.
  const auto transport = transport_.Status();
  UINT32 package_length = 0;
  const bool packaged = GetCurrentPackageFullName(&package_length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
  std::wstring microphone_message;
  if (bridge_.microphone_active()) {
    microphone_message = bridge_.microphone_peak() > 0.001
        ? L"Microphone signal captured; audio buffers are being sent to the iPhone call endpoint. Confirm the other caller hears you."
        : L"The iPhone call endpoint is open, but no microphone signal has been detected. Check mute and the selected Input.";
  } else if (!bridge_.error().empty() && !route_key_.empty()) microphone_message = bridge_.error();
  else if (selected.id.empty()) microphone_message = L"Select your iPhone to send microphone audio during a call.";
  else if (phone_render.empty()) microphone_message = L"No active iPhone call uplink is exposed to this app. Media audio has no microphone path. Transfer an active call to this PC.";
  else microphone_message = L"Opening the microphone path to the iPhone call endpoint.";
  if (voice_mode_) microphone_message = message;
  if (wired_mode_) {
    if (!wired_available) message = L"Select four available, separate endpoints: PC microphone, headphones, From phone and To phone. A compatible audio interface is required; a charging cable alone is insufficient.";
    else if (!wired_running_) message = L"Wired bridge stopped. Press Start wired audio when the interface is connected to your phone.";
    else if (!bridge_.error().empty()) message = bridge_.error();
    else if (bridge_.active()) message = L"Windows audio streams are progressing. Verify a phone recording by muting the PC microphone; phone reception is not yet confirmed.";
    else message = L"Opening wired audio streams...";
    microphone_message = message;
  }
  std::wstring test_message = L"Test your microphone through the selected headphones for 5 seconds.";
  if (!test_.error().empty()) test_message = test_.error();
  else if (testing) test_message = L"Speak now: your microphone plays through the selected output (5 seconds).";
  else if (test_until_ != std::chrono::steady_clock::time_point{})
    test_message = test_.peak() > 0.001
        ? L"Test finished: microphone signal received. Did you hear yourself in the headphones?"
        : L"Test finished: no microphone signal detected. Check Input, mute, and Windows microphone permission.";
  flutter::EncodableMap result = {
      {flutter::EncodableValue("wiredMode"), flutter::EncodableValue(wired_mode_)},
      {flutter::EncodableValue("wiredRunning"), flutter::EncodableValue(wired_running_)},
      {flutter::EncodableValue("wiredAvailable"), flutter::EncodableValue(wired_available)},
      {flutter::EncodableValue("wiredCaptureId"), NullableId(wired_capture_)},
      {flutter::EncodableValue("wiredRenderId"), NullableId(wired_render_)},
      {flutter::EncodableValue("wiredInputs"), flutter::EncodableValue(ToList(pc_captures, input_id_))},
      {flutter::EncodableValue("wiredOutputs"), flutter::EncodableValue(ToList(pc_renders, output_id_))},
      {flutter::EncodableValue("voiceMode"), flutter::EncodableValue(voice_mode_)},
      {flutter::EncodableValue("appVersion"), flutter::EncodableValue(FLUTTER_VERSION)},
      {flutter::EncodableValue("microphoneActive"), flutter::EncodableValue(bridge_.microphone_active())},
      {flutter::EncodableValue("microphonePeak"), flutter::EncodableValue(bridge_.microphone_peak())},
      {flutter::EncodableValue("microphoneFrames"), flutter::EncodableValue(bridge_.microphone_frames())},
      {flutter::EncodableValue("microphoneMessage"), flutter::EncodableValue(Utf8FromUtf16(microphone_message.c_str()))},
      {flutter::EncodableValue("packaged"), flutter::EncodableValue(packaged)},
      {flutter::EncodableValue("mediaState"), flutter::EncodableValue(transport.media_state)},
      {flutter::EncodableValue("callsState"), flutter::EncodableValue(transport.calls_state)},
      {flutter::EncodableValue("testActive"), flutter::EncodableValue(testing)},
      {flutter::EncodableValue("testRouteActive"), flutter::EncodableValue(test_.active())},
      {flutter::EncodableValue("testPeak"), flutter::EncodableValue(
          test_until_ == std::chrono::steady_clock::time_point{} ? 0.0 : test_.peak())},
      {flutter::EncodableValue("testMessage"), flutter::EncodableValue(Utf8FromUtf16(test_message.c_str()))},
      {flutter::EncodableValue("mediaActive"), flutter::EncodableValue(transport.media_open)},
      {flutter::EncodableValue("mediaMessage"), flutter::EncodableValue(Utf8FromUtf16(transport.media_message.c_str()))},
      {flutter::EncodableValue("callsMessage"), flutter::EncodableValue(Utf8FromUtf16(transport.calls_message.c_str()))},
      {flutter::EncodableValue("phones"), flutter::EncodableValue(ToList(phones))},
      {flutter::EncodableValue("inputs"),
       flutter::EncodableValue(ToList(pc_captures, phone_capture))},
      {flutter::EncodableValue("outputs"),
       flutter::EncodableValue(ToList(pc_renders, phone_render))},
      {flutter::EncodableValue("phoneId"), NullableId(phone_id_)},
      {flutter::EncodableValue("phoneConnected"),
       flutter::EncodableValue(selected.connected)},
      {flutter::EncodableValue("inputId"), NullableId(input_id_)},
      {flutter::EncodableValue("outputId"), NullableId(output_id_)},
      {flutter::EncodableValue("routeActive"), flutter::EncodableValue(bridge_.active())},
      {flutter::EncodableValue("message"),
       flutter::EncodableValue(Utf8FromUtf16(message.c_str()))},
  };
  return flutter::EncodableValue(result);
}

std::wstring HfpController::SelectPhone(const std::string* id, bool connect_transport) {
  if (wired_mode_) return L"Switch to Bluetooth mode before selecting a phone.";
  const std::wstring target = id ? FromUtf8(*id) : L"";
  if (!target.empty() && !Contains(Phones(), target))
    return L"iPhone is no longer paired.";
  // BluetoothSetServiceState installs or removes a profile driver; it does not
  // connect a call. Use the phone transport API and keep the profile installed.
  phone_id_ = target;
  if (target.empty()) voice_mode_ = false;
  test_.Stop();
  test_until_ = {};
  if (connect_transport) transport_.Select(target);
  bridge_.Stop();
  route_key_.clear();
  return L"";
}

void HfpController::Reconnect() {
  if (wired_mode_) return;
  bridge_.Stop();
  route_key_.clear();
  transport_.Select(phone_id_);
}

void HfpController::SetVoiceMode(bool enabled) {
  if (wired_mode_) return;
  if (voice_mode_ == enabled) return;
  bridge_.Stop();
  test_.Stop();
  test_until_ = {};
  route_key_.clear();
  voice_mode_ = enabled;
}

std::wstring HfpController::TestAudio() {
  if (wired_running_) return L"Stop wired audio before testing the microphone.";
  if (bridge_.active()) return L"Stop call routing before testing local audio.";
  if (input_id_.empty() || output_id_.empty()) return L"Select a microphone and headphones first.";
  bridge_.Stop();
  route_key_.clear();
  test_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  test_.StartTest(input_id_, output_id_);
  return L"";
}

void HfpController::SetWiredMode(bool enabled) {
  bridge_.Stop();
  test_.Stop();
  test_until_ = {};
  route_key_.clear();
  voice_mode_ = false;
  wired_running_ = false;
  wired_mode_ = enabled;
  phone_id_.clear();
  transport_.Select(L"");
}

std::wstring HfpController::SelectWiredEndpoint(const std::string& id, bool capture) {
  const auto candidate = FromUtf8(id);
  if (!Contains(PcEndpoints(Endpoints(capture ? eCapture : eRender)), candidate))
    return L"Audio interface endpoint is unavailable.";
  if (candidate == (capture ? input_id_ : output_id_))
    return L"Choose an interface endpoint separate from the PC microphone and headphones.";
  SetWiredRunning(false);
  (capture ? wired_capture_ : wired_render_) = candidate;
  SaveDevice(capture ? L"WiredCapture" : L"WiredRender", candidate);
  return L"";
}

std::wstring HfpController::SetWiredRunning(bool enabled) {
  bridge_.Stop();
  route_key_.clear();
  wired_running_ = false;
  if (!enabled) return L"";
  if (!wired_mode_) return L"Select wired mode first.";
  const auto captures = PcEndpoints(Endpoints(eCapture));
  const auto renders = PcEndpoints(Endpoints(eRender));
  if (!Contains(captures, input_id_) || !Contains(captures, wired_capture_) ||
      !Contains(renders, output_id_) || !Contains(renders, wired_render_) ||
      input_id_ == wired_capture_ || output_id_ == wired_render_)
    return L"Select four available, separate PC and phone interface endpoints.";
  test_.Stop();
  test_until_ = {};
  wired_running_ = true;
  return L"";
}

std::wstring HfpController::SelectInput(const std::string& id) {
  const std::wstring candidate = FromUtf8(id);
  if (!Contains(PcEndpoints(Endpoints(eCapture)), candidate))
    return L"Microphone is unavailable.";
  input_id_ = candidate;
  if (wired_mode_) SetWiredRunning(false);
  SaveDevice(L"Input", candidate);
  test_.Stop();
  test_until_ = {};
  return L"";
}

std::wstring HfpController::SelectOutput(const std::string& id) {
  const std::wstring candidate = FromUtf8(id);
  if (!Contains(PcEndpoints(Endpoints(eRender)), candidate))
    return L"Output is unavailable.";
  output_id_ = candidate;
  if (wired_mode_) SetWiredRunning(false);
  SaveDevice(L"Output", candidate);
  test_.Stop();
  test_until_ = {};
  return L"";
}
