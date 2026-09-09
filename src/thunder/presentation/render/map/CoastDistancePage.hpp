#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace thunder {

// Signed distance to coastline: negative = water, positive = land.
// 0.5 m quantization provides +/-16.38 km of range in one R16_SNORM-like page,
// enough for shore foam, shallow-water color, beach masks and coast highlights.
class CoastDistancePage {
public:
    static constexpr std::uint32_t samples_per_side = 128;
    static constexpr std::size_t sample_count = static_cast<std::size_t>(samples_per_side) * samples_per_side;
    static constexpr float distance_step_m = 0.5f;
    static constexpr float max_distance_m = 32'767.0f * distance_step_m;

    using Storage = std::array<std::int16_t, sample_count>;

    void encode(std::span<const float> signed_distance_m) noexcept;
    [[nodiscard]] float decode(std::size_t index) const noexcept;
    [[nodiscard]] std::span<const std::int16_t> samples() const noexcept { return samples_; }
    [[nodiscard]] std::span<std::int16_t> samples() noexcept { return samples_; }

private:
    Storage samples_{};
};

static_assert(sizeof(CoastDistancePage::Storage) == 32u * 1024u);

} // namespace thunder
