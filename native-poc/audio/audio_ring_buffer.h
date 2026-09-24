#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include "audio_resampler.h"

// Bounded mono PCM queue with interpolation and occupancy-controlled drift.
// Both threads may touch indices only while holding this short mutex.
template<size_t Channels = 1> class PcmRing {
    std::array<std::array<int16_t, Channels>, 16384> data_{};
    mutable std::mutex mutex_;
    size_t read_ = 0, count_ = 0, target_ = 320, limit_ = 1600;
    double phase_ = 0, ratio_ = 1;
    bool primed_ = false;
    uint64_t under_ = 0, over_ = 0;
public:
    struct Stats { size_t queued; uint64_t underrun, overrun; double ratio; };
    void reset(unsigned rate, unsigned cushion_ms = 40) {
        (void)FractionalSinc::weights(0); // Prepare table outside streaming callbacks.
        std::lock_guard<std::mutex> lock(mutex_);
        read_ = count_ = 0; phase_ = 0; ratio_ = 1; primed_ = false;
        target_ = std::clamp<size_t>(size_t(rate) * cushion_ms / 1000, 64, 8000);
        limit_ = std::min<size_t>(rate / 5, data_.size() - 64); under_ = over_ = 0;
    }
    void push(const int16_t* samples, size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < n; ++i) {
            if (count_ == data_.size()) { read_ = (read_ + 1) % data_.size(); --count_; ++over_; }
            for (size_t c = 0; c < Channels; ++c)
                data_[(read_ + count_) % data_.size()][c] = samples ? samples[i * Channels + c] : 0;
            ++count_;
        }
        if (count_ > limit_) {
            const auto discard = count_ - target_;
            read_ = (read_ + discard) % data_.size(); count_ -= discard; over_ += discard; phase_ = 0;
        }
    }
    void pull(int16_t* output, size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::fill_n(output, n * Channels, int16_t(0));
        if (!primed_) { if (count_ < target_) return; primed_ = true; }
        const double error = (double(count_) - double(target_)) / double(target_);
        const double desired = 1.0 + std::clamp(error * 0.001, -0.003, 0.003);
        ratio_ += (desired - ratio_) * 0.02;
        for (size_t i = 0; i < n; ++i) {
            if (count_ < FractionalSinc::taps) { under_ += n - i; primed_ = false; phase_ = 0; return; }
            const auto& weights = FractionalSinc::weights(phase_);
            for (size_t c = 0; c < Channels; ++c) {
                double sample = 0;
                for (size_t t = 0; t < weights.size(); ++t) sample += data_[(read_ + t) % data_.size()][c] * weights[t];
                output[i * Channels + c] = static_cast<int16_t>(std::clamp(std::lround(sample), -32768L, 32767L));
            }
            phase_ += ratio_;
            const size_t advance = static_cast<size_t>(phase_);
            phase_ -= double(advance);
            read_ = (read_ + advance) % data_.size(); count_ -= advance;
        }
    }
    Stats stats() const { std::lock_guard<std::mutex> lock(mutex_); return {count_, under_, over_, ratio_}; }
};
using AudioRing = PcmRing<1>;
