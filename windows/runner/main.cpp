#include <flutter/dart_project.h>
#include <windows.h>
#include "flutter_window.h"
#include "platform_bridge.h"
#include "utils.h"
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
  auto arguments = GetCommandLineArguments();
  if (!arguments.empty() && arguments[0] == "--recover") {
    if (!BeginDriverOperation("Native", nullptr)) return 1;
    int status;
    do { Sleep(200); status = DriverOperationStatus(); } while (status == -1);
    MessageBoxW(nullptr, status == 0 ? L"Windows Bluetooth restored." : L"Recovery failed. Close the headset app and retry. Recovery packages remain in C:\\ProgramData\\BluetoothHFP\\backups.", L"Bluetooth HFP Recovery", status == 0 ? MB_OK : MB_ICONERROR);
    return status;
  }
  HANDLE single = CreateMutexW(nullptr, FALSE, L"Local\\BluetoothHFP.Desktop");
  if (!single || GetLastError() == ERROR_ALREADY_EXISTS) { MessageBoxW(nullptr, L"Bluetooth HFP is already running.", L"Bluetooth HFP", MB_OK); return 0; }
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  flutter::DartProject project(L"data");
  // Keep the established Windows renderer for compatibility with remote sessions.
  project.set_impeller_switch(flutter::ImpellerSwitch::Disabled);
  project.set_dart_entrypoint_arguments(arguments);
  FlutterWindow window(project);
  if (!window.Create(L"Bluetooth HFP", {60,60}, {540,960})) return EXIT_FAILURE;
  window.SetQuitOnClose(true);
  MSG msg;
  while (GetMessage(&msg,nullptr,0,0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
  CoUninitialize(); CloseHandle(single); return EXIT_SUCCESS;
}
