#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace thunder {

struct FrameTiming {
    std::uint64_t frame_number = 0;
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
    double submit_ms = 0.0;
    std::uint32_t draw_calls = 0;
    std::uint32_t dispatch_calls = 0;
    std::uint64_t triangles = 0;
    std::uint64_t uploaded_bytes = 0;
};

class FrameProfiler {
public:
    static constexpr std::size_t history_size = 240;

    void push(FrameTiming timing) noexcept {
        history_[write_ % history_size] = timing;
        ++write_;
        count_ = count_ < history_size ? count_ + 1 : history_size;
    }

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] const FrameTiming& latest() const noexcept {
        // Before the first push, `write_ - 1` wraps to SIZE_MAX and lands on an
        // arbitrary slot; callers should gate on count() but we must not hand
        // back a garbage slot either.
        return history_[write_ == 0u ? 0u : (write_ - 1u) % history_size];
    }

    void clear() noexcept {
        write_ = 0;
        count_ = 0;
    }

    [[nodiscard]] double average_cpu_ms(std::size_t samples = history_size) const noexcept {
        const std::size_t n = std::min(count_, samples);
        if (n == 0) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t idx = (write_ - 1u - i + history_size) % history_size;
            sum += history_[idx].cpu_ms;
        }
        return sum / static_cast<double>(n);
    }

    [[nodiscard]] double average_gpu_ms(std::size_t samples = history_size) const noexcept {
        const std::size_t n = std::min(count_, samples);
        if (n == 0) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t idx = (write_ - 1u - i + history_size) % history_size;
            sum += history_[idx].gpu_ms;
        }
        return sum / static_cast<double>(n);
    }

    [[nodiscard]] double max_cpu_ms(std::size_t samples = history_size) const noexcept {
        const std::size_t n = std::min(count_, samples);
        if (n == 0) return 0.0;
        double max_val = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t idx = (write_ - 1u - i + history_size) % history_size;
            if (history_[idx].cpu_ms > max_val) max_val = history_[idx].cpu_ms;
        }
        return max_val;
    }


private:
    std::array<FrameTiming, history_size> history_{};
    std::size_t write_ = 0;
    std::size_t count_ = 0;
};

} // namespace thunder
