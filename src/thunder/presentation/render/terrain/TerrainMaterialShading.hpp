#pragma once

#include "thunder/presentation/render/PhysicalLighting.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace thunder {

enum class TerrainBiomeKind : std::uint8_t {
    Grassland = 0,
    Forest = 1,
    Desert = 2,
    MountainRock = 3,
    Tundra = 4,
    GlacierSnow = 5,
    Count = 6
};

struct TerrainPbrMaterial {
    Vec3 albedo{0.2f, 0.35f, 0.15f};
    float roughness = 0.85f;
    float metallic = 0.0f;
    float normal_strength = 1.0f;
    float detail_scale = 10.0f;
};

struct TerrainShadingInput {
    Vec3 world_pos{};
    Vec3 world_normal{0.0f, 0.0f, 1.0f};
    Vec3 view_dir{0.0f, 0.0f, 1.0f};
    Vec3 sun_dir{0.577f, 0.577f, 0.577f};
    Vec3 sun_color{1.0f, 0.98f, 0.92f};
    Vec3 ambient_color{0.15f, 0.20f, 0.30f};
    
    TerrainBiomeKind primary_biome = TerrainBiomeKind::Grassland;
    TerrainBiomeKind secondary_biome = TerrainBiomeKind::Forest;
    float biome_blend = 0.0f;           // [0, 1]
    float slope = 0.0f;                 // [0, 1] 0=flat, 1=vertical cliff
    float elevation_m = 100.0f;
    float temperature_c = 15.0f;        // Seasonal temperature
    float seasonal_snow_cover = 0.0f;   // [0, 1]
    float coast_distance_m = 1000.0f;
    float water_level_m = 0.0f;
};

struct TerrainShadingOutput {
    Vec3 lit_color{};
    Vec3 surface_albedo{};
    Vec3 surface_normal{};
    float roughness = 0.8f;
    float snow_accumulation = 0.0f;
    float wetness = 0.0f;
};

class TerrainMaterialEvaluator {
public:
    static TerrainPbrMaterial default_material_for_biome(TerrainBiomeKind biome) noexcept;

    // Evaluates multi-layer triplanar PBR terrain shading with snow, slope cliff rocks, and shoreline wetness
    [[nodiscard]] static TerrainShadingOutput evaluate(const TerrainShadingInput& input) noexcept;

    // Triplanar blending weights for cliff projections
    [[nodiscard]] static Vec3 compute_triplanar_weights(const Vec3& normal, float sharpness = 4.0f) noexcept;

    // Dynamic procedural macro variation to break texture repetition over vast continental scales
    [[nodiscard]] static float evaluate_macro_variation(float x, float y, float scale = 0.0002f) noexcept;
};

} // namespace thunder
