#pragma once
#include <windows.h>
#include <winusb.h>
namespace ax201 { void verify_hci(HANDLE file, WINUSB_INTERFACE_HANDLE usb, UCHAR event_pipe, unsigned backend); }
