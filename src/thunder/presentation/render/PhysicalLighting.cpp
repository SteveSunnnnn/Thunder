#include "thunder/presentation/render/PhysicalLighting.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace thunder {

Mat4 Mat4::ortho(float left, float right, float bottom, float top, float near_val, float far_val) noexcept {
    Mat4 res{};
    const float dx = right - left;
    const float dy = top - bottom;
    const float dz = far_val - near_val;
    if (std::abs(dx) < 1e-6f || std::abs(dy) < 1e-6f || std::abs(dz) < 1e-6f) {
        return res;
    }
    res.m[0] = 2.0f / dx;
    res.m[5] = 2.0f / dy;
    res.m[10] = -2.0f / dz;
    res.m[12] = -(right + left) / dx;
    res.m[13] = -(top + bottom) / dy;
    res.m[14] = -(far_val + near_val) / dz;
    res.m[15] = 1.0f;
    return res;
}

std::array<CascadeSplit, CascadedShadowMaps::cascade_count> CascadedShadowMaps::calculate_splits(
    float camera_near, float camera_far, float lambda,
    const Vec3& /*light_dir*/, const Mat4& /*camera_view_inv*/) noexcept {
    std::array<CascadeSplit, cascade_count> splits{};
    const float ratio = camera_far / std::max(1e-4f, camera_near);

    for (std::size_t i = 0; i < cascade_count; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(cascade_count);
        const float log_split = camera_near * std::pow(ratio, p);
        const float uniform_split = camera_near + (camera_far - camera_near) * p;
        const float split_dist = std::max(1e-3f, lambda * log_split + (1.0f - lambda) * uniform_split);

        splits[i].near_dist = (i == 0) ? camera_near : splits[i - 1].far_dist;
        splits[i].far_dist = split_dist;
        splits[i].light_view_proj = Mat4::ortho(-split_dist, split_dist, -split_dist, split_dist, -split_dist * 2.0f, split_dist * 2.0f);
    }
    return splits;
}

// ── PBR BRDF Evaluation ──

float PbrLighting::distribution_ggx(float n_dot_h, float roughness) noexcept {
    const float a = std::max(0.001f, roughness * roughness);
    const float a2 = a * a;
    const float nh2 = n_dot_h * n_dot_h;
    constexpr float kPi = 3.1415926535f;

    const float denom = (nh2 * (a2 - 1.0f) + 1.0f);
    return a2 / (kPi * denom * denom);
}

float PbrLighting::geometry_smith(float n_dot_v, float n_dot_l, float roughness) noexcept {
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    const float g1_v = n_dot_v / (n_dot_v * (1.0f - k) + k);
    const float g1_l = n_dot_l / (n_dot_l * (1.0f - k) + k);
    return g1_v * g1_l;
}

Vec3 PbrLighting::fresnel_schlick(float cos_theta, const Vec3& f0) noexcept {
    const float ct = std::clamp(cos_theta, 0.0f, 1.0f);
    const float om = 1.0f - ct;
    const float om5 = om * om * om * om * om;
    return f0 + (Vec3{1.0f, 1.0f, 1.0f} - f0) * om5;
}

Vec3 PbrLighting::evaluate_direct_light(
    const Vec3& n, const Vec3& v, const Vec3& l,
    const Vec3& albedo, float metallic, float roughness,
    const Vec3& light_radiance) noexcept {
    
    const Vec3 h = (v + l).normalize();
    const float n_dot_v = std::max(1e-4f, n.dot(v));
    const float n_dot_l = std::max(0.0f, n.dot(l));
    const float n_dot_h = std::max(0.0f, n.dot(h));
    const float v_dot_h = std::max(0.0f, v.dot(h));

    const Vec3 dielectrics_f0{0.04f, 0.04f, 0.04f};
    const Vec3 f0 = dielectrics_f0 * (1.0f - metallic) + albedo * metallic;

    const float d = distribution_ggx(n_dot_h, roughness);
    const float g = geometry_smith(n_dot_v, n_dot_l, roughness);
    const Vec3 f = fresnel_schlick(v_dot_h, f0);

    const Vec3 k_s = f;
    const Vec3 k_d = (Vec3{1.0f, 1.0f, 1.0f} - k_s) * (1.0f - metallic);

    constexpr float kPi = 3.1415926535f;
    const Vec3 numerator = k_s * (d * g);
    const float denom = 4.0f * n_dot_v * n_dot_l + 1e-4f;
    const Vec3 specular = numerator * (1.0f / denom);

    const Vec3 diffuse = albedo * k_d * (1.0f / kPi);

    return (diffuse + specular) * light_radiance * n_dot_l;
}

Vec3 PbrLighting::evaluate_subsurface(
    const Vec3& n, const Vec3& v, const Vec3& l,
    const Vec3& sss_color, float thickness, float distortion) noexcept {
    
    const Vec3 sss_l = (l + n * distortion).normalize();
    const float v_dot_sssl = std::clamp(-v.dot(sss_l), 0.0f, 1.0f);
    const float sss_intensity = std::pow(v_dot_sssl, 4.0f) * std::exp(-thickness * 2.0f);
    return sss_color * sss_intensity;
}

// ── Atmosphere & Fog ──

float AtmosphericScattering::phase_rayleigh(float cos_theta) noexcept {
    constexpr float kInv4Pi = 1.0f / (4.0f * 3.1415926535f);
    return (3.0f / 4.0f) * (1.0f + cos_theta * cos_theta) * kInv4Pi;
}

float AtmosphericScattering::phase_mie(float cos_theta, float g) noexcept {
    constexpr float kInv4Pi = 1.0f / (4.0f * 3.1415926535f);
    const float g2 = g * g;
    const float denom = std::pow(1.0f + g2 - 2.0f * g * cos_theta, 1.5f);
    return (1.0f - g2) / (denom + 1e-4f) * kInv4Pi;
}

Vec3 AtmosphericScattering::compute_sky_color(const Vec3& view_dir, const Vec3& sun_dir, float altitude) noexcept {
    const float cos_theta = std::clamp(view_dir.dot(sun_dir), -1.0f, 1.0f);
    const float ray_phase = phase_rayleigh(cos_theta);
    const float mie_phase = phase_mie(cos_theta, 0.76f);

    const float alt_factor = std::exp(-altitude / scale_height_rayleigh);
    const Vec3 rayleigh = beta_rayleigh * (ray_phase * alt_factor);
    const Vec3 mie = Vec3{beta_mie, beta_mie, beta_mie} * (mie_phase * std::exp(-altitude / scale_height_mie));

    const Vec3 sun_glare = Vec3{1.0f, 0.9f, 0.7f} * std::pow(std::max(0.0f, cos_theta), 32.0f) * 1.5f;

    return rayleigh * 18.0f + mie * 1.2f + sun_glare;
}

Vec3 AtmosphericScattering::compute_volumetric_fog(
    const Vec3& /*ray_orig*/, const Vec3& ray_dir, float distance,
    const Vec3& sun_dir, const Vec3& sun_color, const Vec3& ambient_color) noexcept {
    
    constexpr float kFogDensity = 0.00015f;
    const float fog_amount = 1.0f - std::exp(-distance * kFogDensity);

    const float sun_inscatter = std::max(0.0f, ray_dir.dot(sun_dir));
    const Vec3 fog_tint = ambient_color * 0.7f + sun_color * (std::pow(sun_inscatter, 8.0f) * 0.3f);

    return fog_tint * fog_amount;
}

// ── ACES & Color Grading ──

float ToneMappingPostProcess::aces_filmic(float x) noexcept {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

Vec3 ToneMappingPostProcess::aces_filmic(const Vec3& hdr, float exposure) noexcept {
    return {
        aces_filmic(hdr.x * exposure),
        aces_filmic(hdr.y * exposure),
        aces_filmic(hdr.z * exposure)
    };
}

Vec3 ToneMappingPostProcess::apply_warm_archival_grading(const Vec3& ldr, float vignette) noexcept {
    // Warm amber archival-painting color balance
    const float r = ldr.x * 1.05f + 0.02f;
    const float g = ldr.y * 1.00f + 0.01f;
    const float b = ldr.z * 0.92f;

    // Apply smooth corner vignette
    const float v = std::clamp(1.0f - vignette * 0.4f, 0.0f, 1.0f);
    return {
        std::clamp(r * v, 0.0f, 1.0f),
        std::clamp(g * v, 0.0f, 1.0f),
        std::clamp(b * v, 0.0f, 1.0f)
    };
}

} // namespace thunder
