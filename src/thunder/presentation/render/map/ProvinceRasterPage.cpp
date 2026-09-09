#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"

#include <limits>

namespace thunder {

void ProvinceRasterPage::set(std::uint32_t x, std::uint32_t y, ProvinceId province) noexcept {
    if (x >= samples_per_side || y >= samples_per_side || !province.valid() ||
        province.value() >= max_province_count) return;
    samples_[index(x, y)] = static_cast<EncodedId>(province.value() + 1u);
}

void ProvinceRasterPage::set_water(std::uint32_t x, std::uint32_t y) noexcept {
    if (x >= samples_per_side || y >= samples_per_side) return;
    samples_[index(x, y)] = water;
}

ProvinceId ProvinceRasterPage::sample(std::uint32_t x, std::uint32_t y) const noexcept {
    const auto value = encoded(x, y);
    if (value == water) return ProvinceId{};
    return ProvinceId{static_cast<ProvinceId::rep_type>(value - 1u)};
}

ProvinceRasterPage::EncodedId ProvinceRasterPage::encoded(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= samples_per_side || y >= samples_per_side) return water;
    return samples_[index(x, y)];
}

} // namespace thunder
