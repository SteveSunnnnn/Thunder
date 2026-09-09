#pragma once

#include "thunder/simulation/world/GeographyStore.hpp"
#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"
#include "thunder/simulation/world/SpatialPlacement.hpp"
#include "thunder/simulation/world/StateRegionIndex.hpp"
#include "thunder/simulation/world/WorldStaticLayers.hpp"
#include "thunder/simulation/world/WorldMapHierarchy.hpp"
#include "thunder/simulation/world/WorldMapLabels.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/content/worldpack/WorldPackMetadata.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace thunder {

// A pack country is topology identity plus legacy compatibility columns. New
// game content overwrites the legacy values from scripts; retaining them here
// keeps standalone pack inspection and old CNT1 packs readable without making
// the topology loader depend on the mutable World aggregate.
struct WorldPackCountryRecord {
    std::string tag;
    std::array<std::uint8_t, 4> map_color{174u, 179u, 160u, 255u};
    bool authored_map_color = false;
    double population = 0.0;
    double gdp = 0.0;
    double treasury = 0.0;
    double tax_rate = 0.20;
};

// Immutable world data decoded from one .thunderworld pack. It contains no
// economy definitions, simulation stores or script content. WorldBootstrap
// is the narrow composition step that turns this value into a running World.
struct WorldTopology {
    std::vector<WorldPackCountryRecord> countries;
    std::vector<CountryId> market_owners;
    GeographyStore geography;
    GeographyScopeIndex scope_index;
    StateRegionIndex state_regions;
    ProvinceAdjacencyGraph adjacency;
    SpatialPlacementDatabase spatial_placement;
    WorldStaticLayers static_layers;
    WorldMapHierarchy map_hierarchy;
    WorldMapLabels map_labels;
    WorldPackMetadata metadata;
    std::vector<ProvinceId> sea_starts;
    std::uint64_t world_pack_hash = 0;

    [[nodiscard]] static WorldTopology load(const WorldPackReader& pack);
};

} // namespace thunder
