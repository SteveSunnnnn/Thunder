#include "thunder/presentation/render/vegetation/ForestCanopyInstancer.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

Vec3 ForestCanopyInstancer::evaluate_foliage_albedo(
    std::uint32_t species_id, FoliageSeason season, float seasonal_progress, float variation_seed) noexcept {
    
    Vec3 base{};
    if (species_id == 1) {
        // Conifer Pine/Fir: Dark evergreen throughout year, frost tips in winter
        base = Vec3{0.08f, 0.22f, 0.09f};
        if (season == FoliageSeason::Winter) {
            base = base * 0.8f + Vec3{0.3f, 0.35f, 0.4f} * 0.3f; // Frost dusted
        }
    } else if (species_id == 2) {
        // Tropical Palm: Bright vibrant tropical green
        base = Vec3{0.18f, 0.45f, 0.12f};
    } else {
        // Temperate Broadleaf Oak/Birch: Dynamic 4-season cycle
        switch (season) {
        case FoliageSeason::Spring:
            base = Vec3{0.25f, 0.48f, 0.15f}; // Tender light yellow-green
            break;
        case FoliageSeason::Summer:
            base = Vec3{0.12f, 0.35f, 0.10f}; // Deep lush chlorophyll green
            break;
        case FoliageSeason::Autumn: {
            // Gradient from golden yellow to burnt orange and deep crimson
            const Vec3 gold{0.82f, 0.58f, 0.12f};
            const Vec3 crimson{0.75f, 0.22f, 0.10f};
            const float t = std::clamp(seasonal_progress + variation_seed * 0.3f, 0.0f, 1.0f);
            base = gold * (1.0f - t) + crimson * t;
            break;
        }
        case FoliageSeason::Winter:
            base = Vec3{0.35f, 0.28f, 0.22f}; // Barren brown twigs & branches
            break;
        }
    }

    // Individual tree micro-variation
    const float var = 1.0f + (variation_seed - 0.5f) * 0.15f;
    return base * var;
}

Vec3 ForestCanopyInstancer::evaluate_wind_displacement(
    const Vec3& tree_base_pos, float height_fraction, float time_s, const Vec3& wind_dir_vel) noexcept {
    
    const float wind_speed = wind_dir_vel.length();
    if (wind_speed < 0.1f) return Vec3{0.0f, 0.0f, 0.0f};

    const Vec3 wind_dir = wind_dir_vel * (1.0f / wind_speed);

    // 1. Low-frequency main trunk bending (quadratic with height)
    const float spatial_phase = (tree_base_pos.x * 0.05f + tree_base_pos.y * 0.03f);
    const float gust = std::sin(time_s * 1.5f + spatial_phase) * std::cos(time_s * 0.8f + spatial_phase * 0.7f);
    const float trunk_bend = (gust * 0.5f + 0.5f) * (wind_speed * 0.08f) * (height_fraction * height_fraction);

    // 2. High-frequency leaf & branch flutter
    const float flutter_phase = time_s * 6.0f + (tree_base_pos.x * 0.7f + tree_base_pos.y * 1.1f);
    const float flutter = std::sin(flutter_phase) * 0.15f * height_fraction * (wind_speed * 0.1f);

    const Vec3 total_disp = wind_dir * (trunk_bend + flutter);
    return total_disp;
}

ForestShadingOutput ForestCanopyInstancer::evaluate_canopy_lighting(
    const Vec3& world_pos,
    const Vec3& canopy_normal,
    const Vec3& view_dir,
    const Vec3& sun_dir,
    const Vec3& sun_color,
    const Vec3& ambient_color,
    std::uint32_t species_id,
    FoliageSeason season,
    float sss_strength) noexcept {
    
    ForestShadingOutput out;
    const float seed = std::sin(world_pos.x * 0.1f + world_pos.y * 0.2f) * 0.5f + 0.5f;
    out.foliage_albedo = evaluate_foliage_albedo(species_id, season, 0.5f, seed);
    out.normal = canopy_normal.normalize();

    // Direct lighting with softened normals (wrapped diffuse for dense foliage volumes)
    const float n_dot_l = out.normal.dot(sun_dir.normalize());
    const float wrap_diffuse = std::clamp((n_dot_l + 0.35f) / 1.35f, 0.0f, 1.0f);
    const Vec3 direct = out.foliage_albedo * sun_color * wrap_diffuse;

    // Subsurface leaf light transmission (backlighting glow through leaves)
    const float back_light = std::clamp(-view_dir.dot(sun_dir), 0.0f, 1.0f);
    const Vec3 sss = out.foliage_albedo * sun_color * (std::pow(back_light, 3.0f) * sss_strength);

    // Ambient light
    const Vec3 ambient = out.foliage_albedo * ambient_color;

    out.lit_color = direct + sss + ambient;
    out.sss_factor = sss_strength;
    return out;
}

} // namespace thunder
