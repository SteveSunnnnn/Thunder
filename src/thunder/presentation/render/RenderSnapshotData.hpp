#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/world/GeographyStore.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {

// GPU-facing dynamic country payload.
struct CountryRenderRecord {
    CountryId id{};
    float population = 0.0f;
    float gdp = 0.0f;
    float treasury = 0.0f;
    float tax_rate = 0.0f;
};
static_assert(sizeof(CountryRenderRecord) <= 24, "Keep hot render records compact");

// Dynamic ownership and control record per province for political map shader.
struct ProvincePoliticalRenderRecord {
    ProvinceId id{};
    CountryId owner{};
    CountryId controller{};
    std::uint16_t visual_flags = 0; // e.g. occupied, in revolt, thunder
    std::uint16_t dev_level = 0;
};

// Dynamic map-mode scalar value per province for GPU heatmaps/lenses.
struct MapModeRenderRecord {
    ProvinceId id{};
    float scalar_value = 0.0f;
    std::uint32_t color_rgba = 0xffffffffu;
};

// Dynamic living world instance (armies, fleets, trade carts, locomotives)
struct LivingInstanceRenderRecord {
    std::uint32_t entity_id = 0;
    float world_x = 0.0f;
    float world_y = 0.0f;
    float world_z = 0.0f;
    float heading_rad = 0.0f;
    std::uint16_t visual_type = 0; // 0=infantry, 1=cavalry, 2=artillery, 3=ship, 4=train, 5=wagon
    std::uint16_t faction_flag = 0;
};

// Dynamic infrastructure spline connecting active production Hubs
struct InfrastructureSplineRenderRecord {
    ProvinceId source_province{};
    ProvinceId target_province{};
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    std::uint8_t transport_type = 0; // 0=dirt road, 1=paved road, 2=railway, 3=canal
    std::uint8_t activity_level = 1; // traffic volume for animated caravans/trains
};

// Dynamic seasonal climate state per province for weather, temperature, and snow overlay.
struct ProvinceClimateRenderRecord {
    ProvinceId id{};
    float temperature_c = 15.0f;
    float snow_cover = 0.0f;          // 0.0 to 1.0
    float vegetative_activity = 1.0f; // 0.0 to 1.0
};

struct RenderSnapshot {
    std::uint64_t generation = 0;
    std::uint64_t world_checksum = 0;
    float seasonal_snow_factor = 0.0f;
    float seasonal_time_of_year = 0.0f;
    std::vector<CountryRenderRecord> countries;
    std::vector<ProvincePoliticalRenderRecord> province_politics;
    std::vector<ProvinceClimateRenderRecord> province_climates;
    std::vector<MapModeRenderRecord> map_mode_scalars;
    std::vector<LivingInstanceRenderRecord> living_instances;
    std::vector<InfrastructureSplineRenderRecord> infrastructure_splines;

    void reserve(std::size_t countries_capacity, std::size_t provinces_capacity = 0) {
        countries.reserve(countries_capacity);
        if (provinces_capacity > 0) {
            province_politics.reserve(provinces_capacity);
            province_climates.reserve(provinces_capacity);
            map_mode_scalars.reserve(provinces_capacity);
        }
    }
};

} // namespace thunder
