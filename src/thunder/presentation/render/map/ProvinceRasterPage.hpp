#pragma once

#include "thunder/foundation/base/StrongId.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace thunder {

// Runtime GPU province-id page. ID zero is water/no-province, therefore runtime
// ProvinceId N is encoded as N+1. R16_UINT keeps a 128x128 page at 32 KiB and
// supports 65,534 provinces, comfortably above the intended grand-strategy scale.
class ProvinceRasterPage {
public:
    using EncodedId = std::uint16_t;
    static constexpr std::uint32_t samples_per_side = 128;
    static constexpr std::size_t sample_count = static_cast<std::size_t>(samples_per_side) * samples_per_side;
    static constexpr EncodedId water = 0u;
    static constexpr std::uint32_t max_province_count = 65'534u;

    using Storage = std::array<EncodedId, sample_count>;

    void clear() noexcept { samples_.fill(water); }
    void set(std::uint32_t x, std::uint32_t y, ProvinceId province) noexcept;
    void set_water(std::uint32_t x, std::uint32_t y) noexcept;
    [[nodiscard]] ProvinceId sample(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] EncodedId encoded(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] std::span<const EncodedId> samples() const noexcept { return samples_; }
    [[nodiscard]] std::span<EncodedId> samples() noexcept { return samples_; }

private:
    [[nodiscard]] static constexpr std::size_t index(std::uint32_t x, std::uint32_t y) noexcept {
        return static_cast<std::size_t>(y) * samples_per_side + x;
    }

    Storage samples_{};
};

static_assert(sizeof(ProvinceRasterPage::Storage) == 32u * 1024u);

} // namespace thunder
