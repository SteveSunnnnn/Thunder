#include "thunder/presentation/render/map/BorderMesh3D.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

std::vector<BorderSegmentVertexGpu> BorderMesh3D::generate_border_ribbon(
    std::span<const Vec3> spline_points,
    float border_width_m,
    std::uint32_t country_color,
    float glow_factor) {
    
    std::vector<BorderSegmentVertexGpu> vertices;
    if (spline_points.size() < 2) return vertices;

    vertices.reserve(spline_points.size() * 2);
    float accumulated_length = 0.0f;

    for (std::size_t i = 0; i < spline_points.size(); ++i) {
        const auto& p = spline_points[i];
        Vec3 forward{1.0f, 0.0f, 0.0f};

        if (i + 1 < spline_points.size()) {
            forward = (spline_points[i + 1] - p).normalize();
        } else if (i > 0) {
            forward = (p - spline_points[i - 1]).normalize();
        }

        if (i > 0) {
            accumulated_length += (p - spline_points[i - 1]).length();
        }

        // Side normal perpendicular to forward direction in XY horizontal plane
        const Vec3 side{-forward.y, forward.x, 0.0f};
        const float half_width = border_width_m * 0.5f;

        // Left vertex (-1.0)
        vertices.push_back({
            .position = p - side * half_width + Vec3{0.0f, 0.0f, 0.2f}, // Slight offset above ground to avoid z-fighting
            .normal = Vec3{0.0f, 0.0f, 1.0f},
            .tangent = forward,
            .ribbon_u = -1.0f,
            .length_v = accumulated_length,
            .country_color_abgr = country_color,
            .glow_intensity = glow_factor
        });

        // Right vertex (+1.0)
        vertices.push_back({
            .position = p + side * half_width + Vec3{0.0f, 0.0f, 0.2f},
            .normal = Vec3{0.0f, 0.0f, 1.0f},
            .tangent = forward,
            .ribbon_u = 1.0f,
            .length_v = accumulated_length,
            .country_color_abgr = country_color,
            .glow_intensity = glow_factor
        });
    }

    return vertices;
}

Vec3 BorderMesh3D::evaluate_border_pixel_shading(
    float ribbon_u,
    float length_v,
    const Vec3& border_base_color,
    float time_s,
    bool is_selected_or_at_war,
    float border_fade_start) noexcept {
    
    // Antialiased Gaussian/smooth profile across the ribbon width
    const float abs_u = std::abs(ribbon_u);
    if (abs_u >= 1.0f) return Vec3{0.0f, 0.0f, 0.0f};

    // Thunder solid line + soft outer glow falloff
    const float denom = 1.0f - border_fade_start;
    const float thunder_line = denom > 1e-4f
        ? 1.0f - std::clamp((abs_u - border_fade_start) / denom, 0.0f, 1.0f)
        : (abs_u < border_fade_start ? 1.0f : 0.0f);
    const float glow = std::exp(-abs_u * abs_u * 3.5f);

    float intensity = thunder_line * 0.8f + glow * 0.4f;

    if (is_selected_or_at_war) {
        // Dynamic pulse wave traveling along border + bright neon accent
        const float pulse = std::sin(length_v * 0.1f - time_s * 4.0f) * 0.35f + 0.65f;
        const float dash = std::sin(length_v * 0.05f) > 0.0f ? 1.0f : 0.4f; // Contested dashed line
        intensity *= pulse * dash * 1.5f;
    }

    return border_base_color * intensity;
}

} // namespace thunder
