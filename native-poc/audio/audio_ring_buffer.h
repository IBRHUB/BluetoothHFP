#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

// Bounded mono PCM queue with interpolation and occupancy-controlled drift.
// Both threads may touch indices only while holding this short mutex.
class AudioRing {
    std::array<int16_t, 16384> data_{};
    mutable std::mutex mutex_;
    size_t read_ = 0, count_ = 0, target_ = 320, limit_ = 1600;
    double phase_ = 0, ratio_ = 1;
    bool primed_ = false;
    uint64_t under_ = 0, over_ = 0;
public:
    struct Stats { size_t queued; uint64_t underrun, overrun; double ratio; };
    void reset(unsigned rate) {
        std::lock_guard<std::mutex> lock(mutex_);
        read_ = count_ = 0; phase_ = 0; ratio_ = 1; primed_ = false;
        target_ = rate / 25; limit_ = rate / 5; under_ = over_ = 0;
    }
    void push(const int16_t* samples, size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < n; ++i) {
            if (count_ == data_.size()) { read_ = (read_ + 1) % data_.size(); --count_; ++over_; }
            data_[(read_ + count_) % data_.size()] = samples ? samples[i] : 0; ++count_;
        }
        if (count_ > limit_) {
            const auto discard = count_ - target_;
            read_ = (read_ + discard) % data_.size(); count_ -= discard; over_ += discard; phase_ = 0;
        }
    }
    void pull(int16_t* output, size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::fill_n(output, n, int16_t(0));
        if (!primed_) { if (count_ < target_) return; primed_ = true; }
        const double error = (double(count_) - double(target_)) / double(target_);
        const double desired = 1.0 + std::clamp(error * 0.001, -0.003, 0.003);
        ratio_ += (desired - ratio_) * 0.02;
        for (size_t i = 0; i < n; ++i) {
            if (count_ < 2) { under_ += n - i; primed_ = false; phase_ = 0; return; }
            const auto a = data_[read_], b = data_[(read_ + 1) % data_.size()];
            output[i] = static_cast<int16_t>(std::lround(double(a) + (double(b) - a) * phase_));
            phase_ += ratio_;
            const size_t advance = static_cast<size_t>(phase_);
            phase_ -= double(advance);
            read_ = (read_ + advance) % data_.size(); count_ -= advance;
        }
    }
    Stats stats() const { std::lock_guard<std::mutex> lock(mutex_); return {count_, under_, over_, ratio_}; }
};
