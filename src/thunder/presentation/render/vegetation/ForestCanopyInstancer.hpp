#pragma once

#include "thunder/presentation/render/PhysicalLighting.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum class FoliageSeason : std::uint8_t {
    Spring = 0, // Fresh light green
    Summer = 1, // Lush deep emerald
    Autumn = 2, // Vibrant amber, golden ochre, crimson
    Winter = 3  // Frosty evergreen & bare branches
};

struct TreeInstanceGpu {
    Vec3 position{};
    float scale = 1.0f;
    float yaw = 0.0f;
    std::uint32_t species_id = 0; // 0=Broadleaf Oak/Birch, 1=Conifer Pine/Spruce, 2=Palm
    float seasonal_tint = 0.0f;   // [0, 1]
};

struct ForestShadingOutput {
    Vec3 lit_color{};
    Vec3 foliage_albedo{};
    Vec3 normal{};
    float sss_factor = 0.35f; // Subsurface leaf translucency
};

class ForestCanopyInstancer {
public:
    // Computes seasonal foliage base albedo palette
    [[nodiscard]] static Vec3 evaluate_foliage_albedo(
        std::uint32_t species_id,
        FoliageSeason season,
        float seasonal_progress = 0.5f,
        float variation_seed = 0.0f) noexcept;

    // Evaluates 3D canopy wind sway displacement (hierarchical trunk sway + branch flutter)
    [[nodiscard]] static Vec3 evaluate_wind_displacement(
        const Vec3& tree_base_pos,
        float height_fraction,
        float time_s,
        const Vec3& wind_dir_vel) noexcept;

    // Evaluates microfacet PBR + Subsurface leaf scattering (SSS) for tree canopies
    [[nodiscard]] static ForestShadingOutput evaluate_canopy_lighting(
        const Vec3& world_pos,
        const Vec3& canopy_normal,
        const Vec3& view_dir,
        const Vec3& sun_dir,
        const Vec3& sun_color,
        const Vec3& ambient_color,
        std::uint32_t species_id,
        FoliageSeason season,
        float sss_strength = 0.45f) noexcept;
};

} // namespace thunder
