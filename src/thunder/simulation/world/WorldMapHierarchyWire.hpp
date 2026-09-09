#pragma once

#include "thunder/simulation/world/WorldMapHierarchy.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace thunder {

// Serialization-only helpers for compiler-authored Area/TradeProvince/
// Location topology. Geometry remains in the page/vector families; these
// chunks carry only stable identity, parentage and leaf metadata.
class WorldMapHierarchyWire {
public:
    [[nodiscard]] static std::vector<std::byte> areas(std::span<const MapAreaRecord> records);
    [[nodiscard]] static std::vector<std::byte> provinces(std::span<const MapProvinceRecord> records);
    [[nodiscard]] static std::vector<std::byte> locations(std::span<const MapLocationRecord> records);
};

} // namespace thunder
