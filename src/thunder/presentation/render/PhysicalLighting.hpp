#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace thunder {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
    [[nodiscard]] float dot(const Vec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }
    [[nodiscard]] float length() const noexcept { return std::sqrt(dot(*this)); }
    [[nodiscard]] Vec3 normalize() const noexcept {
        float l = length();
        return l > 1e-6f ? (*this) * (1.0f / l) : *this;
    }
};

struct Mat4 {
    std::array<float, 16> m{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static Mat4 identity() noexcept { return Mat4{}; }
    static Mat4 ortho(float left, float right, float bottom, float top, float near_val, float far_val) noexcept;
};

struct CascadeSplit {
    float near_dist = 0.1f;
    float far_dist = 100.0f;
    Mat4 light_view_proj{};
};

class CascadedShadowMaps {
public:
    static constexpr std::size_t cascade_count = 4;

    static std::array<CascadeSplit, cascade_count> calculate_splits(
        float camera_near, float camera_far, float lambda,
        const Vec3& light_dir, const Mat4& camera_view_inv) noexcept;
};

/// Cinematic Microfacet PBR Shader Evaluator (Cook-Torrance GGX)
class PbrLighting {
public:
    [[nodiscard]] static float distribution_ggx(float n_dot_h, float roughness) noexcept;
    [[nodiscard]] static float geometry_smith(float n_dot_v, float n_dot_l, float roughness) noexcept;
    [[nodiscard]] static Vec3 fresnel_schlick(float cos_theta, const Vec3& f0) noexcept;

    [[nodiscard]] static Vec3 evaluate_direct_light(
        const Vec3& n, const Vec3& v, const Vec3& l,
        const Vec3& albedo, float metallic, float roughness,
        const Vec3& light_radiance) noexcept;

    [[nodiscard]] static Vec3 evaluate_subsurface(
        const Vec3& n, const Vec3& v, const Vec3& l,
        const Vec3& sss_color, float thickness, float distortion = 0.5f) noexcept;
};

class AtmosphericScattering {
public:
    // Physical Rayleigh scattering coefficients for RGB wavelengths (680nm, 550nm, 440nm)
    static constexpr Vec3 beta_rayleigh{5.802e-6f, 13.558e-6f, 33.100e-6f};
    // Mie scattering coefficient
    static constexpr float beta_mie = 21.0e-6f;
    // Scale heights in meters
    static constexpr float scale_height_rayleigh = 8000.0f;
    static constexpr float scale_height_mie = 1200.0f;

    static float phase_rayleigh(float cos_theta) noexcept;
    static float phase_mie(float cos_theta, float g = 0.76f) noexcept;
    static Vec3 compute_sky_color(const Vec3& view_dir, const Vec3& sun_dir, float altitude = 100.0f) noexcept;

    // Volumetric dual-exponential height fog
    [[nodiscard]] static Vec3 compute_volumetric_fog(
        const Vec3& ray_orig, const Vec3& ray_dir, float distance,
        const Vec3& sun_dir, const Vec3& sun_color, const Vec3& ambient_color) noexcept;
};

class ToneMappingPostProcess {
public:
    // ACES Filmic curve tone mapping
    [[nodiscard]] static Vec3 aces_filmic(const Vec3& hdr_color, float exposure = 1.0f) noexcept;
    [[nodiscard]] static float aces_filmic(float x) noexcept;

    // Warm archival painting / early-photography color grading
    [[nodiscard]] static Vec3 apply_warm_archival_grading(const Vec3& ldr_color, float vignette_factor = 0.0f) noexcept;
};

} // namespace thunder
