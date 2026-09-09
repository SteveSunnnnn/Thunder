#pragma once
#include <bit>
#include <cstdint>
#include <limits>

namespace thunder {

class DeterministicRng {
public:
    [[nodiscard]] static constexpr std::uint64_t mix(std::uint64_t value) noexcept {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31u);
    }

    // Stateless keyed samples are preferred in parallel simulation. The same
    // entity/tick/counter produces the same value regardless of worker count or
    // work-stealing order.
    [[nodiscard]] static constexpr std::uint64_t keyed_u64(std::uint64_t base_seed,
                                                            std::uint64_t stream_key,
                                                            std::uint64_t counter = 0u) noexcept {
        return mix(base_seed ^ mix(stream_key) ^ mix(counter + 0xD1B54A32D192ED03ull));
    }

    [[nodiscard]] static double keyed_unit(std::uint64_t base_seed,
                                           std::uint64_t stream_key,
                                           std::uint64_t counter = 0u) noexcept {
        constexpr auto inv = 1.0 / static_cast<double>(std::uint64_t{1} << 53u);
        return static_cast<double>(keyed_u64(base_seed, stream_key, counter) >> 11u) * inv;
    }

    [[nodiscard]] static std::uint64_t keyed_range(std::uint64_t base_seed,
                                                   std::uint64_t stream_key,
                                                   std::uint64_t min_val,
                                                   std::uint64_t max_val,
                                                   std::uint64_t counter = 0u) noexcept {
        if (min_val >= max_val) return min_val;
        const auto range = max_val - min_val + 1u;
        // Portable rejection sampling.  The old implementation used
        // __uint128_t/Lemire multiplication, which made this public header
        // unavailable to MSVC and also overflowed for the full uint64 range.
        // `-range % range` is the number of low values that would create a
        // modulo bias; unsigned wraparound is intentional and well-defined.
        const auto threshold = range == 0u ? 0u : ((std::uint64_t{0} - range) % range);
        for (std::uint64_t attempt = 0u;; ++attempt) {
            const auto v = keyed_u64(base_seed, stream_key,
                                     counter + attempt * 0x9E3779B97F4A7C15ull);
            if (range == 0u || v >= threshold) {
                return min_val + (range == 0u ? v : v % range);
            }
        }
    }

    [[nodiscard]] static double keyed_range_double(std::uint64_t base_seed,
                                                  std::uint64_t stream_key,
                                                  double min_val,
                                                  double max_val,
                                                  std::uint64_t counter = 0u) noexcept {
        return min_val + (max_val - min_val) * keyed_unit(base_seed, stream_key, counter);
    }

    explicit DeterministicRng(std::uint64_t seed = 0xC0181836ull) : state_(seed) {}

    [[nodiscard]] std::uint64_t next_u64() noexcept {
        state_ += 0x9E3779B97F4A7C15ull;
        auto z = state_;
        z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27u)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31u);
    }

    [[nodiscard]] std::uint64_t next_u64(std::uint64_t min_val, std::uint64_t max_val) noexcept {
        if (min_val >= max_val) return min_val;
        const auto range = max_val - min_val + 1u;
        const auto threshold = range == 0u ? 0u : ((std::uint64_t{0} - range) % range);
        for (;;) {
            const std::uint64_t v = next_u64();
            if (range == 0u || v >= threshold)
                return min_val + (range == 0u ? v : v % range);
        }
    }

    [[nodiscard]] std::int64_t next_i64(std::int64_t min_val, std::int64_t max_val) noexcept {
        if (min_val >= max_val) return min_val;
        // Subtract in the unsigned domain so INT64_MIN..INT64_MAX is valid.
        const auto range = static_cast<std::uint64_t>(max_val) -
                           static_cast<std::uint64_t>(min_val) + 1u;
        const auto threshold = range == 0u ? 0u : ((std::uint64_t{0} - range) % range);
        for (;;) {
            const std::uint64_t v = next_u64();
            if (range == 0u || v >= threshold) {
                const auto offset = range == 0u ? v : v % range;
                // Addition and conversion are performed on the unsigned bit
                // pattern, avoiding signed overflow at INT64_MIN/MAX.
                return std::bit_cast<std::int64_t>(
                    static_cast<std::uint64_t>(min_val) + offset);
            }
        }
    }

    [[nodiscard]] double unit() noexcept {
        constexpr auto inv = 1.0 / static_cast<double>(std::uint64_t{1} << 53u);
        return static_cast<double>(next_u64() >> 11u) * inv;
    }

    [[nodiscard]] double range_double(double min_val, double max_val) noexcept {
        return min_val + (max_val - min_val) * unit();
    }

    [[nodiscard]] std::uint64_t state() const noexcept { return state_; }


private:
    std::uint64_t state_;
};

} // namespace thunder
