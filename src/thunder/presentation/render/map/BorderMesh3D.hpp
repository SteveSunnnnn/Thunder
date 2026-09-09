#pragma once

#include "thunder/presentation/render/PhysicalLighting.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct BorderSegmentVertexGpu {
    Vec3 position;      // 3D position clamped to terrain surface
    Vec3 normal;
    Vec3 tangent;
    float ribbon_u;     // Across border width [-1..+1]
    float length_v;     // Along border length
    std::uint32_t country_color_abgr;
    float glow_intensity;
};

class BorderMesh3D {
public:
    // Generates continuous 3D ribbon geometry along political borders following terrain elevation contours
    [[nodiscard]] static std::vector<BorderSegmentVertexGpu> generate_border_ribbon(
        std::span<const Vec3> spline_points,
        float border_width_m = 15.0f,
        std::uint32_t country_color = 0xFF2040E0u,
        float glow_factor = 1.0f);

    // Evaluates 3D border shader with pulse animation, dashed wartime frontline patterns, and outer glow
    [[nodiscard]] static Vec3 evaluate_border_pixel_shading(
        float ribbon_u,
        float length_v,
        const Vec3& border_base_color,
        float time_s,
        bool is_selected_or_at_war = false,
        float border_fade_start = 0.6f) noexcept;
};

} // namespace thunder
