#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum class DynamicFlagPattern : std::uint8_t {
    Solid,
    HorizontalTricolor,
    VerticalTricolor,
    NordicCross,
    CrossSaltire
};

struct DynamicFlag3DConfig {
    std::uint16_t columns = 24;
    std::uint16_t rows = 12;
    float width = 1.6f;
    float height = 1.0f;
    float wind_strength = 0.055f;
    float wave_frequency = 5.4f;
    float wave_speed = 3.2f;
    DynamicFlagPattern pattern = DynamicFlagPattern::HorizontalTricolor;
    std::array<std::uint32_t, 3> colors{{0xff243d62u, 0xffeee5cbu, 0xff8f2e2au}};
};

struct DynamicFlagVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Reusable cloth flag mesh. The hoist edge (u=0) remains pinned while the
// rest of the sheet receives two low-frequency wind waves. Render backends
// may animate this mesh on the CPU through update() or reproduce the same
// deformation in a vertex shader using config().
class DynamicFlag3D {
public:
    explicit DynamicFlag3D(DynamicFlag3DConfig config = {});

    void configure(DynamicFlag3DConfig config);
    void update(float time_seconds) noexcept;

    [[nodiscard]] const DynamicFlag3DConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::span<const DynamicFlagVertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept { return indices_; }

private:
    void rebuild();

    DynamicFlag3DConfig config_{};
    std::size_t cloth_vertex_count_ = 0;
    std::vector<DynamicFlagVertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

} // namespace thunder
