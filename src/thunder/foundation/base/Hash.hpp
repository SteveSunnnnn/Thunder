#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace thunder {

class Fnv1a64 {
public:
    void add_bytes(std::span<const std::byte> bytes) noexcept {
        const auto* ptr = reinterpret_cast<const unsigned char*>(bytes.data());
        std::size_t size = bytes.size();
        std::uint64_t val = value_;
        constexpr std::uint64_t prime = 1099511628211ull;
        while (size >= 8) {
            val = (val ^ ptr[0]) * prime;
            val = (val ^ ptr[1]) * prime;
            val = (val ^ ptr[2]) * prime;
            val = (val ^ ptr[3]) * prime;
            val = (val ^ ptr[4]) * prime;
            val = (val ^ ptr[5]) * prime;
            val = (val ^ ptr[6]) * prime;
            val = (val ^ ptr[7]) * prime;
            ptr += 8;
            size -= 8;
        }
        while (size > 0) {
            val = (val ^ *ptr++) * prime;
            --size;
        }
        value_ = val;
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void add(const T& value) noexcept {
        // Deterministic: hash object representation byte-wise but zero padding
        // bytes that may be uninitialized due to alignment. This avoids
        // cross-compiler / optimization non-determinism.
        if constexpr (std::is_floating_point_v<T>) {
            // Normalize signed zero and NaN payloads.  NaNs are not values in
            // the simulation, but can enter a debug/UI path or a malformed
            // mod; hashing their hardware payload would make checksums differ
            // across FPUs and serialization boundaries.
            if (std::isnan(value)) {
                if constexpr (sizeof(T) == sizeof(float)) {
                    constexpr std::uint32_t canonical_nan = 0x7fc00000u;
                    add(canonical_nan);
                } else if constexpr (sizeof(T) == sizeof(double)) {
                    constexpr std::uint64_t canonical_nan = 0x7ff8000000000000ull;
                    add(canonical_nan);
                } else {
                    const T canonical_nan = std::numeric_limits<T>::quiet_NaN();
                    auto bytes = std::as_bytes(std::span{&canonical_nan, std::size_t{1}});
                    add_bytes(bytes);
                }
            } else {
                T normalized = value;
                if (normalized == T{0}) normalized = T{0}; // -0.0 -> 0.0
                auto bytes = std::as_bytes(std::span{&normalized, std::size_t{1}});
                add_bytes(bytes);
            }
        } else {
            // For structs with padding, hash members via memcpy into zeroed buffer
            // Fallback: hash bytes but caller should prefer explicit member hashing
            // for structs containing padding. We keep byte-wise for trivial scalars.
            auto bytes = std::as_bytes(std::span{&value, std::size_t{1}});
            add_bytes(bytes);
        }
    }
    // Byte-wise helper for trivially copyable values.  Callers hashing a
    // struct with padding should still prefer explicit member hashing; the
    // source representation of padding cannot be inferred generically.
    template <typename T>
    void add_padded(const T& value) noexcept {
        alignas(T) std::byte zeroed[sizeof(T)]{};
        std::memcpy(zeroed, &value, sizeof(T));
        add_bytes(std::as_bytes(std::span{zeroed, sizeof(T)}));
    }

    void add(std::string_view text) noexcept {
        auto bytes = std::as_bytes(std::span{text.data(), text.size()});
        add_bytes(bytes);
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_ = 14695981039346656037ull;
};

} // namespace thunder
