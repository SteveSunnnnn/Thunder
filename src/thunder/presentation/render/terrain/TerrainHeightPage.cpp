#include "thunder/presentation/render/terrain/TerrainHeightPage.hpp"

#include <algorithm>
#include <cmath>

namespace thunder {

void TerrainHeightPage::encode(std::span<const float> heights_m) noexcept {
    const std::size_t count = std::min(heights_m.size(), samples_.size());
    for (std::size_t i = 0; i < count; ++i) {
        const float clamped = std::clamp(heights_m[i], minimum_height_m, maximum_height_m);
        const float quantized = (clamped - minimum_height_m) / height_step_m;
        samples_[i] = static_cast<std::uint16_t>(std::lround(quantized));
    }
    for (std::size_t i = count; i < samples_.size(); ++i) {
        samples_[i] = 0u;
    }
}

float TerrainHeightPage::decode(std::size_t index) const noexcept {
    if (index >= samples_.size()) return minimum_height_m;
    return minimum_height_m + static_cast<float>(samples_[index]) * height_step_m;
}

} // namespace thunder
