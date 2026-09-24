#include "ax201_transport.h"
#include "usb_descriptors.h"
#include <windows.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <usb.h>
#include <usbioctl.h>
#include <winusb.h>
#include "hci_transport.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ax201 {
namespace {
// USB device/hub interface GUIDs, not Bluetooth API GUIDs.
const GUID device_guid = {0xa5dcbf10,0x6530,0x11d2,{0x90,0x1f,0x00,0xc0,0x4f,0xb9,0x51,0xed}};
const GUID hub_guid = {0xf18a0e88,0xc30c,0x11d0,{0x88,0x15,0x00,0xa0,0xc9,0x06,0xbe,0xd8}};
struct Devices {
    HDEVINFO value;
    ~Devices() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Usb {
    WINUSB_INTERFACE_HANDLE value = nullptr;
    ~Usb() { if (value) WinUsb_Free(value); }
};
std::wstring property(HDEVINFO list, SP_DEVINFO_DATA& info, DWORD key) {
    DWORD size = 0, type = 0;
    SetupDiGetDeviceRegistryPropertyW(list, &info, key, &type, nullptr, 0, &size);
    if (!size) return L"(unavailable)";
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(list, &info, key, &type,
        reinterpret_cast<BYTE*>(buf.data()), size, nullptr)) return L"(unavailable)";
    return buf.data();
}
bool target(const std::wstring& id) {
    return _wcsnicmp(id.c_str(), L"USB\\VID_8087&PID_0026", 21) == 0;
}
template<class F> void interfaces(const GUID& guid, F callback) {
    Devices list{SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
    if (list.value == INVALID_HANDLE_VALUE) throw std::runtime_error("SetupDiGetClassDevs failed");
    for (DWORD n = 0;; ++n) {
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(list.value, nullptr, &guid, n, &iface)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) throw std::runtime_error("interface enumeration failed");
            break;
        }
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(list.value, &iface, nullptr, 0, &bytes, nullptr);
        if (!bytes) throw std::runtime_error("interface detail size failed");
        std::vector<BYTE> storage(bytes);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
        if (!SetupDiGetDeviceInterfaceDetailW(list.value, &iface, detail, bytes, nullptr, &info))
            throw std::runtime_error("interface detail failed");
        callback(list.value, info, detail->DevicePath);
    }
}
std::vector<uint8_t> hub_configuration(HANDLE hub, ULONG port) {
    const auto header = sizeof(USB_DESCRIPTOR_REQUEST);
    std::vector<uint8_t> buf(header + 65535, 0);
    auto req = reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(buf.data());
    req->ConnectionIndex = port;
    req->SetupPacket.wValue = USB_CONFIGURATION_DESCRIPTOR_TYPE << 8;
    req->SetupPacket.wLength = 65535;
    DWORD got = 0;
    if (!DeviceIoControl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
                        buf.data(), static_cast<DWORD>(buf.size()), buf.data(),
                        static_cast<DWORD>(buf.size()), &got, nullptr)) {
        std::cout << "[USB] Hub GET_DESCRIPTOR failed win32=" << GetLastError() << '\n';
        return {};
    }
    if (got < header + 9) return {};
    auto start = buf.data() + header;
    const size_t total = size_t(start[2]) | (size_t(start[3]) << 8);
    if (total < 9 || total > got - header) return {};
    return {start, start + total};
}
void inspect_hubs() {
    interfaces(hub_guid, [](HDEVINFO, SP_DEVINFO_DATA&, const wchar_t* path) {
        Handle hub{CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr)};
        if (hub.value == INVALID_HANDLE_VALUE) {
            std::cout << "[USB] Hub open unavailable win32=" << GetLastError() << '\n'; return;
        }
        USB_NODE_INFORMATION node{}; node.NodeType = UsbHub; DWORD got = 0;
        if (!DeviceIoControl(hub.value, IOCTL_USB_GET_NODE_INFORMATION, &node, sizeof(node),
                             &node, sizeof(node), &got, nullptr)) return;
        for (ULONG port = 1; port <= node.u.HubInformation.HubDescriptor.bNumberOfPorts; ++port) {
            // SDK structure has a trailing zero-sized PipeList; allocate storage for its output.
            std::vector<BYTE> connection_buffer(sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 32 * sizeof(USB_PIPE_INFO), 0);
            auto& connection = *reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(connection_buffer.data());
            connection.ConnectionIndex = port;
            if (!DeviceIoControl(hub.value, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                &connection, static_cast<DWORD>(connection_buffer.size()), &connection,
                static_cast<DWORD>(connection_buffer.size()), &got, nullptr)) continue;
            if (connection.ConnectionStatus != DeviceConnected ||
                connection.DeviceDescriptor.idVendor != 0x8087 ||
                connection.DeviceDescriptor.idProduct != 0x0026) continue;
            std::cout << "[USB] AX201 USB hub port=" << port << " bcdDevice=0x" << std::hex
                      << connection.DeviceDescriptor.bcdDevice << std::dec
                      << " speedEnum=" << unsigned(connection.Speed) << '\n';
            auto config = hub_configuration(hub.value, port);
            if (config.empty()) std::cout << "[USB] Configuration descriptor unavailable\n";
            else describe_configuration(config, std::cout);
        }
    });
}
}
int probe(bool run_hci) {
    try {
        std::cout << (run_hci ? "[STAGE] 2 HCI verification after USB gate\n" : "[STAGE] 1 USB ownership and descriptor probe; no HCI commands\n");
        bool found = false, opened = false, hci = false;
        interfaces(device_guid, [&](HDEVINFO list, SP_DEVINFO_DATA& info, const wchar_t* path) {
            wchar_t instance[MAX_DEVICE_ID_LEN]{};
            if (!SetupDiGetDeviceInstanceIdW(list, &info, instance, MAX_DEVICE_ID_LEN, nullptr) || !target(instance)) return;
            found = true;
            std::cout << "[USB] AX201 detected\n";
            std::wcout << L"[USB] Instance: " << instance << L"\n[USB] Service: "
                       << property(list, info, SPDRP_SERVICE) << L"\n[USB] LowerFilters: "
                       << property(list, info, SPDRP_LOWERFILTERS) << L'\n';
            Handle file{CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
            if (file.value == INVALID_HANDLE_VALUE) {
                std::cout << "[USB] CreateFile failed win32=" << GetLastError() << '\n'; return;
            }
            Usb usb;
            if (!WinUsb_Initialize(file.value, &usb.value)) {
                std::cout << "[USB] WinUsb_Initialize failed win32=" << GetLastError() << '\n'; return;
            }
            opened = true;
            std::cout << "[USB] WinUSB opened\n";
            USB_DEVICE_DESCRIPTOR dev{}; ULONG received = 0;
            if (!WinUsb_GetDescriptor(usb.value, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                reinterpret_cast<PUCHAR>(&dev), sizeof(dev), &received) || received != sizeof(dev) ||
                dev.idVendor != 0x8087 || dev.idProduct != 0x0026)
                throw std::runtime_error("WinUSB target descriptor mismatch");
            std::vector<uint8_t> config(65535);
            if (!WinUsb_GetDescriptor(usb.value, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
                config.data(), static_cast<ULONG>(config.size()), &received))
                throw std::runtime_error("WinUSB configuration read failed");
            config.resize(received);
            const auto layout = describe_configuration(config, std::cout);
            USB_INTERFACE_DESCRIPTOR iface{};
            if (!WinUsb_QueryInterfaceSettings(usb.value, 0, &iface) || iface.bInterfaceNumber != 0)
                throw std::runtime_error("default WinUSB interface is not HCI interface 0");
            bool interrupt_in = false, bulk_in = false, bulk_out = false;
            UCHAR event_pipe = 0;
            for (UCHAR p = 0; p < iface.bNumEndpoints; ++p) {
                WINUSB_PIPE_INFORMATION pipe{};
                if (!WinUsb_QueryPipe(usb.value, 0, p, &pipe))
                    throw std::runtime_error("WinUSB pipe query failed");
                const bool in = (pipe.PipeId & 0x80) != 0;
                if (pipe.PipeType == UsbdPipeTypeInterrupt && in) { interrupt_in = true; event_pipe = pipe.PipeId; }
                if (pipe.PipeType == UsbdPipeTypeBulk && in) bulk_in = true;
                if (pipe.PipeType == UsbdPipeTypeBulk && !in) bulk_out = true;
            }
            hci = layout.hci && interrupt_in && bulk_in && bulk_out;
            Usb sco;
            if (!WinUsb_GetAssociatedInterface(usb.value, 0, &sco.value))
                std::cout << "[USB] SCO associated interface unavailable win32=" << GetLastError() << '\n';
            else std::cout << "[USB] Associated interface opened; SCO transfer remains untested\n";
            if (run_hci && hci) verify_hci(file.value, usb.value, event_pipe);
        });
        inspect_hubs();
        if (!found) { std::cout << "[GATE] BLOCKED: target USB device interface absent\n"; return 2; }
        if (!opened) {
            std::cout << "[GATE] BLOCKED: no WinUSB handle. Current function/filter driver owns USB.\n"
                         "[HCI] NOT RUN: stage 1 did not pass\n";
            return 3;
        }
        if (!hci) { std::cout << "[GATE] BLOCKED: HCI USB pipes not verified\n"; return 4; }
        std::cout << (run_hci ? "[GATE] PASS: HCI verified. HFP/SCO not yet verified.\n" : "[GATE] PASS: USB access only. HCI readiness and firmware remain unverified.\n");
        return 0;
    } catch (const std::exception& e) {
        std::cout << "[ERROR] " << e.what() << '\n'; return 4;
    }
}
}
