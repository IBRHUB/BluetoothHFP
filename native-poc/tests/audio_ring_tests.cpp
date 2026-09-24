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
    PcmRing<2> stereo;
    stereo.reset(48000);
    std::vector<int16_t> frames(4800 * 2);
    for (size_t i = 0; i < 4800; ++i) { frames[2*i] = 12000; frames[2*i+1] = -7000; }
    stereo.push(frames.data(), 4800);
    std::array<int16_t, 960> left_right{};
    stereo.pull(left_right.data(), 480);
    for (size_t i = 0; i < 480; ++i) if (left_right[2*i] != 12000 || left_right[2*i+1] != -7000) return 7;
    std::cout << "Stereo frame alignment and channel independence passed\n";
    // Linear interpolation loses ~10dB at this frequency at half-sample phase.
    // The quality path must retain treble at every fractional phase.
    constexpr double pi = 3.14159265358979323846;
    for (double fraction : {0.1, 0.25, 0.5, 0.75, 0.9}) {
        const auto& w = FractionalSinc::weights(fraction);
        double real = 0, imag = 0;
        for (size_t t = 0; t < w.size(); ++t) {
            real += w[t] * std::cos(2*pi*0.4*double(t));
            imag += w[t] * std::sin(2*pi*0.4*double(t));
        }
        const double gain = std::sqrt(real*real + imag*imag);
        if (gain < 0.98 || gain > 1.02) return 8;
    }
    std::cout << "Sinc response within 2% at 0.4 cycles/sample across phases\n";
}
