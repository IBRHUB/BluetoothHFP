#include "flutter_window.h"
#include "platform_bridge.h"
#include <flutter/standard_method_codec.h>
#include <optional>
#include <shellapi.h>
#include "flutter/generated_plugin_registrant.h"
FlutterWindow::FlutterWindow(const flutter::DartProject& project) : project_(project) {}
FlutterWindow::~FlutterWindow() {}
bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) return false;
  RECT frame = GetClientArea();
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(frame.right-frame.left, frame.bottom-frame.top, project_);
  if (!flutter_controller_->engine() || !flutter_controller_->view()) return false;
  RegisterPlugins(flutter_controller_->engine());
  channel_ = std::make_unique<flutter::MethodChannel<>>(flutter_controller_->engine()->messenger(), "bluetooth_hfp/platform", &flutter::StandardMethodCodec::GetInstance());
  channel_->SetMethodCallHandler([this](const flutter::MethodCall<>& call, std::unique_ptr<flutter::MethodResult<>> result) {
    if (call.method_name() == "endpoints") { result->Success(AudioEndpoints()); return; }
    if (call.method_name() == "driverPoll") { result->Success(flutter::EncodableValue(DriverOperationStatus())); return; }
    if (call.method_name() == "driverStart") {
      const auto* action = call.arguments() ? std::get_if<std::string>(call.arguments()) : nullptr;
      if (action && BeginDriverOperation(*action, GetHandle())) result->Success();
      else result->Error("driver", "Administrator operation was cancelled, unavailable, or already running.");
      return;
    }
    if (call.method_name() == "close") { allow_close_ = true; PostMessage(GetHandle(), WM_CLOSE, 0, 0); result->Success(); return; }
    if (call.method_name() == "soundSettings") {
      ShellExecuteW(GetHandle(), L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL); result->Success(); return;
    }
    result->NotImplemented();
  });
  SetChildContent(flutter_controller_->view()->GetNativeWindow());
  // Enable the native accessibility bridge before Dart sends its first tree.
  // A late request can start it with an incremental update missing the root
  // (flutter/flutter#175041).
  SendMessage(flutter_controller_->view()->GetNativeWindow(), WM_GETOBJECT,
              0, static_cast<LPARAM>(OBJID_CLIENT));
  flutter_controller_->engine()->SetNextFrameCallback([this]() { Show(); });
  flutter_controller_->ForceRedraw(); return true;
}
void FlutterWindow::OnDestroy() {
  if (channel_) channel_->SetMethodCallHandler(nullptr);
  channel_.reset();
  // Destruction can dispatch window messages. Detach before destroying the view.
  auto controller = std::move(flutter_controller_);
  controller.reset();
  Win32Window::OnDestroy();
}
LRESULT FlutterWindow::MessageHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
  if (message == WM_CLOSE && channel_ && !allow_close_) { channel_->InvokeMethod("closeRequested", nullptr); return 0; }
  if (message == WM_POWERBROADCAST && channel_) {
    if (wparam == PBT_APMSUSPEND) channel_->InvokeMethod("suspend", nullptr);
    if (wparam == PBT_APMRESUMEAUTOMATIC) channel_->InvokeMethod("resume", nullptr);
  }
  if (flutter_controller_) {
    auto result = flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam, lparam);
    if (result) return *result;
  }
  if (message == WM_FONTCHANGE && flutter_controller_) flutter_controller_->engine()->ReloadSystemFonts();
  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
