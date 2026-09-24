#include "usb_descriptors.h"
#include <iomanip>
#include <stdexcept>
namespace ax201 {
Layout describe_configuration(const std::vector<uint8_t>& b, std::ostream& out) {
    if (b.size() < 9 || b[0] != 9 || b[1] != 2)
        throw std::runtime_error("invalid configuration header");
    const size_t total = size_t(b[2]) | (size_t(b[3]) << 8);
    if (total != b.size()) throw std::runtime_error("configuration length mismatch");
    Layout result;
    int iface = -1, alt = -1;
    bool interrupt_in = false, bulk_in = false, bulk_out = false;
    bool iso_in = false, iso_out = false;
    unsigned expected = 0, observed = 0;
    auto finish = [&] {
        if (iface < 0) return;
        if (observed != expected) throw std::runtime_error("endpoint count mismatch");
        if (iface == 0 && alt == 0) result.hci = interrupt_in && bulk_in && bulk_out;
        if (iface == 1 && alt > 0 && iso_in && iso_out) result.sco = true;
    };
    for (size_t p = 9; p < total;) {
        if (total - p < 2 || b[p] < 2 || b[p] > total - p)
            throw std::runtime_error("truncated/zero-length USB descriptor");
        if (b[p+1] == 4) {
            if (b[p] < 9) throw std::runtime_error("short interface descriptor");
            finish();
            iface = b[p+2]; alt = b[p+3]; expected = b[p+4]; observed = 0;
            interrupt_in = bulk_in = bulk_out = iso_in = iso_out = false;
            out << "[USB] Interface=" << iface << " alt=" << alt
                << " endpoints=" << expected << '\n';
        } else if (b[p+1] == 5) {
            if (b[p] < 7 || iface < 0) throw std::runtime_error("invalid endpoint descriptor");
            ++observed;
            const auto addr = b[p+2]; const auto type = b[p+3] & 3;
            const auto mps = unsigned(b[p+4]) | (unsigned(b[p+5]) << 8);
            const bool in = (addr & 0x80) != 0;
            const char* names[] = {"control", "isochronous", "bulk", "interrupt"};
            out << "[USB]   endpoint=0x" << std::hex << unsigned(addr) << std::dec
                << ' ' << names[type] << (in ? " IN" : " OUT")
                << " maxPacketRaw=" << mps << " interval=" << unsigned(b[p+6]) << '\n';
            if (mps != 0) {
                if (type == 3 && in) interrupt_in = true;
                if (type == 2 && in) bulk_in = true;
                if (type == 2 && !in) bulk_out = true;
                if (type == 1 && in) iso_in = true;
                if (type == 1 && !in) iso_out = true;
            }
        }
        p += b[p];
    }
    finish();
    out << "[USB] Descriptor layout: HCI=" << result.hci << " SCO=" << result.sco
        << " (presence only; no HCI/SCO transfer verified)\n";
    return result;
}
}
