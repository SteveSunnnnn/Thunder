#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace thunder {

class StreamingBudgetController {
public:
    struct Config {
        std::uint64_t min_bytes_per_frame = 2ull * 1024ull * 1024ull;
        std::uint64_t initial_bytes_per_frame = 16ull * 1024ull * 1024ull;
        std::uint64_t max_bytes_per_frame = 64ull * 1024ull * 1024ull;
        double target_frame_ms = 16.6667;
    };

    StreamingBudgetController() : StreamingBudgetController(Config{}) {}
    explicit StreamingBudgetController(Config config) : config_(config), budget_(config.initial_bytes_per_frame) {}

    void observe_frame(double cpu_ms, double gpu_ms) noexcept {
        const double frame_ms = std::max(cpu_ms, gpu_ms);
        smoothed_frame_ms_ = smoothed_frame_ms_ == 0.0 ? frame_ms : (smoothed_frame_ms_ * 0.9 + frame_ms * 0.1);

        if (smoothed_frame_ms_ > config_.target_frame_ms * 1.08) {
            budget_ = std::max(config_.min_bytes_per_frame, budget_ * std::uint64_t{3} / std::uint64_t{4});
        } else if (smoothed_frame_ms_ < config_.target_frame_ms * 0.82) {
            const std::uint64_t increment = std::max<std::uint64_t>(std::uint64_t{1} * 1024u * 1024u, budget_ / std::uint64_t{16});
            budget_ = std::min(config_.max_bytes_per_frame, budget_ + increment);
        }
    }

    [[nodiscard]] std::uint64_t bytes_per_frame() const noexcept { return budget_; }
    [[nodiscard]] double smoothed_frame_ms() const noexcept { return smoothed_frame_ms_; }

private:
    Config config_{};
    std::uint64_t budget_ = 0;
    double smoothed_frame_ms_ = 0.0;
};

} // namespace thunder
