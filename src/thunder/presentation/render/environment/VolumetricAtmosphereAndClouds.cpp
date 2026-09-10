#include "thunder/presentation/render/environment/VolumetricAtmosphereAndClouds.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

namespace {
float hash21(float x, float y) noexcept {
    float n = std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f;
    return n - std::floor(n);
}

float smooth_noise2d(float x, float y) noexcept {
    float ix = std::floor(x);
    float iy = std::floor(y);
    float fx = x - ix;
    float fy = y - iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float a = hash21(ix, iy);
    float b = hash21(ix + 1.0f, iy);
    float c = hash21(ix, iy + 1.0f);
    float d = hash21(ix + 1.0f, iy + 1.0f);

    return a * (1.0f - ux) * (1.0f - uy) +
           b * ux * (1.0f - uy) +
           c * (1.0f - ux) * uy +
           d * ux * uy;
}

float fractal_cloud_fbm(float x, float y) noexcept {
    float v = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    for (int i = 0; i < 4; ++i) {
        v += amp * smooth_noise2d(x * freq, y * freq);
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return v;
}
} // namespace

SkyAtmosphereOutput VolumetricAtmosphereAndClouds::compute_sky_environment(
    const Vec3& sun_direction, float /*turbidity*/) noexcept {
    
    SkyAtmosphereOutput out;
    const float sun_cos_zenith = std::clamp(sun_direction.z, -1.0f, 1.0f);
    const float sun_elev = std::asin(sun_cos_zenith);

    if (sun_elev > 0.05f) {
        // Daytime: Rayleigh sky dome with warm solar disk
        const float day_factor = std::clamp(sun_elev / 0.5f, 0.0f, 1.0f);
        out.sky_zenith_color = Vec3{0.18f, 0.42f, 0.78f} * day_factor + Vec3{0.08f, 0.15f, 0.28f} * (1.0f - day_factor);
        out.sky_horizon_color = Vec3{0.65f, 0.75f, 0.85f} * day_factor + Vec3{0.85f, 0.45f, 0.22f} * (1.0f - day_factor);
        out.sun_direct_radiance = Vec3{1.4f, 1.35f, 1.25f} * std::clamp(sun_cos_zenith, 0.1f, 1.0f);
        out.ambient_ground_irradiance = Vec3{0.25f, 0.32f, 0.45f} * day_factor;
    } else if (sun_elev > -0.1f) {
        // Sunset / Golden hour / Twilight
        const float sunset_t = (sun_elev + 0.1f) / 0.15f;
        out.sky_zenith_color = Vec3{0.08f, 0.15f, 0.30f} * sunset_t + Vec3{0.01f, 0.02f, 0.05f} * (1.0f - sunset_t);
        out.sky_horizon_color = Vec3{0.95f, 0.48f, 0.18f} * sunset_t + Vec3{0.20f, 0.10f, 0.15f} * (1.0f - sunset_t);
        out.sun_direct_radiance = Vec3{1.6f, 0.7f, 0.2f} * sunset_t; // Vivid golden-orange sunset rays
        out.ambient_ground_irradiance = Vec3{0.15f, 0.12f, 0.18f};
    } else {
        // Night: Deep indigo celestial dome with moonlight
        out.sky_zenith_color = Vec3{0.005f, 0.010f, 0.025f};
        out.sky_horizon_color = Vec3{0.015f, 0.025f, 0.045f};
        out.sun_direct_radiance = Vec3{0.05f, 0.06f, 0.09f}; // Soft lunar illumination
        out.ambient_ground_irradiance = Vec3{0.02f, 0.03f, 0.05f};
    }

    return out;
}

float VolumetricAtmosphereAndClouds::evaluate_cloud_density(
    float world_x, float world_y, float time_s, float coverage) noexcept {
    const float cov = std::clamp(coverage, 0.0f, 1.0f);
    if (cov <= 1e-4f) return 0.0f;

    // Cloud coordinate with wind translation
    const float scale = 0.00015f; // Continental cloud scale
    const float u = world_x * scale + time_s * 0.005f;
    const float v = world_y * scale + time_s * 0.002f;

    const float fbm = fractal_cloud_fbm(u, v);
    // Threshold with coverage
    const float threshold = 1.0f - cov;
    if (fbm <= threshold) return 0.0f;

    const float density = (fbm - threshold) / cov;
    return std::clamp(density * density, 0.0f, 1.0f);
}

float VolumetricAtmosphereAndClouds::evaluate_terrain_cloud_shadow(
    const Vec3& terrain_world_pos,
    const Vec3& sun_dir,
    const CloudLayerUniforms& uniforms) noexcept {
    
    if (sun_dir.z <= 0.05f) return 1.0f; // Night/low sun: no sharp cloud shadows

    // Project terrain point along sun direction up to cloud ceiling
    const float delta_z = uniforms.cloud_altitude_m - terrain_world_pos.z;
    if (delta_z <= 0.0f) return 1.0f;

    const float t_proj = delta_z / sun_dir.z;
    const float cloud_x = terrain_world_pos.x + sun_dir.x * t_proj - uniforms.wind_velocity.x * uniforms.time_seconds;
    const float cloud_y = terrain_world_pos.y + sun_dir.y * t_proj - uniforms.wind_velocity.y * uniforms.time_seconds;

    const float cloud_density = evaluate_cloud_density(cloud_x, cloud_y, uniforms.time_seconds, uniforms.cloud_coverage);
    if (cloud_density <= 0.01f) return 1.0f;

    // Beer-Lambert light attenuation through cloud layer
    const float shadow_factor = 1.0f - std::clamp(cloud_density * uniforms.shadow_intensity, 0.0f, 0.95f);
    return shadow_factor;
}

Vec3 VolumetricAtmosphereAndClouds::sample_cloud_lighting(
    const Vec3& ray_origin,
    const Vec3& ray_dir,
    float max_dist,
    const Vec3& sun_dir,
    const SkyAtmosphereOutput& sky_env,
    const CloudLayerUniforms& uniforms) noexcept {

    // Simple 4-step volumetric raymarch through cloud slab
    const float slab_bottom = uniforms.cloud_altitude_m;
    const float slab_top = uniforms.cloud_altitude_m + uniforms.cloud_thickness_m;

    if (std::abs(ray_dir.z) < 1e-4f) return Vec3{0.0f, 0.0f, 0.0f};

    const float t0 = (slab_bottom - ray_origin.z) / ray_dir.z;
    const float t1 = (slab_top - ray_origin.z) / ray_dir.z;
    const float t_enter = std::max(0.0f, std::min(t0, t1));
    const float t_exit = std::min(max_dist, std::max(t0, t1));

    if (t_exit <= t_enter) return Vec3{0.0f, 0.0f, 0.0f};

    const float step_size = (t_exit - t_enter) / 4.0f;
    Vec3 accumulated_color{0.0f, 0.0f, 0.0f};
    float transmittance = 1.0f;

    for (int i = 0; i < 4; ++i) {
        const float t = t_enter + (static_cast<float>(i) + 0.5f) * step_size;
        const Vec3 pos = ray_origin + ray_dir * t;
        const float density = evaluate_cloud_density(
            pos.x - uniforms.wind_velocity.x * uniforms.time_seconds,
            pos.y - uniforms.wind_velocity.y * uniforms.time_seconds,
            uniforms.time_seconds,
            uniforms.cloud_coverage
        );

        if (density > 0.01f) {
            // Forward Mie scattering towards sun
            const float cos_theta = std::clamp(ray_dir.dot(sun_dir), -1.0f, 1.0f);
            const float mie_phase = AtmosphericScattering::phase_mie(cos_theta, 0.75f);
            const Vec3 step_light = (sky_env.sun_direct_radiance * (mie_phase * 2.0f) + sky_env.ambient_ground_irradiance) * density;

            accumulated_color = accumulated_color + step_light * (transmittance * step_size * 0.001f);
            transmittance *= std::exp(-density * uniforms.cloud_density * step_size * 0.002f);
            if (transmittance < 0.05f) break;
        }
    }

    return accumulated_color;
}

} // namespace thunder
