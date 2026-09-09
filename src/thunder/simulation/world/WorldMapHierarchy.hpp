#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/world/GeographyStore.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thunder {

// Static three-level spatial hierarchy used by the world map.  The existing
// ProvinceId remains the stable raster/simulation leaf for compatibility;
// LocationId points at that leaf and is the authored smallest territory.
struct MapAreaRecord {
    std::string key;
};

struct MapProvinceRecord {
    std::string key;
    AreaId area{};
};

struct MapLocationRecord {
    std::string key;
    TradeProvinceId province{};
    AreaId area{};
    ProvinceId raster_province{};
    double center_x_m = 0.0;
    double center_y_m = 0.0;
    std::uint32_t area_km2 = 0;
    CompoundTerrain compound_terrain{};
    TerrainType terrain = TerrainType::Plains;
    bool coastal = false;
    std::uint16_t constraint_flags = 0u;
};

enum MapLocationConstraint : std::uint16_t {
    ConstraintSovereign1836 = 1u << 0u,
    ConstraintModernBorder = 1u << 1u,
    ConstraintHistoricTransfer = 1u << 2u,
    ConstraintDisputed = 1u << 3u,
    ConstraintAdministrative = 1u << 4u,
    ConstraintFreeform = 1u << 5u,
};

// Owns only immutable spatial identity and parent/child CSR views.  Political
// ownership, control, claims and simulation stores remain outside this class.
class WorldMapHierarchy {
public:
    void clear() noexcept;

    // Build a deterministic compatibility hierarchy from the existing
    // geography store when a legacy pack has no explicit hierarchy chunks.
    void build_default(const GeographyStore& geography);

    // Replace the hierarchy with compiler-authored records and rebuild CSR
    // child ranges. IDs are the vector indices and are therefore stable inside
    // one world-pack build.
    void set(std::vector<MapAreaRecord> areas,
             std::vector<MapProvinceRecord> provinces,
             std::vector<MapLocationRecord> locations);

    [[nodiscard]] std::size_t area_count() const noexcept { return areas_.size(); }
    [[nodiscard]] std::size_t province_count() const noexcept { return provinces_.size(); }
    [[nodiscard]] std::size_t location_count() const noexcept { return locations_.size(); }

    [[nodiscard]] const MapAreaRecord& area(AreaId id) const;
    [[nodiscard]] const MapProvinceRecord& province(TradeProvinceId id) const;
    [[nodiscard]] const MapLocationRecord& location(LocationId id) const;
    [[nodiscard]] AreaId area_for_province(TradeProvinceId id) const;
    [[nodiscard]] TradeProvinceId province_for_location(LocationId id) const;
    [[nodiscard]] LocationId location_for_raster(ProvinceId id) const noexcept;

    [[nodiscard]] std::span<const MapAreaRecord> areas() const noexcept { return areas_; }
    [[nodiscard]] std::span<const MapProvinceRecord> provinces() const noexcept { return provinces_; }
    [[nodiscard]] std::span<const MapLocationRecord> locations() const noexcept { return locations_; }
    [[nodiscard]] std::span<const TradeProvinceId> provinces_in_area(AreaId id) const;
    [[nodiscard]] std::span<const LocationId> locations_in_province(TradeProvinceId id) const;

    [[nodiscard]] bool validate(std::size_t raster_province_count) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] std::size_t area_index(AreaId id) const;
    [[nodiscard]] std::size_t province_index(TradeProvinceId id) const;
    [[nodiscard]] std::size_t location_index(LocationId id) const;

    std::vector<MapAreaRecord> areas_;
    std::vector<MapProvinceRecord> provinces_;
    std::vector<MapLocationRecord> locations_;
    std::vector<std::int32_t> raster_location_lookup_;
    std::vector<std::uint32_t> area_province_offsets_;
    std::vector<TradeProvinceId> area_provinces_;
    std::vector<std::uint32_t> province_location_offsets_;
    std::vector<LocationId> province_locations_;
};

} // namespace thunder
