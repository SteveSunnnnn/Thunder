#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include <cstdint>
#include <type_traits>

namespace thunder {

enum class BiomeClass : std::uint8_t {
    Temperate = 0,
    Boreal,
    Mediterranean,
    Steppe,
    Desert,
    Tropical,
    Alpine,
    Tundra,
};

enum class BuildingVisualKind : std::uint8_t {
    None = 0,
    Urban,
    Factory,
    Farm,
    Mine,
    Port,
};

enum class LivingInstanceKind : std::uint8_t {
    Residential = 0,
    Commercial,
    Factory,
    Farm,
    Mine,
    Port,
};

struct ProvinceVisualDefinition {
    double center_x_m = 0.0;
    double center_y_m = 0.0;
    std::uint32_t visual_radius_m = 8'000u;
    BiomeClass biome = BiomeClass::Temperate;
    bool coastal = false;
    // Compact GPU family selector derived from the pack-authored
    // ArchitectureRegion key. Zero means use the neutral/default family.
    std::uint16_t architecture_family = 0u;
};

struct LivingChunkKey {
    std::int32_t x = 0;
    std::int32_t y = 0;
    friend constexpr bool operator==(LivingChunkKey, LivingChunkKey) = default;
    friend constexpr auto operator<=>(LivingChunkKey, LivingChunkKey) = default;
};

// 16-byte instance payload intended for direct upload to a GPU structured buffer.
// X/Y are 1-metre offsets inside a 64 km living-map chunk. Z is 0.5 m signed.
struct LivingInstanceGpu {
    std::uint16_t local_x_m = 0;
    std::uint16_t local_y_m = 0;
    std::int16_t local_z_half_m = 0;
    std::uint16_t yaw_u16 = 0;
    std::uint16_t scale_milli = 1000;
    std::uint16_t asset_variant = 0;
    std::uint16_t province_r16 = 0; // 0=no province, otherwise ProvinceId + 1.
    std::uint8_t kind = 0;
    std::uint8_t lod_mask = 0x03u;  // bit0=near, bit1=medium, bit2=far.
};
static_assert(sizeof(LivingInstanceGpu) == 16u);
static_assert(std::is_trivially_copyable_v<LivingInstanceGpu>);

// One compact record per province for medium/far zoom. Renderer can draw a city/
// industry/rural cluster without reading the full near-instance list.
struct LivingClusterGpu {
    std::uint16_t local_x_m = 0;
    std::uint16_t local_y_m = 0;
    std::uint16_t province_r16 = 0;
    std::uint16_t population_q = 0;
    std::uint16_t urban_q = 0;
    std::uint16_t industry_q = 0;
    std::uint16_t rural_q = 0;
    std::uint16_t flags = 0;
};
static_assert(sizeof(LivingClusterGpu) == 16u);
static_assert(std::is_trivially_copyable_v<LivingClusterGpu>);

struct ProvinceVisualAggregate {
    std::uint64_t population = 0;
    std::uint64_t employed = 0;
    std::uint64_t sol_weighted = 0;
    std::uint32_t urban_levels = 0;
    std::uint32_t factory_levels = 0;
    std::uint32_t farm_levels = 0;
    std::uint32_t mine_levels = 0;
    std::uint32_t port_levels = 0;
};

} // namespace thunder
