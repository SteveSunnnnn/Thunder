#include "thunder/presentation/render/water/RiverSplineFlowPass.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

float RiverSplineFlowPass::compute_rapids_factor(float slope_gradient, float flow_speed) noexcept {
    // Steeper descent + fast water produces turbulent white rapids
    const float turbulent_energy = slope_gradient * flow_speed;
    return std::clamp((turbulent_energy - 0.08f) * 4.0f, 0.0f, 1.0f);
}

Vec3 RiverSplineFlowPass::evaluate_river_lighting(
    const Vec3& world_pos,
    const Vec3& base_normal,
    const Vec3& flow_dir,
    float time_s,
    float depth_m,
    float rapids_factor,
    const Vec3& view_dir,
    const Vec3& sun_dir,
    const Vec3& sun_color,
    const Vec3& ambient_color) noexcept {
    
    // 1. Two-phase scrolling flow map normal perturbation
    const float flow_phase1 = time_s * 0.8f - std::floor(time_s * 0.8f);
    const float flow_phase2 = (time_s * 0.8f + 0.5f) - std::floor(time_s * 0.8f + 0.5f);

    const float u1 = world_pos.x * 0.05f + flow_dir.x * flow_phase1;
    const float v1 = world_pos.y * 0.05f + flow_dir.y * flow_phase1;
    const float u2 = world_pos.x * 0.05f + flow_dir.x * flow_phase2;
    const float v2 = world_pos.y * 0.05f + flow_dir.y * flow_phase2;

    const float n_x = (std::sin(u1 * 12.0f) * 0.5f + std::sin(u2 * 12.0f) * 0.5f) * 0.15f;
    const float n_y = (std::cos(v1 * 12.0f) * 0.5f + std::cos(v2 * 12.0f) * 0.5f) * 0.15f;

    Vec3 perturbed_normal = (base_normal + Vec3{n_x, n_y, 0.0f}).normalize();

    // 2. Beer-Lambert spectral transmission (clear river water)
    const Vec3 shallow_river_color{0.25f, 0.55f, 0.50f}; // Mountain river emerald tint
    const Vec3 deep_river_color{0.08f, 0.22f, 0.32f};
    const float depth_factor = std::clamp(depth_m / 4.0f, 0.0f, 1.0f);
    Vec3 water_albedo = shallow_river_color * (1.0f - depth_factor) + deep_river_color * depth_factor;

    // 3. White rapids foam blending
    if (rapids_factor > 0.01f) {
        const float foam_noise = std::sin(world_pos.x * 2.0f + time_s * 5.0f) * std::cos(world_pos.y * 2.0f - time_s * 4.0f);
        const float foam_mask = std::clamp(rapids_factor + foam_noise * 0.25f, 0.0f, 1.0f);
        const Vec3 white_foam{0.92f, 0.95f, 0.98f};
        water_albedo = water_albedo * (1.0f - foam_mask) + white_foam * foam_mask;
    }

    // 4. Fresnel & Specular Sun reflection
    const float cos_theta = std::clamp(view_dir.dot(perturbed_normal), 0.0f, 1.0f);
    const float fresnel = 0.02f + 0.98f * std::pow(1.0f - cos_theta, 5.0f);

    const Vec3 h = (view_dir + sun_dir).normalize();
    const float n_dot_h = std::clamp(perturbed_normal.dot(h), 0.0f, 1.0f);
    const float specular = std::pow(n_dot_h, 128.0f) * 2.5f;

    const Vec3 direct_specular = sun_color * (specular * fresnel);
    const Vec3 diffuse_body = water_albedo * (sun_color * 0.4f + ambient_color);

    return diffuse_body + direct_specular;
}

} // namespace thunder
