#include "thunder/simulation/world/WorldMapHierarchy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace thunder {

namespace {
template <typename Id>
std::size_t checked_index(Id id, std::size_t size, const char* what) {
    if (!id.valid() || static_cast<std::size_t>(id.value()) >= size)
        throw std::out_of_range(what);
    return static_cast<std::size_t>(id.value());
}
}

void WorldMapHierarchy::clear() noexcept {
    areas_.clear();
    provinces_.clear();
    locations_.clear();
    raster_location_lookup_.clear();
    area_province_offsets_.clear();
    area_provinces_.clear();
    province_location_offsets_.clear();
    province_locations_.clear();
}

void WorldMapHierarchy::set(std::vector<MapAreaRecord> areas,
                            std::vector<MapProvinceRecord> provinces,
                            std::vector<MapLocationRecord> locations) {
    areas_ = std::move(areas);
    provinces_ = std::move(provinces);
    locations_ = std::move(locations);

    area_province_offsets_.assign(areas_.size() + 1u, 0u);
    province_location_offsets_.assign(provinces_.size() + 1u, 0u);
    for (const auto& province : provinces_) {
        if (!province.area.valid() || province.area.value() >= areas_.size())
            throw std::invalid_argument("map province references invalid area");
        ++area_province_offsets_[province.area.value() + 1u];
    }
    for (const auto& location : locations_) {
        if (!location.province.valid() || location.province.value() >= provinces_.size() ||
            !location.area.valid() || location.area.value() >= areas_.size())
            throw std::invalid_argument("map location references invalid parent");
        if (provinces_[location.province.value()].area != location.area)
            throw std::invalid_argument("map location area disagrees with parent province");
        ++province_location_offsets_[location.province.value() + 1u];
    }
    for (std::size_t i = 1u; i < area_province_offsets_.size(); ++i)
        area_province_offsets_[i] += area_province_offsets_[i - 1u];
    for (std::size_t i = 1u; i < province_location_offsets_.size(); ++i)
        province_location_offsets_[i] += province_location_offsets_[i - 1u];

    area_provinces_.assign(provinces_.size(), TradeProvinceId{});
    auto area_write = area_province_offsets_;
    for (std::size_t i = 0u; i < provinces_.size(); ++i) {
        const auto area = provinces_[i].area;
        area_provinces_[area_write[area.value()]++] =
            TradeProvinceId{static_cast<TradeProvinceId::rep_type>(i)};
    }
    province_locations_.assign(locations_.size(), LocationId{});
    auto province_write = province_location_offsets_;
    std::uint32_t highest_raster = 0u;
    bool have_raster = false;
    for (std::size_t i = 0u; i < locations_.size(); ++i) {
        const auto& location = locations_[i];
        province_locations_[province_write[location.province.value()]++] =
            LocationId{static_cast<LocationId::rep_type>(i)};
        if (location.raster_province.valid()) {
            highest_raster = std::max(highest_raster, location.raster_province.value());
            have_raster = true;
        }
    }
    raster_location_lookup_.assign(have_raster ? static_cast<std::size_t>(highest_raster) + 1u : 0u, -1);
    for (std::size_t i = 0u; i < locations_.size(); ++i) {
        const auto raster = locations_[i].raster_province;
        if (!raster.valid()) continue;
        if (raster_location_lookup_[raster.value()] >= 0)
            throw std::invalid_argument("duplicate raster province in map locations");
        raster_location_lookup_[raster.value()] = static_cast<std::int32_t>(i);
    }
}

void WorldMapHierarchy::build_default(const GeographyStore& geography) {
    std::vector<MapAreaRecord> areas;
    std::vector<MapProvinceRecord> provinces;
    std::vector<MapLocationRecord> locations;
    std::unordered_map<std::string, AreaId> area_lookup;
    std::unordered_map<std::string, TradeProvinceId> province_lookup;
    areas.reserve(geography.state_count() + 1u);
    provinces.reserve(geography.state_count() + 1u);
    locations.reserve(geography.province_count());

    const auto ensure_area = [&](std::string key) {
        if (const auto found = area_lookup.find(key); found != area_lookup.end()) return found->second;
        const auto id = AreaId{static_cast<AreaId::rep_type>(areas.size())};
        area_lookup.emplace(key, id);
        areas.push_back({std::move(key)});
        return id;
    };
    const auto ensure_province = [&](std::string key, AreaId area) {
        if (const auto found = province_lookup.find(key); found != province_lookup.end()) return found->second;
        const auto id = TradeProvinceId{static_cast<TradeProvinceId::rep_type>(provinces.size())};
        province_lookup.emplace(key, id);
        provinces.push_back({std::move(key), area});
        return id;
    };

    const auto neutral_area = ensure_area("area.neutral");
    std::vector<AreaId> state_areas(geography.state_count(), neutral_area);
    std::vector<TradeProvinceId> state_provinces(geography.state_count());
    for (std::size_t i = 0u; i < geography.state_count(); ++i) {
        const auto state = StateId{static_cast<StateId::rep_type>(i)};
        const auto owner = geography.state_owner(state);
        std::string area_key = "area.neutral";
        if (owner.valid()) area_key = "area.country." + std::to_string(owner.value());
        state_areas[i] = ensure_area(area_key);
        state_provinces[i] = ensure_province(std::string{geography.state_key(state)}, state_areas[i]);
    }
    for (std::size_t i = 0u; i < geography.province_count(); ++i) {
        const auto raster = ProvinceId{static_cast<ProvinceId::rep_type>(i)};
        const auto state = geography.province_state(raster);
        const auto parent = state.valid() && state.value() < state_provinces.size()
            ? state_provinces[state.value()]
            : ensure_province("province.water", neutral_area);
        const auto area = provinces[parent.value()].area;
        const auto kind = geography.province_kind(raster);
        const auto compound = geography.province_compound_terrain(raster);
        const auto legacy_terrain = kind == ProvinceKind::Sea ? TerrainType::Ocean :
                                    kind == ProvinceKind::Lake ? TerrainType::Marsh :
                                    to_legacy_terrain_type(compound);
        locations.push_back({std::string{geography.province_key(raster)}, parent, area, raster,
                             geography.province_center_x(raster), geography.province_center_y(raster),
                             geography.province_areas_km2()[i],
                             compound,
                             legacy_terrain,
                             geography.province_is_coastal(raster),
                             kind == ProvinceKind::Land
                                 ? static_cast<std::uint16_t>(ConstraintFreeform)
                                 : static_cast<std::uint16_t>(0u)});
    }
    set(std::move(areas), std::move(provinces), std::move(locations));
}

const MapAreaRecord& WorldMapHierarchy::area(AreaId id) const { return areas_[area_index(id)]; }
const MapProvinceRecord& WorldMapHierarchy::province(TradeProvinceId id) const { return provinces_[province_index(id)]; }
const MapLocationRecord& WorldMapHierarchy::location(LocationId id) const { return locations_[location_index(id)]; }
AreaId WorldMapHierarchy::area_for_province(TradeProvinceId id) const { return province(id).area; }
TradeProvinceId WorldMapHierarchy::province_for_location(LocationId id) const { return location(id).province; }

LocationId WorldMapHierarchy::location_for_raster(ProvinceId id) const noexcept {
    if (!id.valid() || id.value() >= raster_location_lookup_.size()) return LocationId{};
    const auto index = raster_location_lookup_[id.value()];
    return index < 0 ? LocationId{} : LocationId{static_cast<LocationId::rep_type>(index)};
}

std::span<const TradeProvinceId> WorldMapHierarchy::provinces_in_area(AreaId id) const {
    const auto i = area_index(id);
    return {area_provinces_.data() + area_province_offsets_[i],
            area_province_offsets_[i + 1u] - area_province_offsets_[i]};
}

std::span<const LocationId> WorldMapHierarchy::locations_in_province(TradeProvinceId id) const {
    const auto i = province_index(id);
    return {province_locations_.data() + province_location_offsets_[i],
            province_location_offsets_[i + 1u] - province_location_offsets_[i]};
}

bool WorldMapHierarchy::validate(std::size_t raster_province_count) const noexcept {
    if (areas_.empty() || provinces_.empty() || locations_.empty() ||
        area_province_offsets_.size() != areas_.size() + 1u ||
        province_location_offsets_.size() != provinces_.size() + 1u ||
        area_provinces_.size() != provinces_.size() ||
        province_locations_.size() != locations_.size()) return false;
    std::vector<std::uint8_t> raster_seen(raster_province_count, 0u);
    for (std::size_t i = 0u; i < areas_.size(); ++i) {
        if (areas_[i].key.empty()) return false;
        for (std::size_t j = 0u; j < i; ++j) if (areas_[i].key == areas_[j].key) return false;
    }
    for (std::size_t i = 0u; i < provinces_.size(); ++i) {
        if (provinces_[i].key.empty() || !provinces_[i].area.valid() || provinces_[i].area.value() >= areas_.size()) return false;
        for (std::size_t j = 0u; j < i; ++j) if (provinces_[i].key == provinces_[j].key) return false;
    }
    for (std::size_t i = 0u; i < areas_.size(); ++i)
        if (area_province_offsets_[i] > area_province_offsets_[i + 1u]) return false;
    for (std::size_t i = 0u; i < locations_.size(); ++i) {
        const auto& location = locations_[i];
        if (location.key.empty() || !location.province.valid() || location.province.value() >= provinces_.size() ||
            location.area != provinces_[location.province.value()].area || !std::isfinite(location.center_x_m) ||
            !std::isfinite(location.center_y_m) || location.terrain >= TerrainType::Count ||
            location.compound_terrain.climate >= ClimateType::Count ||
            location.compound_terrain.topography >= TopographyType::Count ||
            location.compound_terrain.vegetation >= VegetationType::Count ||
            (location.constraint_flags & ~static_cast<std::uint16_t>(
                ConstraintSovereign1836 | ConstraintModernBorder | ConstraintHistoricTransfer |
                ConstraintDisputed | ConstraintAdministrative | ConstraintFreeform)) != 0u) return false;
        if (location.raster_province.valid()) {
            if (location.raster_province.value() >= raster_province_count || raster_seen[location.raster_province.value()] != 0u) return false;
            raster_seen[location.raster_province.value()] = 1u;
        }
        if (province_location_offsets_[location.province.value()] > province_location_offsets_[location.province.value() + 1u]) return false;
    }
    return true;
}

std::uint64_t WorldMapHierarchy::checksum() const noexcept {
    Fnv1a64 hash;
    hash.add(areas_.size());
    for (const auto& record : areas_) hash.add(std::string_view{record.key});
    hash.add(provinces_.size());
    for (const auto& record : provinces_) { hash.add(std::string_view{record.key}); hash.add(record.area.value()); }
    hash.add(locations_.size());
    for (const auto& record : locations_) {
        hash.add(std::string_view{record.key}); hash.add(record.province.value()); hash.add(record.area.value());
        hash.add(record.raster_province.value()); hash.add(record.center_x_m); hash.add(record.center_y_m);
        hash.add(record.area_km2); hash.add(static_cast<std::uint8_t>(record.terrain)); hash.add(record.coastal);
        hash.add(static_cast<std::uint8_t>(record.compound_terrain.climate));
        hash.add(static_cast<std::uint8_t>(record.compound_terrain.topography));
        hash.add(static_cast<std::uint8_t>(record.compound_terrain.vegetation));
        hash.add(record.constraint_flags);
    }
    return hash.value();
}

std::size_t WorldMapHierarchy::memory_bytes() const noexcept {
    std::size_t bytes = areas_.capacity() * sizeof(MapAreaRecord) + provinces_.capacity() * sizeof(MapProvinceRecord) +
                        locations_.capacity() * sizeof(MapLocationRecord) + raster_location_lookup_.capacity() * sizeof(std::int32_t) +
                        area_province_offsets_.capacity() * sizeof(std::uint32_t) + area_provinces_.capacity() * sizeof(TradeProvinceId) +
                        province_location_offsets_.capacity() * sizeof(std::uint32_t) + province_locations_.capacity() * sizeof(LocationId);
    for (const auto& record : areas_) bytes += record.key.capacity();
    for (const auto& record : provinces_) bytes += record.key.capacity();
    for (const auto& record : locations_) bytes += record.key.capacity();
    return bytes;
}

std::size_t WorldMapHierarchy::area_index(AreaId id) const { return checked_index(id, areas_.size(), "invalid AreaId"); }
std::size_t WorldMapHierarchy::province_index(TradeProvinceId id) const { return checked_index(id, provinces_.size(), "invalid TradeProvinceId"); }
std::size_t WorldMapHierarchy::location_index(LocationId id) const { return checked_index(id, locations_.size(), "invalid LocationId"); }

} // namespace thunder
