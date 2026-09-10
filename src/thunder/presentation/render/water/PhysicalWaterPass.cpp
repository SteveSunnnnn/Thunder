#include "thunder/presentation/render/water/PhysicalWaterPass.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

Vector3D PhysicalWaterEvaluator::evaluate_displacement(float x, float y, float time_s,
                                                      std::span<const GerstnerWave> waves) noexcept {
    Vector3D result{x, y, 0.0f};
    if (waves.empty()) return result;

    for (const auto& w : waves) {
        const float k = 2.0f * 3.1415926535f / std::max(1.0f, w.wavelength);
        const float c = std::sqrt(9.81f / k) * w.speed;
        const float len_d = std::sqrt(w.dir_x * w.dir_x + w.dir_y * w.dir_y);
        const float dx = len_d > 1e-4f ? w.dir_x / len_d : 1.0f;
        const float dy = len_d > 1e-4f ? w.dir_y / len_d : 0.0f;

        const float phase = k * (dx * x + dy * y) - c * k * time_s;
        const float cos_p = std::cos(phase);
        const float sin_p = std::sin(phase);
        const float denom = k * w.amplitude * static_cast<float>(waves.size());
        const float q = denom > 1e-6f ? w.steepness / denom : 0.0f;

        result.x += q * w.amplitude * dx * cos_p;
        result.y += q * w.amplitude * dy * cos_p;
        result.z += w.amplitude * sin_p;
    }
    return result;
}

Vector3D PhysicalWaterEvaluator::evaluate_normal(float x, float y, float time_s,
                                                std::span<const GerstnerWave> waves) noexcept {
    Vector3D n{0.0f, 0.0f, 1.0f};
    if (waves.empty()) return n;

    float d_x = 0.0f;
    float d_y = 0.0f;

    for (const auto& w : waves) {
        const float k = 2.0f * 3.1415926535f / std::max(1.0f, w.wavelength);
        const float c = std::sqrt(9.81f / k) * w.speed;
        const float len_d = std::sqrt(w.dir_x * w.dir_x + w.dir_y * w.dir_y);
        const float dx = len_d > 1e-4f ? w.dir_x / len_d : 1.0f;
        const float dy = len_d > 1e-4f ? w.dir_y / len_d : 0.0f;

        const float phase = k * (dx * x + dy * y) - c * k * time_s;
        const float cos_p = std::cos(phase);

        d_x += dx * k * w.amplitude * cos_p;
        d_y += dy * k * w.amplitude * cos_p;
    }

    n.x = -d_x;
    n.y = -d_y;
    n.z = 1.0f;

    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 1e-4f) {
        n.x /= len;
        n.y /= len;
        n.z /= len;
    }
    return n;
}

float PhysicalWaterEvaluator::evaluate_fresnel(float cos_theta, float r0) noexcept {
    const float ct = std::clamp(cos_theta, 0.0f, 1.0f);
    const float om = 1.0f - ct;
    const float om2 = om * om;
    const float om5 = om2 * om2 * om;
    return r0 + (1.0f - r0) * om5;
}

float PhysicalWaterEvaluator::evaluate_transmission(float water_depth_m, float mu) noexcept {
    const float d = std::max(0.0f, water_depth_m);
    return std::exp(-mu * d);
}

Vector3D PhysicalWaterEvaluator::evaluate_spectral_transmission(
    float water_depth_m, Vector3D mu_rgb) noexcept {
    const float d = std::max(0.0f, water_depth_m);
    return {
        std::exp(-mu_rgb.x * d),
        std::exp(-mu_rgb.y * d),
        std::exp(-mu_rgb.z * d)
    };
}

float PhysicalWaterEvaluator::evaluate_sun_glitter(
    const Vector3D& normal, const Vector3D& view_dir, const Vector3D& sun_dir, float roughness) noexcept {
    
    const float hx = view_dir.x + sun_dir.x;
    const float hy = view_dir.y + sun_dir.y;
    const float hz = view_dir.z + sun_dir.z;
    const float hlen = std::sqrt(hx * hx + hy * hy + hz * hz);
    if (hlen < 1e-4f) return 0.0f;

    const float nh = std::max(0.0f, (normal.x * hx + normal.y * hy + normal.z * hz) / hlen);
    const float a2 = roughness * roughness;
    const float denom = nh * nh * (a2 - 1.0f) + 1.0f;
    return (a2) / (3.1415926535f * denom * denom);
}

float PhysicalWaterEvaluator::evaluate_caustics(
    float x, float y, float depth_m, float time_s) noexcept {
    if (depth_m <= 0.0f) return 1.0f;

    // Dual-wave interference pattern for caustics projection
    const float scale1 = 0.2f;
    const float scale2 = 0.35f;
    const float c1 = std::sin(x * scale1 + time_s * 1.5f) * std::cos(y * scale1 + time_s * 1.2f);
    const float c2 = std::sin((x + y) * scale2 - time_s * 2.0f);

    const float caustic_val = std::pow(std::abs(c1 + c2) * 0.5f, 3.0f) * 2.0f;
    const float attenuation = std::exp(-0.1f * depth_m);
    return 1.0f + caustic_val * attenuation;
}

float PhysicalWaterEvaluator::evaluate_coast_foam(
    float coast_dist_m, float wave_z, float max_foam_width) noexcept {
    if (coast_dist_m <= 0.0f || coast_dist_m >= max_foam_width) return 0.0f;
    const float dist_factor = 1.0f - (coast_dist_m / max_foam_width);
    const float crest_factor = std::clamp(wave_z * 0.5f + 0.5f, 0.0f, 1.0f);
    return dist_factor * (0.6f + 0.4f * crest_factor);
}

} // namespace thunder
