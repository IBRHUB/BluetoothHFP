#include "flutter_window.h"

#include <optional>
#include <flutter/standard_method_codec.h>
#include <shellapi.h>
#include <dwmapi.h>

#include "flutter/generated_plugin_registrant.h"
#include "utils.h"

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }
  hfp_controller_ = std::make_unique<HfpController>();

  const BOOL dark = TRUE;
  const COLORREF caption = RGB(0, 0, 0);
  const COLORREF caption_text = RGB(255, 255, 255);
  DwmSetWindowAttribute(GetHandle(), 20, &dark, sizeof(dark));
  DwmSetWindowAttribute(GetHandle(), 35, &caption, sizeof(caption));
  DwmSetWindowAttribute(GetHandle(), 36, &caption_text, sizeof(caption_text));

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  channel_ = std::make_unique<flutter::MethodChannel<>>(
      flutter_controller_->engine()->messenger(), "bluetooth_hfp/windows",
      &flutter::StandardMethodCodec::GetInstance());
  channel_->SetMethodCallHandler(
      [this](const flutter::MethodCall<>& call,
             std::unique_ptr<flutter::MethodResult<>> result) {
        if (call.method_name() == "snapshot") {
          result->Success(hfp_controller_->Snapshot());
          return;
        }
        if (call.method_name() == "reconnect") {
          hfp_controller_->Reconnect();
          result->Success();
          return;
        }
        if (call.method_name() == "openBluetoothSettings" ||
            call.method_name() == "openSoundSettings" ||
            call.method_name() == "openCallPermissions" ||
            call.method_name() == "openPhoneLink") {
          const wchar_t* uri = L"ms-settings:bluetooth";
          if (call.method_name() == "openSoundSettings") uri = L"ms-settings:sound";
          if (call.method_name() == "openCallPermissions") uri = L"ms-settings:privacy-phonecalls";
          if (call.method_name() == "openPhoneLink") uri = L"ms-phone:";
          const auto launched = ShellExecuteW(nullptr, L"open",
              uri, nullptr, nullptr, SW_SHOWNORMAL);
          if (reinterpret_cast<INT_PTR>(launched) > 32) result->Success();
          else result->Error("windows_settings", "Cannot open the requested Windows app or settings.");
          return;
        }
        const auto* arguments = call.arguments();
        const auto* id = arguments ? std::get_if<std::string>(arguments) : nullptr;
        std::wstring error;
        if (call.method_name() == "testAudio") {
          error = hfp_controller_->TestAudio();
        } else if (call.method_name() == "selectPhone") {
          error = hfp_controller_->SelectPhone(id);
        } else if (call.method_name() == "selectInput" && id) {
          error = hfp_controller_->SelectInput(*id);
        } else if (call.method_name() == "selectOutput" && id) {
          error = hfp_controller_->SelectOutput(*id);
        } else {
          result->NotImplemented();
          return;
        }
        if (error.empty()) result->Success();
        else result->Error("windows_audio", Utf8FromUtf16(error.c_str()));
      });
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  channel_.reset();
  if (hfp_controller_) {
    hfp_controller_->Stop();
    hfp_controller_.reset();
  }
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
