#include "bluetooth/usb_descriptors.h"
#include <sstream>
#include <stdexcept>
#include <iostream>
int main() {
    std::vector<uint8_t> good = {9,2,39,0,1,1,0,0x80,50,
        9,4,0,0,3,0xe0,1,1,0, 7,5,0x81,3,16,0,1,
        7,5,0x82,2,64,0,0, 7,5,0x02,2,64,0,0};
    std::ostringstream sink;
    auto result = ax201::describe_configuration(good, sink);
    if (!result.hci || result.sco) return 1;
    auto reject = [&](std::vector<uint8_t> b) {
        try { ax201::describe_configuration(b, sink); return false; }
        catch (const std::runtime_error&) { return true; }
    };
    auto bad = good; bad[9] = 0; if (!reject(bad)) return 2;
    bad = good; bad.pop_back(); if (!reject(bad)) return 3;
    bad = good; bad[13] = 2; if (!reject(bad)) return 4;
    bad = good; bad[18] = 40; if (!reject(bad)) return 5;
    auto sco = good;
    const std::vector<uint8_t> alt0 = {9,4,1,0,2,0xe0,1,1,0,
        7,5,3,1,0,0,1, 7,5,0x83,1,0,0,1};
    sco.insert(sco.end(), alt0.begin(), alt0.end());
    sco[2] = static_cast<uint8_t>(sco.size()); sco[4] = 2;
    result = ax201::describe_configuration(sco, sink);
    if (!result.hci || result.sco) return 6; // Zero-bandwidth alt must not count.
    auto alt1 = alt0; alt1[3] = 1; alt1[13] = 9; alt1[20] = 9;
    sco.insert(sco.end(), alt1.begin(), alt1.end());
    sco[2] = static_cast<uint8_t>(sco.size());
    result = ax201::describe_configuration(sco, sink);
    if (!result.hci || !result.sco) return 7;
    std::cout << "USB descriptor bounds and endpoint validation passed\n";
}
