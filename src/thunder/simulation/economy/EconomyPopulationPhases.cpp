#include "thunder/simulation/economy/EconomyPhaseInternals.hpp"
#include "thunder/simulation/economy/BuildingStore.hpp"
#include "thunder/simulation/economy/MarketStore.hpp"
#include "thunder/simulation/economy/PopStore.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

namespace thunder {

using namespace economy_detail;

// Weekly natural population growth (births minus deaths), the demand/labour
// side of the engine's growth loop: rising standards of living lower mortality
// AND — with a generational lag — fertility, reproducing the demographic
// transition: net growth rises through the early transition, peaks while
// mortality has fallen but fertility has not yet, then declines toward zero
// as prosperity completes the transition. Pure integer math on the same milli
// scales as the rest of the simulation — deterministic, no RNG.
//
// Weekly rates in ppm (approximate annual equivalents ×52):
//   fertility    800 ppm/week (≈ +42/1000/yr) up to 10000 milli SoL, then
//                falls linearly to 200 ppm/week (≈ +10/1000/yr) at 30000
//                milli — couples stop having many children only a
//                generation after prosperity arrives;
//   mortality    780 ppm/week (≈ +41/1000/yr) up to 5000 milli, falling
//                linearly to 180 ppm/week (≈ +9/1000/yr) at 30000 milli —
//                medicine and food security respond immediately;
//   crisis       below 2500 milli SoL mortality rises linearly by up to
//                +440 ppm/week — famine and destitution push net growth
//                negative (≈ −2.2%/year at zero SoL).
// Resulting net growth: ≈ +20 ppm/week (+1.0%/yr) at subsistence, peak
// ≈ +140 ppm/week (+0.73%/yr) at 10000 milli, ≈ +20 ppm/week (+0.1%/yr) at
// 30000 milli — the transition hump, not a straight line.
//
// Fractional weekly growth accumulates in the pop's growth_progress_milli
// column and is applied once it crosses a whole person, so POPs of any size
// grow at their exact rate.
JobDispatchStats EconomySystem::population_growth(World& world) {
    constexpr std::int64_t subsistence_sol_milli = 5000;
    constexpr std::int64_t fertility_decline_sol_milli = 10'000;
    constexpr std::int64_t affluent_sol_milli = 30'000;
    constexpr std::int64_t crisis_sol_milli = 2500;
    constexpr std::int64_t base_birth_ppm_week = 800;
    constexpr std::int64_t affluent_birth_ppm_week = 200;
    constexpr std::int64_t base_death_ppm_week = 780;
    constexpr std::int64_t affluent_death_ppm_week = 180;
    constexpr std::int64_t max_crisis_mortality_ppm = 440;
    constexpr std::int64_t milli_person_scale = 1000;

    auto populations = world.pops.populations_mut();
    auto growth_progress = world.pops.growth_progress_mut();
    const auto sols = world.pops.sol_all();

    for (std::size_t i = 0; i < populations.size(); ++i) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const auto sol = static_cast<std::int64_t>(sols[i]);

        // Mortality responds immediately to living standards.
        auto death_ppm = base_death_ppm_week;
        if (sol > subsistence_sol_milli) {
            const auto above = std::min<std::int64_t>(sol - subsistence_sol_milli,
                                                      affluent_sol_milli - subsistence_sol_milli);
            death_ppm -= above * (base_death_ppm_week - affluent_death_ppm_week) /
                         (affluent_sol_milli - subsistence_sol_milli);
        }
        if (sol < crisis_sol_milli) {
            death_ppm += (crisis_sol_milli - sol) * max_crisis_mortality_ppm / crisis_sol_milli;
        }

        // Fertility lags prosperity by design (the generational delay): it
        // only starts declining once a POP is firmly above subsistence.
        auto birth_ppm = base_birth_ppm_week;
        if (sol > fertility_decline_sol_milli) {
            const auto above = std::min<std::int64_t>(sol - fertility_decline_sol_milli,
                                                      affluent_sol_milli - fertility_decline_sol_milli);
            birth_ppm -= above * (base_birth_ppm_week - affluent_birth_ppm_week) /
                         (affluent_sol_milli - fertility_decline_sol_milli);
        }

        const auto net_ppm = birth_ppm - death_ppm;

        // Accumulate this week's fractional growth in milli-persons. The
        // accumulator is int64 so a huge POP's weekly delta cannot overflow
        // the persisted int32 remainder before whole persons are extracted.
        const auto weekly_milli_persons = static_cast<std::int64_t>(populations[i]) * net_ppm /
                                          milli_person_scale;
        auto& progress = growth_progress[i];
        const auto accumulated = static_cast<std::int64_t>(progress) + weekly_milli_persons;

        // Apply whole persons; keep the exact remainder for future weeks.
        auto applied = accumulated / milli_person_scale; // truncates toward zero
        auto population = static_cast<std::int64_t>(populations[i]) + applied;
        if (population < 1) {
            // A remnant household always survives; clamp and roll back the
            // unapplied part of the decline so the POP can recover.
            population = 1;
        }
        // PopulationCount ceiling: absurdly large POPs saturate instead of
        // wrapping the uint32 column.
        if (population > static_cast<std::int64_t>(std::numeric_limits<PopulationCount>::max())) {
            population = static_cast<std::int64_t>(std::numeric_limits<PopulationCount>::max());
        }
        applied = population - static_cast<std::int64_t>(populations[i]);
        if (applied != 0) {
            populations[i] = static_cast<PopulationCount>(population);
            progress = static_cast<std::int32_t>(std::clamp<std::int64_t>(
                accumulated - applied * milli_person_scale,
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()));
        } else {
            progress = static_cast<std::int32_t>(std::clamp<std::int64_t>(
                accumulated,
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()));
        }
    }
    return JobDispatchStats{};
}



JobDispatchStats EconomySystem::gather_pop_hot(World& world, JobSystem& jobs) {
    const auto pop_population = world.pops.populations();
    const auto pop_employers = world.pops.employers();
    const auto pop_need_profiles = world.pops.need_profiles();
    const auto pop_income = world.pops.incomes();
    const auto pop_cash = world.pops.cash_all();
    const auto pop_sol = world.pops.sol_all();
    const auto pop_provinces = world.pops.provinces();
    const auto pop_literacy = world.pops.literacy_all();
    const auto pop_qual = world.pops.qualifications_all();
    pop_hot_.resize(world.pops.size());
    pop_hot_offsets_.assign(world.markets.size() + 1u, 0u);
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        pop_hot_offsets_[mi + 1u] = pop_hot_offsets_[mi]
            + static_cast<std::uint32_t>(index_.pops(market).size());
    }
    return jobs.parallel_for(world.markets.size(), 1u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                const auto ids = index_.pops(market);
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], ids.size());
                for (std::size_t k = 0; k < ids.size(); ++k) {
                    const auto pi = static_cast<std::size_t>(ids[k].value());
                    const auto prov = pop_provinces[pi];
                    const auto lit = pop_literacy[pi];
                const auto q = pop_qual[pi];
                // Clamp the need profile here so every downstream consumer can
                // index the per-profile scratch buffers directly. A POP created
                // at runtime without a profile carries NeedProfileId{} whose
                // value is 0xFFFFFFFF, which would index ~4G elements past the
                // end of a markets x profile_count buffer.
                auto profile = pop_need_profiles[pi];
                if (!profile.valid() ||
                    static_cast<std::size_t>(profile.value()) >= definitions_.need_profile_count()) {
                    profile = definitions_.need_profile_count() > 0u
                                  ? NeedProfileId{0u}
                                  : NeedProfileId{};
                }
                rows[k] = PopHotRow{pop_population[pi], 0u, pop_employers[pi], profile,
                                    pop_income[pi], pop_cash[pi], pop_sol[pi],
                                    lit, q,
                                    static_cast<std::uint16_t>(prov.valid() ? prov.value() : 0xFFFFu)};
                }
            }
        });
}


JobDispatchStats EconomySystem::scatter_pop_hot(World& world, JobSystem& jobs) {
    auto pop_employers = world.pops.employers_mut();
    auto pop_employed = world.pops.employed_mut();
    auto pop_income = world.pops.incomes_mut();
    auto pop_cash = world.pops.cash_mut();
    auto pop_sol = world.pops.sol_mut();
    auto pop_qual = world.pops.qualifications_mut();
    auto pop_radicals = world.pops.radicals_mut();
    auto pop_loyalists = world.pops.loyalists_mut();
    const auto pop_literacy = world.pops.literacy_all();
    const auto pop_population = world.pops.populations();

    return jobs.parallel_for(world.markets.size(), 1u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                const auto ids = index_.pops(market);
                const auto rows = std::span<const PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], ids.size());
                for (std::size_t k = 0; k < ids.size(); ++k) {
                    const auto pi = static_cast<std::size_t>(ids[k].value());
                    pop_employers[pi] = rows[k].employer;
                    pop_employed[pi] = rows[k].employed;
                    pop_income[pi] = rows[k].income_milli;
                    pop_cash[pi] = rows[k].cash_milli;
                    const auto new_sol = rows[k].sol_milli;
                    pop_sol[pi] = new_sol;
                    pop_qual[pi] = rows[k].qualification_permyriad;

                    // POP Radicalism & Loyalist Dynamics (Victoria 3 standard):
                    // Higher literacy raises the expected SoL threshold.
                    const auto expected_sol = 5000 + (static_cast<std::int32_t>(pop_literacy[pi]) * 2);
                    const auto pop_size = pop_population[pi];

                    if (new_sol < expected_sol) {
                        const std::uint64_t diff = static_cast<std::uint64_t>(expected_sol - new_sol);
                        const auto ratio_ppm = std::min<std::uint64_t>(1'000'000u, (diff * 1'000'000u) / static_cast<std::uint64_t>(std::max(1, expected_sol)));
                        pop_radicals[pi] = static_cast<PopulationCount>((static_cast<std::uint64_t>(pop_size) * ratio_ppm) / 1'000'000u);
                        pop_loyalists[pi] = 0u;
                    } else if (new_sol > expected_sol) {
                        const std::uint64_t diff = static_cast<std::uint64_t>(new_sol - expected_sol);
                        const auto ratio_ppm = std::min<std::uint64_t>(1'000'000u, (diff * 1'000'000u) / static_cast<std::uint64_t>(std::max(1, new_sol)));
                        pop_loyalists[pi] = static_cast<PopulationCount>((static_cast<std::uint64_t>(pop_size) * ratio_ppm) / 1'000'000u);
                        pop_radicals[pi] = 0u;
                    } else {
                        pop_radicals[pi] = 0u;
                        pop_loyalists[pi] = 0u;
                    }
                }
            }
        });
}

JobDispatchStats EconomySystem::employment(World& world, JobSystem& jobs) {
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();
    const auto building_provinces = world.buildings.provinces();
    auto building_employees = world.buildings.employees_mut();
    const auto wage_offers = world.buildings.wage_offers();
    auto building_cash = world.buildings.cash_mut();
    const auto type_defs = definitions_.building_types();

    return jobs.parallel_for(world.markets.size(), 1u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                // Competitive labor market: rank this market's buildings by
                // wage offer (descending, id-ascending tie break).
                const auto market_buildings = index_.buildings(market);
                struct FlatBucket {
                    std::uint16_t prov;
                    std::uint32_t offset;
                    std::uint32_t count;
                };
                static thread_local std::vector<std::size_t> order;
                static thread_local std::vector<std::size_t> unassigned_buildings;
                static thread_local std::vector<std::size_t> bucketed_buildings;
                static thread_local std::vector<FlatBucket> flat_buckets;
                static thread_local std::vector<std::uint32_t> prov_to_bucket;
                order.clear();
                unassigned_buildings.clear();
                bucketed_buildings.clear();
                flat_buckets.clear();

                order.reserve(market_buildings.size());
                for (const auto b : market_buildings) order.push_back(static_cast<std::size_t>(b.value()));
                if (order.size() > 1u) {
                    if (order.size() <= 8u) {
                        for (std::size_t i = 1; i < order.size(); ++i) {
                            const auto key = order[i];
                            std::size_t j = i;
                            while (j > 0 && (wage_offers[key] > wage_offers[order[j - 1]] ||
                                   (wage_offers[key] == wage_offers[order[j - 1]] && key < order[j - 1]))) {
                                order[j] = order[j - 1];
                                --j;
                            }
                            order[j] = key;
                        }
                    } else {
                        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                            if (wage_offers[a] != wage_offers[b]) return wage_offers[a] > wage_offers[b];
                            return a < b;
                        });
                    }
                }
                for (const auto bi : order) {
                    const auto& type = type_defs[building_types[bi].value()];
                    const std::uint64_t capacity = static_cast<std::uint64_t>(type.workers_per_level) * building_levels[bi];
                    building_remaining_[bi] = static_cast<PopulationCount>(std::min<std::uint64_t>(capacity, std::numeric_limits<PopulationCount>::max()));
                    building_employees[bi] = 0u;

                    const auto b_prov = building_provinces[bi];
                    const auto prov_r16 = static_cast<std::uint16_t>(b_prov.valid() ? b_prov.value() : 0xFFFFu);
                    if (prov_r16 == 0xFFFFu) {
                        unassigned_buildings.push_back(bi);
                    } else {
                        if (prov_r16 >= prov_to_bucket.size()) {
                            prov_to_bucket.resize(static_cast<std::size_t>(prov_r16) + 1024u, 0u);
                        }
                        const auto entry = prov_to_bucket[prov_r16];
                        if (entry != 0u) {
                            ++flat_buckets[entry - 1u].count;
                        } else {
                            flat_buckets.push_back(FlatBucket{prov_r16, 0u, 1u});
                            prov_to_bucket[prov_r16] = static_cast<std::uint32_t>(flat_buckets.size());
                        }
                    }
                }
                if (!flat_buckets.empty()) {
                    std::uint32_t cursor = 0;
                    for (auto& b : flat_buckets) {
                        b.offset = cursor;
                        cursor += b.count;
                        b.count = 0;
                    }
                    bucketed_buildings.resize(cursor);
                    for (const auto bi : order) {
                        const auto b_prov = building_provinces[bi];
                        const auto prov_r16 = static_cast<std::uint16_t>(b_prov.valid() ? b_prov.value() : 0xFFFFu);
                        if (prov_r16 != 0xFFFFu) {
                            const auto b_idx = prov_to_bucket[prov_r16] - 1u;
                            auto& b = flat_buckets[b_idx];
                            bucketed_buildings[b.offset + b.count++] = bi;
                        }
                    }
                }
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], index_.pops(market).size());
                for (auto& row : rows) {
                    PopulationCount total_employed = 0u;
                    PopulationCount remaining_pop = row.population;
                    // This week's wage income is rebuilt segment by segment below.
                    row.income_milli = 0;

                    // Reservation Wage based on subsistence baseline, SoL, and literacy:
                    const EconomyPrice reservation_wage = static_cast<EconomyPrice>(
                        100 + (row.sol_milli > 10'000 ? (row.sol_milli - 10'000) / 250 : 0) + row.literacy_permyriad / 100);

                    // Wage settlement is per hire segment: each building pays for
                    // exactly the workers IT hired at ITS OWN offer, debited from
                    // its own cash (down to the credit limit). The previous scheme
                    // charged the POP's entire headcount to the LAST building that
                    // hired any remainder — earlier (higher-wage) employers got
                    // free labour while the last employer paid phantom wages.
                    const auto hire_from = [&](std::size_t bi, PopulationCount hire_count) {
                        building_employees[bi] += hire_count;
                        const EconomyAmount segment_bill =
                            mul_div_nonnegative(static_cast<EconomyAmount>(hire_count), wage_offers[bi], 1);
                        const EconomyAmount payable = std::max<EconomyAmount>(
                            0, saturating_sub(building_cash[bi], building_credit_limit_milli));
                        const EconomyAmount paid = std::min(segment_bill, payable);
                        if (paid > 0) {
                            building_cash[bi] = saturating_sub(building_cash[bi], paid);
                            row.cash_milli = saturating_add(row.cash_milli, paid);
                            row.income_milli = saturating_add(row.income_milli, paid);
                        }
                    };

                    // Find localized candidate buildings for this POP's province
                    std::span<const std::size_t> candidates;
                    if (row.province_r16 != 0xFFFFu) {
                        if (row.province_r16 < prov_to_bucket.size() && prov_to_bucket[row.province_r16] != 0u) {
                            const auto& bucket = flat_buckets[prov_to_bucket[row.province_r16] - 1u];
                            candidates = std::span<const std::size_t>{bucketed_buildings.data() + bucket.offset, bucket.count};
                        }
                    } else {
                        candidates = order;
                    }

                    if (!candidates.empty()) {
                        for (const auto bi : candidates) {
                            if (remaining_pop == 0u) break;
                            auto& remaining = building_remaining_[bi];
                            if (remaining == 0u) continue;

                            // Reservation wage check
                            if (wage_offers[bi] < reservation_wage) continue;

                            const auto hire_count = std::min(remaining_pop, remaining);
                            remaining -= hire_count;
                            hire_from(bi, hire_count);
                            remaining_pop -= hire_count;
                            total_employed += hire_count;
                            row.employer = BuildingId{static_cast<BuildingId::rep_type>(bi)};
                        }
                    }

                    // Fallback to unassigned buildings if any capacity left
                    if (remaining_pop > 0u && !unassigned_buildings.empty()) {
                        for (const auto bi : unassigned_buildings) {
                            if (remaining_pop == 0u) break;
                            auto& remaining = building_remaining_[bi];
                            if (remaining == 0u) continue;
                            if (wage_offers[bi] < reservation_wage) continue;

                            const auto hire_count = std::min(remaining_pop, remaining);
                            remaining -= hire_count;
                            hire_from(bi, hire_count);
                            remaining_pop -= hire_count;
                            total_employed += hire_count;
                            row.employer = BuildingId{static_cast<BuildingId::rep_type>(bi)};
                        }
                    }

                    if (total_employed == 0u) {
                        row.employer = BuildingId{};
                    }
                    row.employed = total_employed;
                }
                for (const auto& b : flat_buckets) {
                    prov_to_bucket[b.prov] = 0u;
                }
            }
        });
}




JobDispatchStats EconomySystem::production(World& world, JobSystem& jobs) {
    // Derive last tick's fulfillment from serialized world state (flows are
    // only cleared below), so save/load round-trips stay checksum-identical
    // without persisting EconomySystem scratch.
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        const auto supply = world.markets.supply_row(market);
        const auto demand = world.markets.demand_row(market);
        const auto inventory = world.markets.inventory_row(market);
        auto fulfillment = std::span<std::int64_t>{
            market_fulfillment_ppm_.data() + mi * supply.size(), supply.size()};
        for (std::size_t gi = 0; gi < supply.size(); ++gi) {
            const EconomyAmount available = saturating_add(supply[gi], inventory[gi]);
            fulfillment[gi] = demand[gi] > 0
                ? std::min(mul_div_nonnegative(std::min(available, demand[gi]), ppm_scale, demand[gi]), ppm_scale)
                : (available > 0 ? ppm_scale : 0);
        }
    }
    world.markets.clear_flows();
    const auto building_types = world.buildings.types();
    const auto building_employees = world.buildings.employees_all();
    const auto building_methods = world.buildings.production_methods();
    const auto building_provinces = world.buildings.provinces();
    const auto type_defs = definitions_.building_types();
    const auto input_flows = definitions_.input_flows();
    const auto output_flows = definitions_.output_flows();
    // Use market-level grain; for single-market world, shard by stable building ID
    // to keep determinism across worker counts (future: shard inside market)
    return jobs.parallel_for(world.markets.size(), 1u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                auto supply = world.markets.supply_row(market);
                auto demand = world.markets.demand_row(market);
                const auto fulfillment = std::span<const std::int64_t>{
                    market_fulfillment_ppm_.data() + mi * supply.size(), supply.size()};
                for (const auto b : index_.buildings(market)) {
                    const auto bi = static_cast<std::size_t>(b.value());
                    const auto workers = building_employees[bi];
                    const auto& type = type_defs[building_types[bi].value()];
                    auto input_begin = type.input_begin;
                    auto input_count = type.input_count;
                    auto output_begin = type.output_begin;
                    auto output_count = type.output_count;
                    std::int32_t throughput_ppm = 1'000'000;
                    const auto method_id = building_methods[bi];
                    if (method_id.valid()) {
                        const auto& method = definitions_.production_method(method_id);
                        if (method.building_type == building_types[bi]) {
                            input_begin = method.input_begin;
                            input_count = method.input_count;
                            output_begin = method.output_begin;
                            output_count = method.output_count;
                            throughput_ppm = method.throughput_ppm;
                        }
                    }
                    const auto inputs = input_flows.subspan(input_begin, input_count);
                    const auto outputs = output_flows.subspan(output_begin, output_count);
                    // Natural soil fertility, land deposit capacity & seasonal temperature activity
                    if (inputs.empty() && bi < building_provinces.size()) {
                        const auto prov = building_provinces[bi];
                        if (prov.valid() && prov.value() < world.geography.province_count()) {
                            const auto compound = world.geography.province_compound_terrain(prov);
                            const auto center_y = world.geography.province_center_y(prov);
                            const auto seasonal = evaluate_seasonal_province_state(compound, center_y, 1u, 8u);
                            const auto fertility_ppm = soil_fertility_ppm(compound.vegetation, compound.climate);
                            const auto combined_fertility = mul_div_nonnegative(
                                static_cast<EconomyAmount>(fertility_ppm),
                                static_cast<EconomyAmount>(seasonal.vegetative_activity_ppm), ppm_scale);
                            throughput_ppm = static_cast<std::int32_t>(mul_div_nonnegative(
                                throughput_ppm, combined_fertility, ppm_scale));
                        }
                    }
                    // Input shortage rationing: if a building requires raw materials (inputs),
                    // and ANY input is missing / 0 available, throughput MUST drop to 0!
                    // (Cannot produce output goods out of thin air without raw materials!)
                    for (const auto& flow : inputs) {
                        throughput_ppm = static_cast<std::int32_t>(std::min<std::int64_t>(
                            throughput_ppm, fulfillment[flow.good.value()]));
                    }
                    if (!inputs.empty() && throughput_ppm <= 0) {
                        throughput_ppm = 0;
                    }
                    building_throughput_ppm_[bi] = throughput_ppm;
                    for (const auto& flow : inputs) {
                        const auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        demand[flow.good.value()] = saturating_add(demand[flow.good.value()], mul_div_nonnegative(q, throughput_ppm, ppm_scale));
                    }
                    for (const auto& flow : outputs) {
                        const auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        supply[flow.good.value()] = saturating_add(supply[flow.good.value()], mul_div_nonnegative(q, throughput_ppm, ppm_scale));
                    }
                }
            }
        });
}



JobDispatchStats EconomySystem::consumption(World& world, JobSystem& jobs) {
    const auto profile_defs = definitions_.need_profiles();
    const auto need_flows = definitions_.need_flows();
    const std::size_t profile_count = profile_defs.size();
    return jobs.parallel_for(world.markets.size(), 1u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                auto demand = world.markets.demand_row(market);
                auto* profile_population = profile_population_.data() + mi * profile_count;
                std::fill(profile_population, profile_population + profile_count, std::uint64_t{0});
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], index_.pops(market).size());
                for (auto& row : rows) {
                    // Wages were already settled per hire segment during the
                    // employment phase: row.income_milli is the exact amount the
                    // POP was actually paid this week (0 when unemployed or when
                    // the employer's cash plus credit line could not cover it).
                    const EconomyAmount income = row.income_milli;

                    // EFFECTIVE DEMAND SCALED BY LITERACY AND SOL:
                    // Base: penniless/unemployed POPs have 25% basic subsistence demand.
                    // Solvent POPs scale demand with Standard of Living and Literacy expectations.
                    std::uint64_t effective_pop = 0u;
                    if (income > 0 || row.cash_milli > 0) {
                        const std::int64_t sol_scale_ppm = std::clamp<std::int64_t>(
                            1'000'000LL + (static_cast<std::int64_t>(row.sol_milli) * 10LL) + (static_cast<std::int64_t>(row.literacy_permyriad) * 100LL),
                            800'000LL, 4'000'000LL);
                        effective_pop = mul_div_nonnegative(static_cast<std::uint64_t>(row.population), sol_scale_ppm, ppm_scale);
                    } else {
                        effective_pop = static_cast<std::uint64_t>(row.population) / 4u;
                    }
                    // Guarded by the same clamp applied when the hot rows were
                    // gathered; profile_count == 0 leaves no bucket to write.
                    if (profile_count > 0u &&
                        static_cast<std::size_t>(row.need_profile.value()) < profile_count) {
                        profile_population[row.need_profile.value()] += effective_pop;
                    }
                }
                for (std::size_t profile_index = 0; profile_index < profile_count; ++profile_index) {
                    const auto population = profile_population[profile_index];
                    if (population == 0u) continue;
                    const auto& profile = profile_defs[profile_index];
                    const auto flows = need_flows.subspan(profile.flow_begin, profile.flow_count);
                    std::size_t k = 0;
                    for (; k + 4u <= flows.size(); k += 4u) {
                        for (std::size_t j = 0; j < 4u; ++j) {
                            const auto& need = flows[k + j];
                            const auto add = mul_div_nonnegative(
                                static_cast<EconomyAmount>(population),
                                need.quantity_milli_per_1000_people, 1000);
                            demand[need.good.value()] = saturating_add(demand[need.good.value()], add);
                        }
                    }
                    for (; k < flows.size(); ++k) {
                        const auto& need = flows[k];
                        const auto add = mul_div_nonnegative(
                            static_cast<EconomyAmount>(population),
                            need.quantity_milli_per_1000_people, 1000);
                        demand[need.good.value()] = saturating_add(demand[need.good.value()], add);
                    }
                }
            }
        });
}
} // namespace thunder
