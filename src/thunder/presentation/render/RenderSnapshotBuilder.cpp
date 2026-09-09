#include "thunder/presentation/render/RenderSnapshotBuilder.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <cmath>

namespace thunder {

void build_render_snapshot(const World& world, RenderSnapshot& out,
                           std::uint64_t generation,
                           std::uint64_t world_checksum) {
    out.generation = generation;
    out.world_checksum = world_checksum;

    // 1. Countries snapshot
    out.countries.clear();
    if (out.countries.capacity() < world.countries.size())
        out.countries.reserve(world.countries.size());

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

    // 2. Province political & seasonal climate state
    out.province_politics.clear();
    out.province_climates.clear();
    const auto prov_count = world.geography.province_count();
    if (out.province_politics.capacity() < prov_count)
        out.province_politics.reserve(prov_count);
    if (out.province_climates.capacity() < prov_count)
        out.province_climates.reserve(prov_count);

    const unsigned current_month = 1;
    const unsigned current_day = 8;

    for (std::size_t i = 0; i < prov_count; ++i) {
        const ProvinceId id{static_cast<ProvinceId::rep_type>(i)};
        const auto owner = world.geography.province_owner(id);
        const auto state = world.geography.province_state(id);
        const auto compound = world.geography.province_compound_terrain(id);
        const auto center_y = world.geography.province_center_y(id);

        const auto seasonal = evaluate_seasonal_province_state(compound, center_y, current_month, current_day);

        std::uint16_t flags = 0;
        if (world.geography.province_is_coastal(id)) flags |= 1u;
        if (world.geography.province_is_impassable(id)) flags |= 2u;
        if (state.valid()) {
            const auto res = world.geography.state_resistance_ppm(state);
            if (res > 200'000u) flags |= 4u; // unrest / resistance flag
        }
        if (seasonal.is_snow_covered) flags |= (1u << 3u);
        if (seasonal.is_frozen) flags |= (1u << 4u);

        out.province_politics.push_back({
            id,
            owner,
            owner, // default controller = owner
            flags,
            0u
        });

        out.province_climates.push_back({
            id,
            seasonal.temperature_c,
            static_cast<float>(seasonal.snow_cover_ppm) / 1'000'000.0f,
            static_cast<float>(seasonal.vegetative_activity_ppm) / 1'000'000.0f
        });
    }

    // 3. Dynamic infrastructure splines connecting adjacent active state hubs
    out.infrastructure_splines.clear();
    for (std::size_t i = 0; i < prov_count; ++i) {
        const ProvinceId id{static_cast<ProvinceId::rep_type>(i)};
        if (world.geography.province_kind(id) != ProvinceKind::Land) continue;
        const auto state = world.geography.province_state(id);
        if (!state.valid()) continue;

        const auto capital = world.geography.state_capital(state);
        if (capital.valid() && capital != id && capital.value() < prov_count) {
            const float x0 = static_cast<float>(world.geography.province_center_x(id));
            const float y0 = static_cast<float>(world.geography.province_center_y(id));
            const float x1 = static_cast<float>(world.geography.province_center_x(capital));
            const float y1 = static_cast<float>(world.geography.province_center_y(capital));

            out.infrastructure_splines.push_back({
                id,
                capital,
                x0,
                y0,
                x1,
                y1,
                0u, // default road
                1u  // active traffic
            });
        }
    }
}

RenderSnapshot build_render_snapshot(const World& world) {
    RenderSnapshot snapshot;
    snapshot.reserve(world.countries.size(), world.geography.province_count());
    build_render_snapshot(world, snapshot, 0, world.checksum());
    return snapshot;
}

} // namespace thunder
