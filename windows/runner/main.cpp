#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>
#include <appmodel.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>

#include "flutter_window.h"
#include "utils.h"

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  if (command_line_arguments.size() == 3 && command_line_arguments[0] == "--inspect-endpoint") {
    std::ofstream report(std::filesystem::u8path(command_line_arguments[1]));
    const std::string& id = command_line_arguments[2];
    // Windows endpoint IDs consist of ASCII GUIDs.
    report << Utf8FromUtf16(InspectAudioEndpoint(std::wstring(id.begin(), id.end())).c_str());
    ::CoUninitialize();
    return report ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  // Headless hardware smoke test. Optional address connects the selected phone;
  // it never dials or answers a call. Output remains on the local machine.
  if (command_line_arguments.size() >= 2 &&
      (command_line_arguments[0] == "--diagnose" || command_line_arguments[0] == "--diagnose-route" ||
       command_line_arguments[0] == "--diagnose-transfer" ||
       command_line_arguments[0] == "--diagnose-voice")) {
    std::ofstream report(std::filesystem::u8path(command_line_arguments[1]));
    if (!report) { ::CoUninitialize(); return EXIT_FAILURE; }
    UINT32 package_length = 0;
    report << "Package identity: "
           << (GetCurrentPackageFullName(&package_length, nullptr) == ERROR_INSUFFICIENT_BUFFER)
           << "\n";
    report << Utf8FromUtf16(InspectAudioDevices().c_str()) << "\n";
    {
      HfpController controller;
      controller.SetVoiceMode(command_line_arguments[0] == "--diagnose-voice");
      if (command_line_arguments.size() >= 3) {
        const auto error = controller.SelectPhone(&command_line_arguments[2],
            command_line_arguments[0] != "--diagnose-route");
        report << "Selection: " << Utf8FromUtf16(error.c_str()) << "\n";
      }
      const int iterations = command_line_arguments.size() >= 3 ? 10 : 1;
      bool transfer_requested = false;
      for (int i = 0; i < iterations; ++i) {
        const auto snapshot = controller.Snapshot();
        const auto& values = std::get<flutter::EncodableMap>(snapshot);
        if (command_line_arguments[0] == "--diagnose-transfer" && !transfer_requested) {
          auto state = values.find(flutter::EncodableValue("callsState"));
          if (state != values.end() && std::get_if<std::string>(&state->second) &&
              std::get<std::string>(state->second) == "connected") {
            controller.RequestPcAudio();
            transfer_requested = true;
          }
        }
        report << "--- " << i * 5 << " seconds ---\n";
        for (const auto& entry : values) {
          const auto* key = std::get_if<std::string>(&entry.first);
          if (!key) continue;
          if (key->find("Id") != std::string::npos) continue;
          report << *key << ": ";
          if (const auto* value = std::get_if<std::string>(&entry.second)) report << *value;
          else if (const auto* boolean = std::get_if<bool>(&entry.second)) report << *boolean;
          else if (const auto* number = std::get_if<double>(&entry.second)) report << *number;
          else if (const auto* frames = std::get_if<int64_t>(&entry.second)) report << *frames;
          else if (const auto* list = std::get_if<flutter::EncodableList>(&entry.second)) report << list->size() << " devices";
          report << "\n";
        }
        report.flush();
        if (i + 1 < iterations) std::this_thread::sleep_for(std::chrono::seconds(5));
      }
      controller.Stop();
    }
    ::CoUninitialize();
    return EXIT_SUCCESS;
  }

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(560, 720);
  if (!window.Create(L"Bluetooth HFP", origin, size)) {
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  ::CoUninitialize();
  return EXIT_SUCCESS;
}
