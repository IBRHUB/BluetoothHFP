#include "phone_transport.h"

#include <windows.h>
#include <winrt/Windows.ApplicationModel.Calls.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Audio.h>

#include <algorithm>
#include <chrono>
#include <cwctype>

using namespace winrt;
using namespace Windows::ApplicationModel::Calls;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Foundation;
using namespace Windows::Media::Audio;
using namespace std::chrono_literals;

namespace {
template <typename T>
auto Await(const T& operation, const std::atomic<bool>& stop,
           const std::atomic<unsigned long>& revision, unsigned long current) {
  const auto deadline = std::chrono::steady_clock::now() + 20s;
  while (operation.Status() == AsyncStatus::Started) {
    if (stop || revision != current) {
      operation.Cancel();
      throw hresult_canceled();
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      operation.Cancel();
      throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    std::this_thread::sleep_for(50ms);
  }
  if (stop || revision != current) throw hresult_canceled();
  return operation.GetResults();
}

std::wstring Upper(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
  return value;
}

std::wstring Failure(const hresult_error& error) {
  wchar_t code[24];
  swprintf_s(code, L" (0x%08X)", static_cast<unsigned int>(error.code().value));
  return std::wstring(error.message()) + code;
}

// Match a stable Bluetooth address or container, never a friendly name or the
// first device in a collection (two paired phones can have the same name).
DeviceInformation Match(const DeviceInformationCollection& devices,
                        const std::wstring& address, const guid& container) {
  for (const auto& device : devices) {
    if (Upper(std::wstring(device.Id())).find(address) != std::wstring::npos)
      return device;
    const auto property = device.Properties().TryLookup(L"System.Devices.ContainerId");
    if (container != guid{} && property &&
        unbox_value_or<guid>(property, guid{}) == container) return device;
  }
  return nullptr;
}
}  // namespace

PhoneTransport::PhoneTransport() : worker_([this] { Run(); }) {}
PhoneTransport::~PhoneTransport() { Stop(); }

void PhoneTransport::Stop() {
  stop_ = true;
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void PhoneTransport::Select(const std::wstring& address) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      status_ = {};
      status_.media_message = L"Bluetooth worker is stopped. Restart the app.";
      status_.calls_message = status_.media_message;
      status_.media_state = status_.calls_state = "error";
      return;
    }
    address_ = address;
    status_ = {};
    if (!address.empty()) {
      status_.media_message = L"Preparing the media receiver for this app.";
      status_.calls_message = L"Waiting for media setup before checking this app's call access.";
      status_.media_state = "connecting";
      status_.calls_state = "waiting";
      status_.media_since = status_.calls_since = std::chrono::steady_clock::now();
    }
    ++revision_;
  }
  wake_.notify_all();
}

TransportStatus PhoneTransport::Status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto result = status_;
  const auto now = std::chrono::steady_clock::now();
  // Also cover synchronous Windows calls: never leave the UI spinning forever
  // if Windows stalls before an async operation can even be returned.
  auto watchdog = [&](std::string& state, std::wstring& message,
                      std::chrono::steady_clock::time_point since) {
    const auto limit = state == "waiting" ? 100s : 25s;
    if ((state == "connecting" || state == "waiting") && now - since > limit) {
      state = "timeout";
      message = L"Windows did not respond in time. Last step: " + message +
                L" Restart this app if Reconnect does not recover.";
    }
  };
  watchdog(result.media_state, result.media_message, result.media_since);
  watchdog(result.calls_state, result.calls_message, result.calls_since);
  return result;
}

void PhoneTransport::Run() {
  try {
    init_apartment(apartment_type::multi_threaded);
    {
      AudioPlaybackConnection media{nullptr};
      PhoneLineTransportDevice calls{nullptr};
      bool registered_here = false;
      unsigned long handled = 0;
      auto cleanup = [&] {
        if (media) { try { media.Close(); } catch (...) {} media = nullptr; }
        // Only undo registrations created by this instance.
        if (calls && registered_here) {
          try { calls.UnregisterApp(); } catch (...) {}
        }
        calls = nullptr;
        registered_here = false;
      };
      while (!stop_) {
        std::wstring address;
        unsigned long current;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          wake_.wait_for(lock, 500ms, [&] { return stop_ || revision_ != handled; });
          if (stop_) break;
          current = revision_;
          address = address_;
        }
        if (current == handled) {
          if (media) {
            try {
              const bool opened = media.State() == AudioPlaybackConnectionState::Opened;
              std::lock_guard<std::mutex> lock(mutex_);
              if (revision_ == current) {
                status_.media_open = opened;
                status_.media_state = opened ? "connected" : "disconnected";
                status_.media_message = opened
                    ? L"Media connected. Play audio on iPhone; output follows Windows sound settings."
                    : L"Media idle/disconnected. Select this PC on iPhone or press Reconnect.";
              }
            } catch (const hresult_error& e) {
              std::lock_guard<std::mutex> lock(mutex_);
              if (revision_ == current) {
                status_.media_open = false;
                status_.media_state = "error";
                status_.media_message = L"Media: " + Failure(e);
              }
            }
          }
          continue;
        }
        handled = current;
        cleanup();
        if (address.empty()) continue;
        auto publish = [&](bool is_media, const std::wstring& message, bool success = false,
                           const std::string& state = "error") {
          std::lock_guard<std::mutex> lock(mutex_);
          if (revision_ != current || stop_) return;
          if (is_media) {
            status_.media_message = message;
            status_.media_open = success;
            status_.media_state = success ? "connected" : state;
            status_.media_since = std::chrono::steady_clock::now();
          } else {
            status_.calls_message = message;
            status_.calls_connected = success;
            status_.calls_state = success ? "connected" : state;
            status_.calls_since = std::chrono::steady_clock::now();
          }
        };
        guid container{};
        const auto properties = single_threaded_vector<hstring>({L"System.Devices.ContainerId"});
        try {
          publish(true, L"Resolving the selected Bluetooth device.", false, "connecting");
          auto bluetooth = Await(Windows::Devices::Bluetooth::BluetoothDevice::FromBluetoothAddressAsync(
              std::stoull(address, nullptr, 16)), stop_, revision_, current);
          if (bluetooth) {
            publish(true, L"Reading the phone's Windows device identity.", false, "connecting");
            auto info = Await(DeviceInformation::CreateFromIdAsync(bluetooth.DeviceId(), properties),
                              stop_, revision_, current);
            if (info) container = unbox_value_or<guid>(
                info.Properties().TryLookup(L"System.Devices.ContainerId"), guid{});
            bluetooth.Close();
          }
        } catch (const hresult_error&) {
          // Interface IDs can still match the exact radio address.
        }
        try {
          publish(true, L"Finding this phone's media receiver interface.", false, "connecting");
          auto devices = Await(DeviceInformation::FindAllAsync(
              AudioPlaybackConnection::GetDeviceSelector(), properties), stop_, revision_, current);
          auto device = Match(devices, address, container);
          if (!device) {
            publish(true, L"Windows exposes no media receiver interface for this phone. Bluetooth pairing may still be connected.", false, "unavailable");
          } else {
            media = AudioPlaybackConnection::TryCreateFromId(device.Id());
            if (!media) throw hresult_error(E_NOINTERFACE);
            publish(true, L"Enabling Windows media reception for this app.", false, "connecting");
            Await(media.StartAsync(), stop_, revision_, current);
            publish(true, L"Opening the media stream. Waiting for the iPhone (up to 20 seconds).", false, "connecting");
            const auto result = Await(media.OpenAsync(), stop_, revision_, current);
            if (result.Status() == AudioPlaybackConnectionOpenResultStatus::Success) {
              publish(true, L"Media connected. Output follows Windows sound settings.", true);
            } else {
              publish(true, L"Media could not connect (status " +
                  std::to_wstring(static_cast<int>(result.Status())) +
                  L"). Unlock iPhone, select this PC as audio output, then Reconnect.");
              media.Close();
              media = nullptr;
            }
          }
        } catch (const hresult_error& e) {
          publish(true, L"Media: " + Failure(e), false,
              e.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT) ? "timeout" : "error");
          if (media) { try { media.Close(); } catch (...) {} media = nullptr; }
        }
        if (stop_ || revision_ != current) continue;
        try {
          publish(false, L"Finding this phone's call interface for this app.", false, "connecting");
          auto devices = Await(DeviceInformation::FindAllAsync(
              PhoneLineTransportDevice::GetDeviceSelector(PhoneLineTransport::Bluetooth), properties),
              stop_, revision_, current);
          auto device = Match(devices, address, container);
          if (!device) {
            publish(false, L"Windows exposes no call interface to this app. Calls in Phone Link can work independently.", false, "unavailable");
          } else {
            calls = PhoneLineTransportDevice::FromId(device.Id());
            if (!calls) throw hresult_error(E_NOINTERFACE);
            publish(false, L"Checking whether Windows allows this app to manage calls.", false, "connecting");
            const auto access = Await(calls.RequestAccessAsync(), stop_, revision_, current);
            if (access != DeviceAccessStatus::Allowed) {
              publish(false, access == DeviceAccessStatus::DeniedByUser
                  ? L"Call permission is disabled for this app. Bluetooth and Phone Link are independent."
                  : L"Windows denied THIS APP access to calls. Bluetooth can remain connected and Phone Link can still work.",
                  false, "blocked");
            } else {
              publish(false, L"Registering this app for the phone's call transport.", false, "connecting");
              if (!calls.IsRegistered()) { calls.RegisterApp(); registered_here = true; }
              if (!calls.IsRegistered()) throw hresult_error(E_ACCESSDENIED,
                  L"Windows did not register the app for call transport");
              publish(false, L"Requesting call transport connection for this app (up to 20 seconds).", false, "connecting");
              const bool connected = Await(calls.ConnectAsync(), stop_, revision_, current);
              publish(false, connected
                  ? L"Call transport connected. Start a call and select this PC on iPhone."
                  : L"This app could not connect call transport. Phone Link may still handle your calls.", connected);
            }
          }
        } catch (const hresult_error& e) {
          publish(false, L"Calls in this app: " + Failure(e) +
              L". This does not describe Phone Link's connection.", false,
              e.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT) ? "timeout" : "error");
        }
      }
      cleanup();
    }
    uninit_apartment();
  } catch (const hresult_error& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.media_open = false;
    status_.calls_connected = false;
    status_.media_state = status_.calls_state = "error";
    status_.media_message = L"Windows Bluetooth initialization failed: " + Failure(e);
    status_.calls_message = status_.media_message;
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.media_open = false;
    status_.calls_connected = false;
    status_.media_state = status_.calls_state = "error";
    status_.media_message = L"Bluetooth worker failed. Restart the app.";
    status_.calls_message = status_.media_message;
  }
}
