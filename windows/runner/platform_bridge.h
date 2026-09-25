#pragma once
#include <flutter/encodable_value.h>
#include <windows.h>
#include <string>
flutter::EncodableList AudioEndpoints();
std::wstring AppDirectory();
bool BeginDriverOperation(const std::string& action, HWND owner);
int DriverOperationStatus();
