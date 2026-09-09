#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace thunder {

class TerrainHeightPage {
public:
    static constexpr std::uint32_t samples_per_side = 65;
    static constexpr std::size_t sample_count = static_cast<std::size_t>(samples_per_side) * samples_per_side;
    static constexpr float minimum_height_m = -12'000.0f;
    static constexpr float height_step_m = 0.5f;
    static constexpr float maximum_height_m = minimum_height_m + 65'535.0f * height_step_m;

    using Storage = std::array<std::uint16_t, sample_count>;

    void encode(std::span<const float> heights_m) noexcept;
    [[nodiscard]] float decode(std::size_t index) const noexcept;
    [[nodiscard]] std::span<const std::uint16_t> samples() const noexcept { return samples_; }
    [[nodiscard]] std::span<std::uint16_t> samples() noexcept { return samples_; }

private:
    Storage samples_{};
};

static_assert(TerrainHeightPage::sample_count * sizeof(std::uint16_t) == 8450u);

} // namespace thunder
