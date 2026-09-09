#pragma once

#include "thunder/presentation/render/PhysicalLighting.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace thunder {

struct CloudLayerUniforms {
    float cloud_coverage = 0.45f;       // [0, 1] Cloudiness fraction
    float cloud_density = 0.8f;         // Optical thickness
    float cloud_altitude_m = 4000.0f;   // Base cloud ceiling height
    float cloud_thickness_m = 1200.0f;  // Vertical cloud depth
    Vec3 wind_velocity{15.0f, 5.0f, 0.0f}; // Wind drift in m/s
    float time_seconds = 0.0f;
    float shadow_softness = 0.35f;
    float shadow_intensity = 0.55f;     // Ground shadow darkness
};

struct SkyAtmosphereOutput {
    Vec3 sky_zenith_color{};
    Vec3 sky_horizon_color{};
    Vec3 sun_direct_radiance{};
    Vec3 ambient_ground_irradiance{};
    float atmospheric_turbidity = 2.0f;
};

class VolumetricAtmosphereAndClouds {
public:
    // Calculates physical solar spectral radiance and sky dome gradients based on sun elevation angle
    [[nodiscard]] static SkyAtmosphereOutput compute_sky_environment(
        const Vec3& sun_direction, float turbidity = 2.0f) noexcept;

    // Evaluates 2D/3D procedural cloud density at a given world position and time
    [[nodiscard]] static float evaluate_cloud_density(
        float world_x, float world_y, float time_s, float coverage = 0.45f) noexcept;

    // Computes moving cloud shadow attenuation factor [0 = deep shadow, 1 = full sunlight] on 3D terrain
    [[nodiscard]] static float evaluate_terrain_cloud_shadow(
        const Vec3& terrain_world_pos,
        const Vec3& sun_dir,
        const CloudLayerUniforms& uniforms) noexcept;

    // Evaluates volumetric raymarching light extinction through atmospheric cloud layer
    [[nodiscard]] static Vec3 sample_cloud_lighting(
        const Vec3& ray_origin,
        const Vec3& ray_dir,
        float max_dist,
        const Vec3& sun_dir,
        const SkyAtmosphereOutput& sky_env,
        const CloudLayerUniforms& uniforms) noexcept;
};

} // namespace thunder
