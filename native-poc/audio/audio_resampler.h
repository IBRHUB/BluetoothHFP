#pragma once
#include <array>
#include <cmath>
// Fractional-delay, 32-tap Blackman-windowed sinc. A phase table avoids trig in
// the audio callback. Used only for +/-0.3% clock correction; WASAPI handles
// conversion between device rates and Bluetooth rates with its quality SRC.
class FractionalSinc {
public:
    static constexpr size_t taps = 32, phases = 1024;
    using Weights = std::array<double, taps>;
    static const Weights& weights(double fraction) {
        static const auto table = [] {
            std::array<Weights, phases> result{};
            constexpr double pi = 3.14159265358979323846;
            for (size_t p = 0; p < phases; ++p) {
                double total = 0;
                for (size_t t = 0; t < taps; ++t) {
                    const double x = double(t) - 15.0 - double(p) / double(phases);
                    const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
                    const double w = 0.42 - 0.5 * std::cos(2*pi*double(t)/31) + 0.08 * std::cos(4*pi*double(t)/31);
                    result[p][t] = sinc * w; total += result[p][t];
                }
                for (auto& coefficient : result[p]) coefficient /= total;
            }
            return result;
        }();
        return table[static_cast<size_t>(fraction * phases) % phases];
    }
};
