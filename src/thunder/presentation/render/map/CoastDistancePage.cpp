#include "thunder/presentation/render/map/CoastDistancePage.hpp"

#include <algorithm>
#include <cmath>

namespace thunder {

void CoastDistancePage::encode(std::span<const float> signed_distance_m) noexcept {
    const auto n = std::min(signed_distance_m.size(), samples_.size());
    constexpr float inv_step = 1.0f / distance_step_m;
    for (std::size_t i = 0; i < n; ++i) {
        const float clamped = std::clamp(signed_distance_m[i], -max_distance_m, max_distance_m);
        const float val = clamped * inv_step;
        samples_[i] = static_cast<std::int16_t>(val >= 0.0f ? val + 0.5f : val - 0.5f);
    }
    for (std::size_t i = n; i < samples_.size(); ++i) samples_[i] = 0;
}

float CoastDistancePage::decode(std::size_t index) const noexcept {
    if (index >= samples_.size()) return 0.0f;
    return static_cast<float>(samples_[index]) * distance_step_m;
}

} // namespace thunder
