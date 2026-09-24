#include "audio/audio_ring_buffer.h"
#include <array>
#include <iostream>
#include <vector>
int main() {
    AudioRing ring; ring.reset(8000);
    std::array<int16_t, 80> output{};
    output.fill(1234); ring.pull(output.data(), output.size());
    for (auto sample : output) if (sample != 0) return 1;
    std::vector<int16_t> input(4000, 1234);
    ring.push(input.data(), input.size());
    auto stats = ring.stats();
    if (stats.queued > 1600 || stats.overrun == 0) return 2;
    ring.pull(output.data(), output.size());
    for (auto sample : output) if (sample != 1234) return 3;
    for (int i = 0; i < 100; ++i) ring.pull(output.data(), output.size());
    if (ring.stats().underrun == 0) return 4;
    // Producer clock runs 0.05% faster, consumer stays at 80 samples/tick.
    ring.reset(8000); ring.push(input.data(), 320);
    for (int tick = 0; tick < 20000; ++tick) {
        ring.push(input.data(), tick % 25 == 0 ? 81 : 80);
        ring.pull(output.data(), output.size());
        for (auto sample : output) if (sample != 1234) return 5;
    }
    stats = ring.stats();
    if (stats.overrun || stats.underrun || stats.queued > 1000 || stats.ratio <= 1) return 6;
    std::cout << "Audio ring silence, overflow, starvation and 500ppm drift passed\n";
}
