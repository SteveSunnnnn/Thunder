#include "thunder/presentation/render/terrain/TerrainMaterialShading.hpp"

namespace thunder {

TerrainPbrMaterial TerrainMaterialEvaluator::default_material_for_biome(TerrainBiomeKind biome) noexcept {
    switch (biome) {
    case TerrainBiomeKind::Grassland:
        return {Vec3{0.18f, 0.32f, 0.12f}, 0.82f, 0.0f, 1.0f, 12.0f};
    case TerrainBiomeKind::Forest:
        return {Vec3{0.12f, 0.24f, 0.09f}, 0.88f, 0.0f, 1.2f, 16.0f};
    case TerrainBiomeKind::Desert:
        return {Vec3{0.72f, 0.58f, 0.38f}, 0.75f, 0.0f, 0.8f, 8.0f};
    case TerrainBiomeKind::MountainRock:
        return {Vec3{0.35f, 0.33f, 0.31f}, 0.65f, 0.0f, 1.8f, 20.0f};
    case TerrainBiomeKind::Tundra:
        return {Vec3{0.40f, 0.38f, 0.30f}, 0.80f, 0.0f, 1.0f, 10.0f};
    case TerrainBiomeKind::GlacierSnow:
        return {Vec3{0.92f, 0.94f, 0.98f}, 0.35f, 0.0f, 0.5f, 6.0f};
    default:
        return {Vec3{0.3f, 0.3f, 0.3f}, 0.8f, 0.0f, 1.0f, 10.0f};
    }
}

Vec3 TerrainMaterialEvaluator::compute_triplanar_weights(const Vec3& normal, float sharpness) noexcept {
    float x = std::pow(std::abs(normal.x), sharpness);
    float y = std::pow(std::abs(normal.y), sharpness);
    float z = std::pow(std::abs(normal.z), sharpness);
    float sum = x + y + z;
    if (sum < 1e-5f) return {0.0f, 0.0f, 1.0f};
    float inv = 1.0f / sum;
    return {x * inv, y * inv, z * inv};
}

float TerrainMaterialEvaluator::evaluate_macro_variation(float x, float y, float scale) noexcept {
    // Fast 2-octave value noise approximation for continental color breakdown
    float nx = x * scale;
    float ny = y * scale;
    float s1 = std::sin(nx * 1.3f + ny * 0.7f) * std::cos(ny * 1.1f - nx * 0.8f);
    float s2 = std::sin(nx * 3.7f - ny * 2.3f) * 0.5f;
    return (s1 + s2) * 0.15f; // [-0.225, +0.225]
}

TerrainShadingOutput TerrainMaterialEvaluator::evaluate(const TerrainShadingInput& input) noexcept {
    TerrainShadingOutput out;

    // 1. Biome Material Interpolation
    const auto mat_a = default_material_for_biome(input.primary_biome);
    const auto mat_b = default_material_for_biome(input.secondary_biome);
    const float t = std::clamp(input.biome_blend, 0.0f, 1.0f);

    Vec3 base_albedo = mat_a.albedo * (1.0f - t) + mat_b.albedo * t;
    float roughness = mat_a.roughness * (1.0f - t) + mat_b.roughness * t;

    // 2. Macro color variation (prevents tiling on huge maps)
    const float macro_var = evaluate_macro_variation(input.world_pos.x, input.world_pos.y);
    base_albedo = base_albedo * (1.0f + macro_var);

    // 3. Triplanar Cliff Projection (Steep slopes transition to vertical MountainRock with geological strata)
    const float cliff_factor = std::clamp((input.slope - 0.35f) * 3.0f, 0.0f, 1.0f);
    if (cliff_factor > 0.0f) {
        const auto rock_mat = default_material_for_biome(TerrainBiomeKind::MountainRock);
        const auto tri_weights = compute_triplanar_weights(input.world_normal);
        // Triplanar albedo modulation with geological sedimentary strata banding
        const float strata = 0.5f + 0.5f * std::sin(input.world_pos.z * 0.15f);
        const Vec3 cliff_albedo = rock_mat.albedo * (0.85f + tri_weights.z * 0.2f + strata * 0.15f);
        base_albedo = base_albedo * (1.0f - cliff_factor) + cliff_albedo * cliff_factor;
        roughness = roughness * (1.0f - cliff_factor) + rock_mat.roughness * cliff_factor;
    }

    // 4. Shoreline Moisture & Wet Sand Darkening
    float wetness = 0.0f;
    if (input.coast_distance_m < 30.0f && input.elevation_m < (input.water_level_m + 5.0f)) {
        const float dist_factor = std::clamp(1.0f - (input.coast_distance_m / 30.0f), 0.0f, 1.0f);
        const float elev_factor = std::clamp(1.0f - std::max(0.0f, (input.elevation_m - input.water_level_m) / 5.0f), 0.0f, 1.0f);
        wetness = dist_factor * elev_factor;
        // Wet ground is darker and significantly smoother/glossier
        base_albedo = base_albedo * (1.0f - wetness * 0.45f);
        roughness = std::max(0.08f, roughness * (1.0f - wetness * 0.75f));
    }
    out.wetness = wetness;

    // 5. Dynamic Snow Accumulation (Temperature + Elevation + Slope)
    // Snow does not stick to vertical cliffs, accumulates on flat plateaus
    float snow_factor = input.seasonal_snow_cover;
    if (input.temperature_c < 0.0f) {
        snow_factor = std::max(snow_factor, std::clamp(-input.temperature_c * 0.1f, 0.0f, 1.0f));
    }
    // High altitude freezing
    if (input.elevation_m > 2500.0f) {
        snow_factor = std::max(snow_factor, std::clamp((input.elevation_m - 2500.0f) / 1500.0f, 0.0f, 1.0f));
    }
    // Slope clearance: snow slides off steep rock faces
    const float snow_slope_adhesion = std::clamp(1.0f - (input.slope - 0.4f) * 2.5f, 0.0f, 1.0f);
    const float final_snow = snow_factor * snow_slope_adhesion;
    out.snow_accumulation = final_snow;

    if (final_snow > 0.0f) {
        const auto snow_mat = default_material_for_biome(TerrainBiomeKind::GlacierSnow);
        // Snow sparkles: high specular with sub-surface translucency
        base_albedo = base_albedo * (1.0f - final_snow) + snow_mat.albedo * final_snow;
        roughness = roughness * (1.0f - final_snow) + snow_mat.roughness * final_snow;
    }

    out.surface_albedo = base_albedo;
    out.surface_normal = input.world_normal.normalize();
    out.roughness = roughness;

    // 6. Full Microfacet PBR Lighting
    const Vec3 direct = PbrLighting::evaluate_direct_light(
        out.surface_normal,
        input.view_dir.normalize(),
        input.sun_dir.normalize(),
        out.surface_albedo,
        0.0f, // Dielectric terrain
        out.roughness,
        input.sun_color
    );

    // Hemispheric ambient lighting (sky irradiance + ground bounce) with slope ravine occlusion
    const float ao = std::clamp(1.0f - input.slope * 0.40f, 0.40f, 1.0f);
    const float n_up = std::clamp(out.surface_normal.z * 0.5f + 0.5f, 0.0f, 1.0f);
    const Vec3 ambient = input.ambient_color * n_up * out.surface_albedo * ao;

    out.lit_color = direct + ambient;
    return out;
}

} // namespace thunder
