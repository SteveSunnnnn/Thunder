#include "thunder/presentation/render/RenderSnapshot.hpp"

namespace thunder {

void build_render_snapshot(const World& world, RenderSnapshot& out, std::uint64_t generation, std::uint64_t world_checksum) {
    out.generation = generation;
    out.world_checksum = world_checksum;
    out.countries.clear();
    if (out.countries.capacity() < world.countries.size()) out.countries.reserve(world.countries.size());

    for (std::size_t i = 0; i < world.countries.size(); ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        out.countries.push_back({
            id,
            static_cast<float>(world.countries.population(id)),
            static_cast<float>(world.countries.gdp(id)),
            static_cast<float>(world.countries.treasury(id)),
            static_cast<float>(world.countries.tax_rate(id))
        });
    }
}

RenderSnapshot build_render_snapshot(const World& world) {
    RenderSnapshot snapshot;
    snapshot.reserve(world.countries.size());
    build_render_snapshot(world, snapshot, 0, world.checksum());
    return snapshot;
}

} // namespace thunder
