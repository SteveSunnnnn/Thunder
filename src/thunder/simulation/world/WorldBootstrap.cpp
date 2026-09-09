#include "thunder/simulation/world/WorldBootstrap.hpp"

#include "thunder/simulation/world/WorldTopology.hpp"

#include <utility>

namespace thunder {

WorldBootstrapResult WorldBootstrap::load(const WorldPackReader& pack,
                                          const EconomyDefinitions& definitions) {
    auto topology = WorldTopology::load(pack);

    WorldBootstrapResult result;
    result.metadata = std::move(topology.metadata);
    result.sea_starts = std::move(topology.sea_starts);
    result.world_pack_hash = topology.world_pack_hash;
    result.scope_index = std::move(topology.scope_index);
    result.state_regions = std::move(topology.state_regions);
    result.adjacency = std::move(topology.adjacency);
    result.spatial_placement = std::move(topology.spatial_placement);
    result.static_layers = std::move(topology.static_layers);
    result.map_hierarchy = std::move(topology.map_hierarchy);
    result.map_labels = std::move(topology.map_labels);

    result.world.countries.reserve(topology.countries.size());
    for (const auto& country : topology.countries) {
        result.world.countries.create({country.tag, country.population, country.gdp,
                                       country.treasury, country.tax_rate});
    }
    result.world.markets.resize(topology.market_owners.size(), definitions);
    for (std::size_t index = 0u; index < topology.market_owners.size(); ++index) {
        result.world.markets.set_owner(
            MarketId{static_cast<MarketId::rep_type>(index)},
            topology.market_owners[index]);
    }
    result.world.geography = std::move(topology.geography);
    result.world.map_hierarchy = result.map_hierarchy;
    return result;
}

} // namespace thunder
