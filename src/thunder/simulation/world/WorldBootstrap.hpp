#pragma once
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include "thunder/simulation/world/GeographyStore.hpp"
#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"
#include "thunder/simulation/world/SpatialPlacement.hpp"
#include "thunder/simulation/world/StateRegionIndex.hpp"
#include "thunder/simulation/world/WorldStaticLayers.hpp"
#include "thunder/simulation/world/WorldMapHierarchy.hpp"
#include "thunder/simulation/world/WorldMapLabels.hpp"
#include "thunder/simulation/world/WorldBootstrapWire.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/content/worldpack/WorldPackMetadata.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {
struct WorldBootstrapResult {
    World world;
    GeographyScopeIndex scope_index;
    StateRegionIndex state_regions;
    ProvinceAdjacencyGraph adjacency;
    SpatialPlacementDatabase spatial_placement;
    WorldStaticLayers static_layers;
    WorldMapHierarchy map_hierarchy;
    WorldMapLabels map_labels;
    WorldPackMetadata metadata;
    std::vector<ProvinceId> sea_starts;
    std::uint64_t world_pack_hash=0;
};
class WorldBootstrap {
public:
    [[nodiscard]] static WorldBootstrapResult load(const WorldPackReader& pack, const EconomyDefinitions& definitions);
};
} // namespace thunder
