#include "hci_transport.h"
#include "controller_profiles.h"
#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace ax201 {
namespace {
ULONG finish(HANDLE file, OVERLAPPED& ov, BOOL immediate, ULONG bytes, DWORD timeout) {
    if (immediate) return bytes;
    if (GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("USB I/O submission failed");
    if (WaitForSingleObject(ov.hEvent, timeout) != WAIT_OBJECT_0) {
        CancelIoEx(file, &ov);
        GetOverlappedResult(file, &ov, &bytes, TRUE); // Drain before freeing OVERLAPPED/buffer.
        throw std::runtime_error("HCI USB I/O timeout; controller readiness not established");
    }
    if (!GetOverlappedResult(file, &ov, &bytes, FALSE)) throw std::runtime_error("USB I/O completion failed");
    return bytes;
}
struct Event { HANDLE value = CreateEventW(nullptr, TRUE, FALSE, nullptr); ~Event() { if (value) CloseHandle(value); } };
class Commands {
    HANDLE file_; WINUSB_INTERFACE_HANDLE usb_; UCHAR pipe_;
    std::vector<UCHAR> pending_;
public:
    Commands(HANDLE file, WINUSB_INTERFACE_HANDLE usb, UCHAR pipe) : file_(file), usb_(usb), pipe_(pipe) {}
    std::vector<UCHAR> send(USHORT opcode) {
        Event event;
        if (!event.value) throw std::runtime_error("CreateEvent failed");
        OVERLAPPED ov{}; ov.hEvent = event.value;
        UCHAR command[] = {static_cast<UCHAR>(opcode), static_cast<UCHAR>(opcode >> 8), 0};
        WINUSB_SETUP_PACKET setup{}; setup.RequestType = 0x20; setup.Length = sizeof(command);
        ULONG bytes = 0;
        BOOL ok = WinUsb_ControlTransfer(usb_, setup, command, sizeof(command), &bytes, &ov);
        if (finish(file_, ov, ok, bytes, 3000) != sizeof(command)) throw std::runtime_error("short HCI command transfer");
        const auto deadline = GetTickCount64() + 3000;
        for (;;) {
            if (pending_.size() >= 2 && pending_.size() >= size_t(2 + pending_[1])) {
                const size_t n = size_t(2 + pending_[1]);
                std::vector<UCHAR> packet(pending_.begin(), pending_.begin() + n);
                pending_.erase(pending_.begin(), pending_.begin() + n);
                if (packet[0] == 0x0e && n >= 6 && (packet[3] | (packet[4] << 8)) == opcode) {
                    std::cout << "[HCI] Command Complete opcode=0x" << std::hex << opcode
                              << " status=0x" << unsigned(packet[5]) << std::dec << '\n';
                    if (packet[5]) throw std::runtime_error("HCI command rejected; firmware/init gate failed");
                    return {packet.begin() + 5, packet.end()};
                }
                if (packet[0] == 0x0f && n >= 6 && (packet[4] | (packet[5] << 8)) == opcode && packet[2])
                    throw std::runtime_error("HCI command status failure");
                std::cout << "[HCI] Async event=0x" << std::hex << unsigned(packet[0]) << std::dec << '\n';
                continue;
            }
            const auto now = GetTickCount64();
            if (now >= deadline) throw std::runtime_error("HCI command completion deadline expired");
            std::array<UCHAR, 1024> buffer{};
            ResetEvent(event.value); ov = {}; ov.hEvent = event.value; bytes = 0;
            ok = WinUsb_ReadPipe(usb_, pipe_, buffer.data(), static_cast<ULONG>(buffer.size()), &bytes, &ov);
            bytes = finish(file_, ov, ok, bytes, static_cast<DWORD>(deadline - now));
            pending_.insert(pending_.end(), buffer.begin(), buffer.begin() + bytes);
        }
    }
};
void verify_intel_legacy(Commands& commands) {
    const auto intel = commands.send(0xfc05); // Legacy Intel Read Version, no parameters.
    if (intel.size() != 10 || intel[1] != 0x37) throw std::runtime_error("Intel version needs a different parser");
    std::cout << "[FW] Intel platform=0x" << std::hex << unsigned(intel[1])
              << " variant=0x" << unsigned(intel[2]) << " hwRevision=0x" << unsigned(intel[3])
              << " fwVariant=0x" << unsigned(intel[4]) << " fwRevision=0x" << unsigned(intel[5]) << std::dec << '\n';
    if (intel[4] != 0x23) throw std::runtime_error("Intel firmware is not operational; loader required");
    std::cout << "[FW] Operational firmware already resident; no firmware upload performed\n";
}
}
void verify_hci(HANDLE file, WINUSB_INTERFACE_HANDLE usb, UCHAR pipe, unsigned backend) {
    if (backend != HFP_BACKEND_INTEL_LEGACY) throw std::runtime_error("unimplemented controller initialization backend");
    Commands commands(file, usb, pipe);
    commands.send(0x0c03); // Standard reset only, not Intel firmware reset.
    const auto version = commands.send(0x1001);
    if (version.size() != 9) throw std::runtime_error("invalid local version reply");
    const auto manufacturer = version[5] | (version[6] << 8);
    std::cout << "[HCI] Version=" << unsigned(version[1]) << " manufacturer=" << manufacturer << '\n';
    if (manufacturer != 2) throw std::runtime_error("controller manufacturer does not match Intel backend");
    verify_intel_legacy(commands);
    const auto features = commands.send(0x1003);
    if (features.size() != 9) throw std::runtime_error("invalid supported features reply");
    const auto buffers = commands.send(0x1005);
    if (buffers.size() != 8) throw std::runtime_error("invalid buffer size reply");
    std::cout << "[HCI] ACL length=" << (buffers[1] | (buffers[2] << 8))
              << " SCO length=" << unsigned(buffers[3])
              << " SCO buffers=" << (buffers[6] | (buffers[7] << 8)) << '\n';
    const auto address = commands.send(0x1009);
    if (address.size() != 7) throw std::runtime_error("invalid BD_ADDR reply");
    std::cout << "[BT] Local address: ";
    for (int i = 6; i >= 1; --i) std::cout << std::hex << std::setfill('0') << std::setw(2)
        << unsigned(address[i]) << (i == 1 ? "\n" : ":");
    std::cout << std::dec << "[HCI] Controller ready\n";
}
}
