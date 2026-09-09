#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct Vector3D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct GerstnerWave {
    float dir_x = 1.0f;
    float dir_y = 0.0f;
    float amplitude = 1.0f;
    float wavelength = 50.0f;
    float speed = 2.0f;
    float steepness = 0.5f;
};

struct PhysicalWaterUniforms {
    float time_seconds = 0.0f;
    Vector3D sun_direction{0.5773f, 0.5773f, 0.5773f};
    std::uint32_t shallow_color = 0xff30a0d0u;
    std::uint32_t deep_color = 0xff0a1e3cu;
    float absorption_mu = 0.15f;
    float fresnel_r0 = 0.02f;
    float coast_foam_width_m = 12.0f;
};

class PhysicalWaterEvaluator {
public:
    [[nodiscard]] static Vector3D evaluate_displacement(float x, float y, float time_s,
                                                        std::span<const GerstnerWave> waves) noexcept;

    [[nodiscard]] static Vector3D evaluate_normal(float x, float y, float time_s,
                                                  std::span<const GerstnerWave> waves) noexcept;

    [[nodiscard]] static float evaluate_fresnel(float cos_theta, float r0 = 0.02f) noexcept;

    [[nodiscard]] static float evaluate_transmission(float water_depth_m, float mu = 0.15f) noexcept;

    // Multi-spectral chromatic extinction (Beer-Lambert over RGB)
    [[nodiscard]] static Vector3D evaluate_spectral_transmission(
        float water_depth_m, Vector3D mu_rgb = {0.22f, 0.06f, 0.02f}) noexcept;

    // Sun glitter / micro-facet ocean surface specular sparkle
    [[nodiscard]] static float evaluate_sun_glitter(
        const Vector3D& normal, const Vector3D& view_dir, const Vector3D& sun_dir,
        float roughness = 0.08f) noexcept;

    // Dynamic underwater caustic irradiance
    [[nodiscard]] static float evaluate_caustics(
        float x, float y, float depth_m, float time_s) noexcept;

    [[nodiscard]] static float evaluate_coast_foam(
        float coast_dist_m, float wave_z, float max_foam_width = 15.0f) noexcept;
};

} // namespace thunder
