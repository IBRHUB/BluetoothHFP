#include "hfp_controller.h"

#include <windows.h>
#include <bluetoothapis.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#include "utils.h"

using Microsoft::WRL::ComPtr;

namespace {

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
    if (Lower(endpoint.name).find(L"hf audio") == std::wstring::npos)
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

flutter::EncodableValue HfpController::Snapshot() {
  const auto phones = Phones();
  const auto captures = Endpoints(eCapture);
  const auto renders = Endpoints(eRender);
  const auto pc_captures = PcEndpoints(captures);
  const auto pc_renders = PcEndpoints(renders);
  if (input_id_.empty()) input_id_ = DefaultId(eCapture);
  if (output_id_.empty()) output_id_ = DefaultId(eRender);
  if (!Contains(pc_captures, input_id_))
    input_id_ = pc_captures.empty() ? L"" : pc_captures.front().id;
  if (!Contains(pc_renders, output_id_))
    output_id_ = pc_renders.empty() ? L"" : pc_renders.front().id;

  Device selected;
  for (const auto& phone : phones) {
    if (phone.id == phone_id_) selected = phone;
  }
  if (!phone_id_.empty() && selected.id.empty()) {
    phone_id_.clear();
  }

  std::wstring phone_capture;
  std::wstring phone_render;
  if (selected.connected) {
    phone_capture = PhoneEndpoint(captures, selected.name, phones.size() == 1);
    phone_render = PhoneEndpoint(renders, selected.name, phones.size() == 1);
  }
  std::wstring key;
  if (selected.connected && !input_id_.empty() && !output_id_.empty() &&
      !phone_capture.empty() && !phone_render.empty()) {
    key = phone_capture + L"|" + output_id_ + L"|" + input_id_ + L"|" +
          phone_render;
  }
  if (key != route_key_) {
    bridge_.Stop();
    route_key_ = key;
    if (!key.empty()) bridge_.Start(phone_capture, output_id_, input_id_, phone_render);
  }

  std::wstring message;
  if (bridge_.active())
    message = L"Call audio is routed through your selected devices.";
  else if (selected.id.empty())
    message = L"Pair your iPhone in Windows Bluetooth settings, then select it.";
  else if (!selected.connected)
    message = L"Waiting for the iPhone Bluetooth connection.";
  else if (input_id_.empty() || output_id_.empty())
    message = L"Select a PC microphone and output device.";
  else if (phone_capture.empty() || phone_render.empty())
    message = L"Connected. Transfer an active iPhone call to this PC to enable HFP audio.";
  else if (!bridge_.error().empty()) message = bridge_.error();
  else message = L"Opening call audio…";

  // Exclude the selected phone endpoints from PC input/output choices.
  flutter::EncodableMap result = {
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

std::wstring HfpController::ConnectPhone(const std::string* id) {
  const std::wstring target = id ? FromUtf8(*id) : L"";
  if (!target.empty() && !Contains(Phones(), target))
    return L"iPhone is no longer paired.";
  if (phone_id_ == target) return L"";
  // BluetoothSetServiceState installs or removes a profile driver; it does not
  // connect a call. Keep Windows' HFP driver installed and only control this
  // app's audio route. The live Bluetooth and endpoint state is polled above.
  phone_id_ = target;
  bridge_.Stop();
  route_key_.clear();
  return L"";
}

std::wstring HfpController::SelectInput(const std::string& id) {
  const std::wstring candidate = FromUtf8(id);
  if (!Contains(PcEndpoints(Endpoints(eCapture)), candidate))
    return L"Microphone is unavailable.";
  input_id_ = candidate;
  return L"";
}

std::wstring HfpController::SelectOutput(const std::string& id) {
  const std::wstring candidate = FromUtf8(id);
  if (!Contains(PcEndpoints(Endpoints(eRender)), candidate))
    return L"Output is unavailable.";
  output_id_ = candidate;
  return L"";
}
