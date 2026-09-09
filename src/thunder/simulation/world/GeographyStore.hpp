#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace thunder {

// ============================================================================
// EU5-style 3-Layer Orthogonal Compound Terrain System
// ============================================================================

// 1. Climate (气候): controls seasonal weather, snowfall, attrition, and monsoons
enum class ClimateType : std::uint8_t {
    Temperate = 0,      // 温带（温和多雨/四季分明）
    Mediterranean = 1,  // 地中海气候（夏干冬雨）
    Subtropical = 2,    // 亚热带（湿润季风）
    Tropical = 3,       // 热带（高温多雨/季风损耗）
    Arid = 4,           // 干旱沙漠气候（极端缺水/高损耗）
    SemiArid = 5,       // 半干旱/草原气候
    Subarctic = 6,      // 亚寒带（针叶林气候/漫长严冬）
    Arctic = 7,         // 极地/极寒苔原（常年冻土/重度损耗）
    Oceanic = 8,        // 海洋性气候
    Count = 9
};

// 2. Topography (地貌): controls combat width, unit movement speed, and defense
enum class TopographyType : std::uint8_t {
    Flatlands = 0,      // 平原/低地（开阔战场/标准移动）
    Hills = 1,          // 丘陵（中等起伏/防御加成）
    Mountains = 2,      // 山脉（狭窄战斗宽度/通行惩罚/高防御加成）
    Plateau = 3,        // 高原（高海拔开阔/轻微损耗）
    Marsh = 4,          // 沼泽/湿地（重度移动阻滞/疾病损耗）
    Count = 5
};

// 3. Vegetation (植被): controls agricultural yield, RGO resources, and building slots
enum class VegetationType : std::uint8_t {
    Farmlands = 0,          // 农田熟地（高农业产出/建筑槽位丰沛）
    Grasslands = 1,         // 草原/草甸（放牧/中等开发）
    Woods = 2,              // 疏林/次生林（木材资源/轻微防御）
    Forest = 3,             // 密林/温带针阔叶林（丰富木材/重度视线遮挡）
    Jungle = 4,             // 雨林/热带丛林（开发难度极高/特殊物产）
    SparselyVegetated = 5,  // 荒漠植被/灌木荒原（贫瘠）
    Barren = 6,             // 不毛之地/冰原冻土（极少产出）
    Count = 7
};

// Compound 3-Layer Orthogonal Terrain Descriptor
struct CompoundTerrain {
    ClimateType climate = ClimateType::Temperate;
    TopographyType topography = TopographyType::Flatlands;
    VegetationType vegetation = VegetationType::Grasslands;

    [[nodiscard]] constexpr bool operator==(const CompoundTerrain&) const noexcept = default;
};

// Legacy single-enum vocabulary retained for compatibility
enum class TerrainType : std::uint8_t {
    Plains = 0,
    Hills = 1,
    Mountains = 2,
    Forest = 3,
    Marsh = 4,
    Desert = 5,
    Jungle = 6,
    Urban = 7,
    Arctic = 8,
    Ocean = 9,
    Count = 10
};

[[nodiscard]] constexpr CompoundTerrain to_compound_terrain(TerrainType legacy) noexcept {
    switch (legacy) {
    case TerrainType::Plains:
        return {ClimateType::Temperate, TopographyType::Flatlands, VegetationType::Grasslands};
    case TerrainType::Hills:
        return {ClimateType::Temperate, TopographyType::Hills, VegetationType::Grasslands};
    case TerrainType::Mountains:
        return {ClimateType::Temperate, TopographyType::Mountains, VegetationType::Woods};
    case TerrainType::Forest:
        return {ClimateType::Temperate, TopographyType::Flatlands, VegetationType::Forest};
    case TerrainType::Marsh:
        return {ClimateType::Oceanic, TopographyType::Marsh, VegetationType::Grasslands};
    case TerrainType::Desert:
        return {ClimateType::Arid, TopographyType::Flatlands, VegetationType::SparselyVegetated};
    case TerrainType::Jungle:
        return {ClimateType::Tropical, TopographyType::Flatlands, VegetationType::Jungle};
    case TerrainType::Urban:
        return {ClimateType::Temperate, TopographyType::Flatlands, VegetationType::Farmlands};
    case TerrainType::Arctic:
        return {ClimateType::Arctic, TopographyType::Flatlands, VegetationType::Barren};
    case TerrainType::Ocean:
        return {ClimateType::Oceanic, TopographyType::Flatlands, VegetationType::Barren};
    default:
        return {ClimateType::Temperate, TopographyType::Flatlands, VegetationType::Grasslands};
    }
}

[[nodiscard]] constexpr TerrainType to_legacy_terrain_type(CompoundTerrain compound) noexcept {
    if (compound.climate == ClimateType::Arctic) return TerrainType::Arctic;
    if (compound.climate == ClimateType::Arid) return TerrainType::Desert;
    if (compound.vegetation == VegetationType::Jungle || compound.climate == ClimateType::Tropical) return TerrainType::Jungle;
    if (compound.topography == TopographyType::Mountains) return TerrainType::Mountains;
    if (compound.topography == TopographyType::Hills) return TerrainType::Hills;
    if (compound.topography == TopographyType::Marsh) return TerrainType::Marsh;
    if (compound.vegetation == VegetationType::Forest) return TerrainType::Forest;
    if (compound.vegetation == VegetationType::Farmlands) return TerrainType::Plains;
    return TerrainType::Plains;
}

[[nodiscard]] constexpr std::uint32_t soil_fertility_ppm(VegetationType veg, ClimateType climate) noexcept {
    std::uint32_t base_ppm = 1'000'000u;
    switch (veg) {
    case VegetationType::Farmlands: base_ppm = 1'000'000u; break;
    case VegetationType::Grasslands: base_ppm = 850'000u; break;
    case VegetationType::Woods: base_ppm = 700'000u; break;
    case VegetationType::Forest: base_ppm = 550'000u; break;
    case VegetationType::Jungle: base_ppm = 400'000u; break;
    case VegetationType::SparselyVegetated: base_ppm = 200'000u; break;
    case VegetationType::Barren: base_ppm = 50'000u; break;
    default: base_ppm = 850'000u; break;
    }
    // Climate physical moisture and thermal constraint
    switch (climate) {
    case ClimateType::Mediterranean: base_ppm = (base_ppm * 95u) / 100u; break;
    case ClimateType::Subtropical: base_ppm = std::min(1'000'000u, (base_ppm * 105u) / 100u); break;
    case ClimateType::SemiArid: base_ppm = (base_ppm * 65u) / 100u; break;
    case ClimateType::Arid: base_ppm = (base_ppm * 40u) / 100u; break;
    case ClimateType::Arctic: base_ppm = (base_ppm * 10u) / 100u; break;
    case ClimateType::Subarctic: base_ppm = (base_ppm * 50u) / 100u; break;
    case ClimateType::Tropical: base_ppm = (base_ppm * 85u) / 100u; break;
    case ClimateType::Oceanic: base_ppm = (base_ppm * 90u) / 100u; break;
    default: break;
    }
    return base_ppm;
}

[[nodiscard]] constexpr std::uint32_t climate_seasonal_attrition_ppm(ClimateType climate, std::uint8_t month) noexcept {
    const bool is_winter = (month >= 11 || month <= 2);
    switch (climate) {
    case ClimateType::Arctic: return is_winter ? 80'000u : 30'000u;
    case ClimateType::Subarctic: return is_winter ? 50'000u : 10'000u;
    case ClimateType::Arid: return 40'000u;
    case ClimateType::Tropical: return 35'000u;
    case ClimateType::Temperate: return is_winter ? 15'000u : 0u;
    default: return 0u;
    }
}

[[nodiscard]] constexpr std::uint32_t frontage_capacity_manpower(TopographyType topo) noexcept {
    switch (topo) {
    case TopographyType::Flatlands: return 50'000u;
    case TopographyType::Plateau: return 45'000u;
    case TopographyType::Hills: return 35'000u;
    case TopographyType::Marsh: return 30'000u;
    case TopographyType::Mountains: return 20'000u;
    default: return 50'000u;
    }
}

[[nodiscard]] constexpr double topography_movement_cost_factor(TopographyType topo) noexcept {
    switch (topo) {
    case TopographyType::Flatlands: return 1.0;
    case TopographyType::Plateau: return 1.15;
    case TopographyType::Hills: return 1.35;
    case TopographyType::Marsh: return 1.75;
    case TopographyType::Mountains: return 2.20;
    default: return 1.0;
    }
}

[[nodiscard]] constexpr double topography_combat_width_factor(TopographyType topo) noexcept {
    return static_cast<double>(frontage_capacity_manpower(topo)) / 50'000.0;
}

[[nodiscard]] constexpr double vegetation_agriculture_modifier(VegetationType veg) noexcept {
    return static_cast<double>(soil_fertility_ppm(veg, ClimateType::Temperate)) / 1'000'000.0;
}

[[nodiscard]] constexpr std::int32_t vegetation_building_slots_delta(VegetationType veg) noexcept {
    switch (veg) {
    case VegetationType::Farmlands: return 2;
    case VegetationType::Grasslands: return 0;
    case VegetationType::Woods: return 0;
    case VegetationType::Forest: return -1;
    case VegetationType::Jungle: return -2;
    case VegetationType::SparselyVegetated: return -2;
    case VegetationType::Barren: return -4;
    default: return 0;
    }
}

[[nodiscard]] constexpr double climate_attrition_rate(ClimateType climate, bool winter) noexcept {
    switch (climate) {
    case ClimateType::Tropical: return 0.05;
    case ClimateType::Arid: return 0.08;
    case ClimateType::Subarctic: return winter ? 0.12 : 0.03;
    case ClimateType::Arctic: return winter ? 0.25 : 0.15;
    default: return winter ? 0.04 : 0.0;
    }
}

// Dynamic seasonal temperature, physical snowpack & thermal energy state per province
struct SeasonalProvinceState {
    float temperature_c = 15.0f;                        // Temperature in Celsius (°C)
    std::int32_t temperature_milli_c = 15000;           // Temperature in milli-Celsius
    float snowpack_depth_mm = 0.0f;                     // Physical snowpack depth on ground (mm)
    float snow_melt_runoff_mm = 0.0f;                   // Daily snowmelt runoff (mm)
    float thermal_energy_balance_wm2 = 0.0f;            // Net surface radiation balance (W/m^2)
    float surface_albedo = 0.20f;                       // Surface albedo (0.15 ~ 0.85)
    std::uint32_t snow_cover_ppm = 0u;                  // Snow / frost coverage (0 to 1,000,000 ppm)
    std::uint32_t vegetative_activity_ppm = 1'000'000u; // Active growing season factor (0 to 1,000,000 ppm)
    std::uint32_t seasonal_attrition_ppm = 0u;          // Seasonal logistics & weather attrition
    std::string_view season_name = "Summer";            // "Spring", "Summer", "Autumn", "Winter"
    bool is_frozen = false;                             // True when temperature <= 0°C
    bool is_snow_covered = false;                       // True when snow_cover_ppm > 150,000
    bool is_melting = false;                            // True when snowpack is actively melting in spring
};

[[nodiscard]] inline SeasonalProvinceState evaluate_seasonal_province_state(
    CompoundTerrain compound, double center_y_m, unsigned month, unsigned day) noexcept {
    const float day_of_year = static_cast<float>((month > 0 ? (month - 1) * 30 : 0) + std::clamp<unsigned>(day, 1, 30));
    float base_temp_c = 12.0f;
    float temp_amplitude_c = 12.0f;
    float base_precipitation_mm_month = 60.0f;

    switch (compound.climate) {
    case ClimateType::Arctic:        base_temp_c = -14.0f; temp_amplitude_c = 18.0f; base_precipitation_mm_month = 25.0f; break;
    case ClimateType::Subarctic:     base_temp_c = -2.0f;  temp_amplitude_c = 20.0f; base_precipitation_mm_month = 45.0f; break;
    case ClimateType::Temperate:     base_temp_c = 11.5f;  temp_amplitude_c = 13.0f; base_precipitation_mm_month = 65.0f; break;
    case ClimateType::Mediterranean: base_temp_c = 16.5f;  temp_amplitude_c = 8.5f;  base_precipitation_mm_month = 50.0f; break;
    case ClimateType::Subtropical:   base_temp_c = 19.5f;  temp_amplitude_c = 9.0f;  base_precipitation_mm_month = 110.0f; break;
    case ClimateType::Tropical:      base_temp_c = 26.5f;  temp_amplitude_c = 2.5f;  base_precipitation_mm_month = 180.0f; break;
    case ClimateType::Arid:          base_temp_c = 22.0f;  temp_amplitude_c = 16.0f; base_precipitation_mm_month = 10.0f; break;
    case ClimateType::SemiArid:      base_temp_c = 13.5f;  temp_amplitude_c = 14.0f; base_precipitation_mm_month = 30.0f; break;
    case ClimateType::Oceanic:       base_temp_c = 10.5f;  temp_amplitude_c = 6.0f;  base_precipitation_mm_month = 85.0f; break;
    default:                         base_temp_c = 12.0f;  temp_amplitude_c = 10.0f; base_precipitation_mm_month = 60.0f; break;
    }

    float altitude_drop_c = 0.0f;
    switch (compound.topography) {
    case TopographyType::Mountains: altitude_drop_c = 8.5f; break;
    case TopographyType::Plateau:   altitude_drop_c = 4.0f; break;
    case TopographyType::Hills:     altitude_drop_c = 2.0f; break;
    case TopographyType::Marsh:     altitude_drop_c = 0.0f; break;
    case TopographyType::Flatlands: altitude_drop_c = 0.0f; break;
    default: break;
    }

    const float hemisphere_sign = (center_y_m < -100.0) ? -1.0f : 1.0f;
    constexpr float pi = 3.14159265358979323846f;
    const float season_phase = std::cos((day_of_year - 195.0f) * (2.0f * pi / 365.0f));
    
    // Thermal energy radiation balance: Solar incoming vs Outgoing Longwave
    const float solar_elevation_factor = std::clamp(0.50f + 0.50f * (season_phase * hemisphere_sign), 0.05f, 1.0f);
    const float incoming_solar_wm2 = 340.0f * solar_elevation_factor;

    float cur_temp_c = (base_temp_c - altitude_drop_c) + (temp_amplitude_c * season_phase * hemisphere_sign);

    // 1. Winter Snow Accumulation (再次积雪):
    float snowpack_mm = 0.0f;
    if (cur_temp_c < 0.0f) {
        const float winter_severity = std::min(1.0f, -cur_temp_c / 10.0f);
        snowpack_mm = base_precipitation_mm_month * (1.5f + winter_severity * 2.5f);
    } else if (compound.topography == TopographyType::Mountains && base_temp_c - altitude_drop_c < 2.0f) {
        snowpack_mm = 150.0f + std::max(0.0f, (5.0f - cur_temp_c) * 40.0f);
    }

    // 2. Spring/Summer Snow Melting (积雪融化):
    float melt_runoff_mm = 0.0f;
    bool is_melting = false;
    if (cur_temp_c > 0.0f && snowpack_mm > 0.0f) {
        is_melting = true;
        const float daily_melt_capacity = cur_temp_c * 4.5f;
        melt_runoff_mm = std::min(snowpack_mm, daily_melt_capacity);
        snowpack_mm = std::max(0.0f, snowpack_mm - melt_runoff_mm);
    }

    // 3. Albedo Feedback on Net Surface Thermal Balance:
    const float snow_ratio = std::clamp(snowpack_mm / 100.0f, 0.0f, 1.0f);
    const float surface_albedo = 0.20f + snow_ratio * 0.55f;
    const float absorbed_solar_wm2 = incoming_solar_wm2 * (1.0f - surface_albedo);
    const float outgoing_longwave_wm2 = 210.0f + 3.2f * cur_temp_c;
    const float net_thermal_flux_wm2 = absorbed_solar_wm2 - outgoing_longwave_wm2;

    if (snow_ratio > 0.1f) {
        cur_temp_c -= snow_ratio * 2.2f;
    }

    SeasonalProvinceState state;
    state.temperature_c = cur_temp_c;
    state.temperature_milli_c = static_cast<std::int32_t>(cur_temp_c * 1000.0f);
    state.snowpack_depth_mm = snowpack_mm;
    state.snow_melt_runoff_mm = melt_runoff_mm;
    state.thermal_energy_balance_wm2 = net_thermal_flux_wm2;
    state.surface_albedo = surface_albedo;
    state.is_frozen = (cur_temp_c <= 0.0f);
    state.is_melting = is_melting;

    const unsigned effective_season_month = (hemisphere_sign >= 0.0f) ? month : ((month + 5) % 12 + 1);
    if (effective_season_month >= 3 && effective_season_month <= 5) {
        state.season_name = "Spring";
    } else if (effective_season_month >= 6 && effective_season_month <= 8) {
        state.season_name = "Summer";
    } else if (effective_season_month >= 9 && effective_season_month <= 11) {
        state.season_name = "Autumn";
    } else {
        state.season_name = "Winter";
    }

    if (snowpack_mm > 50.0f || cur_temp_c <= -5.0f) {
        state.snow_cover_ppm = 1'000'000u;
    } else if (snowpack_mm > 0.0f || cur_temp_c < 3.0f) {
        const float ppm_from_depth = (snowpack_mm / 50.0f) * 1'000'000.0f;
        const float ppm_from_temp = ((3.0f - cur_temp_c) / 8.0f) * 1'000'000.0f;
        state.snow_cover_ppm = static_cast<std::uint32_t>(std::clamp(std::max(ppm_from_depth, ppm_from_temp), 0.0f, 1'000'000.0f));
    } else {
        state.snow_cover_ppm = 0u;
    }
    state.is_snow_covered = (state.snow_cover_ppm > 150'000u);

    // Vegetative activity: Spring meltwater runoff enriches soil moisture for young crops
    if (cur_temp_c <= -2.0f) {
        state.vegetative_activity_ppm = 100'000u; // Winter dormancy
    } else if (cur_temp_c < 12.0f) {
        float act = 0.10f + 0.90f * ((cur_temp_c + 2.0f) / 14.0f);
        if (melt_runoff_mm > 0.0f) act = std::min(1.0f, act + 0.15f); // Spring snowmelt moisture bonus
        state.vegetative_activity_ppm = static_cast<std::uint32_t>(std::clamp(act * 1'000'000.0f, 100'000.0f, 1'000'000.0f));
    } else if (cur_temp_c <= 32.0f) {
        state.vegetative_activity_ppm = 1'000'000u; // Optimal thermal window
    } else {
        const float heat_factor = std::max(0.40f, 1.0f - (cur_temp_c - 32.0f) * 0.05f);
        state.vegetative_activity_ppm = static_cast<std::uint32_t>(heat_factor * 1'000'000.0f);
    }

    if (cur_temp_c < -10.0f) {
        state.seasonal_attrition_ppm = 80'000u;
    } else if (cur_temp_c < 0.0f) {
        state.seasonal_attrition_ppm = 35'000u;
    } else if (cur_temp_c > 38.0f) {
        state.seasonal_attrition_ppm = 40'000u;
    } else {
        state.seasonal_attrition_ppm = 0u;
    }

    return state;
}

enum class ProvinceKind : std::uint8_t {
    Land = 0,
    Sea = 1,
    Lake = 2,
};

struct StateInit {
    std::string key;
    CountryId owner{};
    MarketId market{};
    ProvinceId capital{};
    std::uint32_t resistance_ppm = 0;
    std::uint32_t infrastructure = 100u;
    std::uint32_t market_access_ppm = 1'000'000u;
};

struct ProvinceInit {
    std::string key;
    StateId state{};
    CountryId owner{};
    MarketId market{};
    double center_x_m = 0.0;
    double center_y_m = 0.0;
    std::uint32_t area_km2 = 0;
    ProvinceKind kind = ProvinceKind::Land;
    bool coastal = false;
    bool impassable = false;
    CompoundTerrain compound_terrain{};
};

class GeographyStore {
public:
    StateId create_state(StateInit init);
    ProvinceId create_province(ProvinceInit init);
    void reserve_states(std::size_t count);
    void reserve_provinces(std::size_t count);

    [[nodiscard]] std::size_t state_count() const noexcept { return state_keys_.size(); }
    [[nodiscard]] std::size_t province_count() const noexcept { return province_keys_.size(); }
    [[nodiscard]] std::string_view state_key(StateId id) const;
    [[nodiscard]] std::string_view province_key(ProvinceId id) const;
    [[nodiscard]] CountryId state_owner(StateId id) const;
    [[nodiscard]] MarketId state_market(StateId id) const;
    [[nodiscard]] ProvinceId state_capital(StateId id) const;
    [[nodiscard]] std::uint32_t state_resistance_ppm(StateId id) const;
    [[nodiscard]] std::uint32_t state_infrastructure(StateId id) const;
    [[nodiscard]] std::uint32_t state_infrastructure_load(StateId id) const;
    [[nodiscard]] std::uint32_t state_market_access_ppm(StateId id) const;
    [[nodiscard]] StateId province_state(ProvinceId id) const;
    [[nodiscard]] CountryId province_owner(ProvinceId id) const;
    [[nodiscard]] MarketId province_market(ProvinceId id) const;
    [[nodiscard]] double province_center_x(ProvinceId id) const;
    [[nodiscard]] double province_center_y(ProvinceId id) const;
    [[nodiscard]] ProvinceKind province_kind(ProvinceId id) const;
    [[nodiscard]] bool province_is_coastal(ProvinceId id) const;
    [[nodiscard]] bool province_is_impassable(ProvinceId id) const;
    [[nodiscard]] CompoundTerrain province_compound_terrain(ProvinceId id) const;
    [[nodiscard]] ClimateType province_climate(ProvinceId id) const;
    [[nodiscard]] TopographyType province_topography(ProvinceId id) const;
    [[nodiscard]] VegetationType province_vegetation(ProvinceId id) const;

    void set_state_owner(StateId id, CountryId owner);
    void set_state_market(StateId id, MarketId market);
    void set_state_capital(StateId id, ProvinceId capital);
    void set_state_resistance_ppm(StateId id, std::uint32_t ppm);
    void add_state_resistance_ppm(StateId id, std::int32_t delta);
    void set_state_infrastructure(StateId id, std::uint32_t value);
    void set_state_infrastructure_load(StateId id, std::uint32_t value);
    void set_state_market_access_ppm(StateId id, std::uint32_t ppm);
    void set_province_owner(ProvinceId id, CountryId owner);
    void set_province_market(ProvinceId id, MarketId market);
    void set_province_state(ProvinceId id, StateId state);
    void set_province_kind(ProvinceId id, ProvinceKind kind);
    void set_province_coastal(ProvinceId id, bool coastal);
    void set_province_impassable(ProvinceId id, bool impassable);
    void set_province_compound_terrain(ProvinceId id, CompoundTerrain terrain);
    void set_province_climate(ProvinceId id, ClimateType climate);
    void set_province_topography(ProvinceId id, TopographyType topography);
    void set_province_vegetation(ProvinceId id, VegetationType vegetation);

    [[nodiscard]] std::span<const CountryId> state_owners() const noexcept { return state_owners_; }
    [[nodiscard]] std::span<const MarketId> state_markets() const noexcept { return state_markets_; }
    [[nodiscard]] std::span<const ProvinceId> state_capitals() const noexcept { return state_capitals_; }
    [[nodiscard]] std::span<const std::uint32_t> state_resistance() const noexcept { return state_resistance_ppm_; }
    [[nodiscard]] std::span<const std::uint32_t> state_infrastructure() const noexcept { return state_infrastructure_; }
    [[nodiscard]] std::span<const std::uint32_t> state_infrastructure_load() const noexcept { return state_infrastructure_load_; }
    [[nodiscard]] std::span<const std::uint32_t> state_market_access() const noexcept { return state_market_access_ppm_; }
    [[nodiscard]] std::span<const StateId> province_states() const noexcept { return province_states_; }
    [[nodiscard]] std::span<const CountryId> province_owners() const noexcept { return province_owners_; }
    [[nodiscard]] std::span<const MarketId> province_markets() const noexcept { return province_markets_; }
    [[nodiscard]] std::span<const double> province_center_xs() const noexcept { return province_center_x_m_; }
    [[nodiscard]] std::span<const double> province_center_ys() const noexcept { return province_center_y_m_; }
    [[nodiscard]] std::span<const std::uint32_t> province_areas_km2() const noexcept { return province_area_km2_; }
    [[nodiscard]] std::span<const ProvinceKind> province_kinds() const noexcept { return province_kinds_; }
    [[nodiscard]] std::span<const std::uint8_t> province_coastal() const noexcept { return province_coastal_; }
    [[nodiscard]] std::span<const std::uint8_t> province_impassable() const noexcept { return province_impassable_; }
    [[nodiscard]] std::span<const ClimateType> province_climates() const noexcept { return province_climates_; }
    [[nodiscard]] std::span<const TopographyType> province_topographies() const noexcept { return province_topographies_; }
    [[nodiscard]] std::span<const VegetationType> province_vegetations() const noexcept { return province_vegetations_; }

    [[nodiscard]] bool validate(std::size_t country_count, std::size_t market_count) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] std::size_t state_index(StateId id) const;
    [[nodiscard]] std::size_t province_index(ProvinceId id) const;

    std::vector<std::string> state_keys_;
    std::vector<CountryId> state_owners_;
    std::vector<MarketId> state_markets_;
    std::vector<ProvinceId> state_capitals_;
    std::vector<std::uint32_t> state_resistance_ppm_;
    std::vector<std::uint32_t> state_infrastructure_;
    std::vector<std::uint32_t> state_infrastructure_load_;
    std::vector<std::uint32_t> state_market_access_ppm_;

    std::vector<std::string> province_keys_;
    std::vector<StateId> province_states_;
    std::vector<CountryId> province_owners_;
    std::vector<MarketId> province_markets_;
    std::vector<double> province_center_x_m_;
    std::vector<double> province_center_y_m_;
    std::vector<std::uint32_t> province_area_km2_;
    std::vector<ProvinceKind> province_kinds_;
    std::vector<std::uint8_t> province_coastal_;
    std::vector<std::uint8_t> province_impassable_;
    std::vector<ClimateType> province_climates_;
    std::vector<TopographyType> province_topographies_;
    std::vector<VegetationType> province_vegetations_;
};

class GeographyScopeIndex {
public:
    void rebuild(std::size_t country_count, const GeographyStore& geography);
    [[nodiscard]] std::span<const StateId> states(CountryId country) const;
    [[nodiscard]] std::span<const ProvinceId> provinces(StateId state) const;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
private:
    std::vector<std::uint32_t> country_state_offsets_;
    std::vector<StateId> country_states_;
    std::vector<std::uint32_t> state_province_offsets_;
    std::vector<ProvinceId> state_provinces_;
};

} // namespace thunder
