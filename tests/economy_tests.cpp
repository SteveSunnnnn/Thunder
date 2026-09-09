#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/economy/EconomySystem.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace thunder;

struct EconomyFixture {
    EconomyDefinitions definitions;
    World world;
    GoodId grain;
    GoodId coal;
    GoodId iron;
    GoodId tools;
    GoodId clothes;
    NeedProfileId worker_needs;
    std::vector<BuildingTypeId> building_types;
};

static EconomyFixture make_fixture(std::size_t markets, std::size_t buildings_per_market,
                                   std::size_t pops_per_market, PopulationCount pop_size) {
    EconomyFixture f;
    f.grain = f.definitions.add_good({"grain", 1000});
    f.coal = f.definitions.add_good({"coal", 1400});
    f.iron = f.definitions.add_good({"iron", 1800});
    f.tools = f.definitions.add_good({"tools", 3200});
    f.clothes = f.definitions.add_good({"clothes", 2200});

    const std::array<NeedFlow, 2> needs{{{f.grain, 8000}, {f.clothes, 1500}}};
    f.worker_needs = f.definitions.add_need_profile("workers", needs);

    const std::array<RecipeFlow, 0> none{};
    const std::array<RecipeFlow, 1> farm_out{{{f.grain, 12'000}}};
    const std::array<RecipeFlow, 1> coal_out{{{f.coal, 8'000}}};
    const std::array<RecipeFlow, 1> iron_out{{{f.iron, 7'000}}};
    const std::array<RecipeFlow, 2> tools_in{{{f.iron, 4'000}, {f.coal, 2'000}}};
    const std::array<RecipeFlow, 1> tools_out{{{f.tools, 5'000}}};
    const std::array<RecipeFlow, 1> clothes_out{{{f.clothes, 8'000}}};
    f.building_types.push_back(f.definitions.add_building_type("farm", 1000, none, farm_out));
    f.building_types.push_back(f.definitions.add_building_type("coal_mine", 1000, none, coal_out));
    f.building_types.push_back(f.definitions.add_building_type("iron_mine", 1000, none, iron_out));
    f.building_types.push_back(f.definitions.add_building_type("toolworks", 1000, tools_in, tools_out));
    f.building_types.push_back(f.definitions.add_building_type("textile", 1000, none, clothes_out));

    f.world.countries.reserve(markets);
    for (std::size_t m=0;m<markets;++m) {
        f.world.countries.create({"TST", static_cast<double>(pops_per_market * pop_size), 100.0, 10.0, 0.20});
    }
    f.world.markets.resize(markets, f.definitions);
    for (std::size_t m=0;m<markets;++m) {
        f.world.markets.set_owner(MarketId{static_cast<MarketId::rep_type>(m)}, CountryId{static_cast<CountryId::rep_type>(m)});
    }

    f.world.buildings.reserve(markets * buildings_per_market);
    f.world.pops.reserve(markets * pops_per_market);
    for (std::size_t m=0;m<markets;++m) {
        std::vector<BuildingId> employers;
        employers.reserve(buildings_per_market);
        for (std::size_t b=0;b<buildings_per_market;++b) {
            employers.push_back(f.world.buildings.create({MarketId{static_cast<MarketId::rep_type>(m)},
                f.building_types[b % f.building_types.size()], 1u,
                static_cast<EconomyPrice>(900 + (b % 7u) * 60u), 50'000}));
        }
        for (std::size_t p=0;p<pops_per_market;++p) {
            f.world.pops.create({MarketId{static_cast<MarketId::rep_type>(m)}, pop_size,
                employers[p % employers.size()], f.worker_needs});
        }
    }
    return f;
}

static void test_fixed_point_math() {
    assert(mul_div_nonnegative(1000, 2500, 1000) == 2500);
    assert(signed_ratio_ppm(50, 100) == 500'000);
    assert(signed_ratio_ppm(-50, 100) == -500'000);
    const auto max = std::numeric_limits<EconomyAmount>::max();
    const auto min = std::numeric_limits<EconomyAmount>::min();
    assert(mul_div_nonnegative(max, max, 1) == max);
    assert(mul_div_nonnegative(max, max, max) == max);
    assert(signed_ratio_ppm(min, max) == -1'000'000);
    assert(saturating_add(max, 1) == max);
    assert(saturating_add(min, -1) == min);
    assert(saturating_sub(min, 1) == min);
    assert(saturating_sub(max, -1) == max);
}

static void test_headline_tax_rate_drives_income_tax_policy() {
    World world;
    const auto country = world.countries.create({"TAX", 0.0, 0.0, 0.0, 0.20});
    world.countries.set_tax_rate(country, 0.35);
    assert(world.countries.tax_policy(country).income_tax_ppm == 350'000);

    TaxPolicy policy;
    policy.income_tax_ppm = 2'000'000;
    policy.consumption_tax_ppm = -1;
    world.countries.set_tax_policy(country, policy);
    assert(world.countries.tax_policy(country).income_tax_ppm == 1'000'000);
    assert(world.countries.tax_policy(country).consumption_tax_ppm == 0);
}

static void test_economy_definitions_reject_unsafe_content() {
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1'000});
    bool rejected = false;
    try { (void)definitions.add_good({"grain", 2'000}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    const std::array<RecipeFlow, 1> invalid_good{{{GoodId{99u}, 1'000}}};
    rejected = false;
    try { (void)definitions.add_building_type("invalid", 1'000, {}, invalid_good); }
    catch (const std::out_of_range&) { rejected = true; }
    assert(rejected);

    const std::array<NeedFlow, 1> negative_need{{{grain, -1}}};
    rejected = false;
    try { (void)definitions.add_need_profile("negative", negative_need); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

static void test_market_entity_index_and_weekly_flow() {
    auto f = make_fixture(4u, 20u, 100u, 100u);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    assert(economy.index().market_count() == 4u);
    assert(economy.index().pops(MarketId{2u}).size() == 100u);
    assert(economy.index().buildings(MarketId{2u}).size() == 20u);

    JobSystem jobs{2u};
    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);
    assert(profile.total.count() > 0);
    assert(f.world.markets.demand(MarketId{0u}, f.grain) > 0);
    assert(f.world.markets.supply(MarketId{0u}, f.grain) > 0);
    assert(f.world.pops.income(PopId{0u}) > 0);
    assert(f.world.pops.standard_of_living_milli(PopId{0u}) >= 0);
    assert(std::abs(f.world.countries.population(CountryId{0u}) - 10'000.0) < 0.5);
    assert(f.world.countries.gdp(CountryId{0u}) >= 0.0);
}


static void test_market_index_rebuilds_on_membership_changes() {
    auto f = make_fixture(2u, 4u, 8u, 100u);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    assert(economy.index().pops(MarketId{0u}).size() == 8u);
    assert(economy.index().buildings(MarketId{0u}).size() == 4u);

    f.world.pops.set_market(PopId{0u}, MarketId{1u});
    f.world.pops.set_employer(PopId{0u}, BuildingId{4u});
    f.world.buildings.set_market(BuildingId{0u}, MarketId{1u});

    JobSystem jobs{0u};
    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);
    assert(economy.index().pops(MarketId{0u}).size() == 7u);
    assert(economy.index().pops(MarketId{1u}).size() == 9u);
    assert(economy.index().buildings(MarketId{0u}).size() == 3u);
    assert(economy.index().buildings(MarketId{1u}).size() == 5u);
}

static void test_scarcity_raises_price() {
    auto f = make_fixture(1u, 5u, 500u, 200u);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    const auto before = f.world.markets.price(MarketId{0u}, f.grain);
    JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);
    const auto after = f.world.markets.price(MarketId{0u}, f.grain);
    assert(after > before);
}

static void test_economy_is_deterministic_across_worker_counts() {
    // 128 markets create 32 fixed partitions, exceeding JobSystem's inline
    // threshold. This is intentionally large enough to exercise real worker
    // execution instead of comparing two serial paths that merely have
    // different worker counts configured.
    auto fixture = make_fixture(128u, 24u, 160u, 100u);
    World serial_world = fixture.world;
    World parallel_world = fixture.world;
    EconomySystem serial_economy{fixture.definitions};
    EconomySystem parallel_economy{fixture.definitions};
    serial_economy.rebuild_indices(serial_world);
    parallel_economy.rebuild_indices(parallel_world);
    JobSystem serial_jobs{0u};
    JobSystem parallel_jobs{4u};
    EconomyTickProfile parallel_profile;
    for (int week=0;week<24;++week) {
        serial_economy.run_weekly(serial_world, serial_jobs);
        parallel_economy.run_weekly(parallel_world, parallel_jobs, &parallel_profile);
    }
    assert(parallel_profile.workers_used > 1u);
    assert(serial_world.checksum() == parallel_world.checksum());
}

static void test_pop_storage_is_compact() {
    auto f = make_fixture(8u, 20u, 10'000u, 100u);
    const double hot_bytes_per_pop = static_cast<double>(f.world.pops.hot_memory_bytes()) / static_cast<double>(f.world.pops.size());
    const double total_bytes_per_pop = static_cast<double>(f.world.pops.memory_bytes()) / static_cast<double>(f.world.pops.size());
    assert(hot_bytes_per_pop <= 48.0);
    assert(total_bytes_per_pop <= 96.0);
}

static void test_population_growth_tracks_standard_of_living() {
    // North-star growth loop with a realistic demographic transition: mortality
    // responds to living standards immediately, fertility lags a generation —
    // net growth therefore peaks mid-transition (10000 milli) and declines
    // toward zero as prosperity completes (30000 milli). Destitution shrinks.
    // Fractional weekly growth accumulates exactly in growth_progress_milli.
    auto f = make_fixture(1u, 1u, 4u, 10'000u);
    EconomySystem economy{f.definitions};
    const PopId transition_peak{0u};
    const PopId affluent{1u};
    const PopId subsistence{2u};
    const PopId destitute{3u};
    f.world.pops.set_standard_of_living_milli(transition_peak, 10'000);
    f.world.pops.set_standard_of_living_milli(affluent, 30'000);
    f.world.pops.set_standard_of_living_milli(subsistence, 5'000);
    f.world.pops.set_standard_of_living_milli(destitute, 0);

    // Net rates: peak 140 ppm/week (mortality has fallen, fertility has not),
    // affluent 20 ppm/week (fertility decline caught up), subsistence 20
    // ppm/week, destitute -420 ppm/week (mortality crisis).
    for (int week = 0; week < 5; ++week) economy.population_growth(f.world);
    assert(f.world.pops.population(transition_peak) == 10'007u);
    assert(f.world.pops.growth_progress_milli(transition_peak) == 0);
    assert(f.world.pops.population(affluent) == 10'001u);
    assert(f.world.pops.growth_progress_milli(affluent) == 0);
    assert(f.world.pops.population(subsistence) == 10'001u);
    assert(f.world.pops.growth_progress_milli(subsistence) == 0);
    // Decline compounds (the weekly delta scales with the shrinking POP).
    assert(f.world.pops.population(destitute) == 9'980u);

    // The transition peak outgrows both edges — wealth does NOT monotonically
    // raise growth; completed prosperity slows it back down.
    const auto peak_growth = static_cast<std::int64_t>(f.world.pops.population(transition_peak)) - 10'000;
    const auto affluent_growth = static_cast<std::int64_t>(f.world.pops.population(affluent)) - 10'000;
    assert(peak_growth > affluent_growth * 2);

    // The remnant floor: a lone survivor in a mortality crisis never drops
    // below one person.
    f.world.pops.set_population(destitute, 1u);
    f.world.pops.set_standard_of_living_milli(destitute, 0);
    for (int week = 0; week < 8; ++week) economy.population_growth(f.world);
    assert(f.world.pops.population(destitute) == 1u);

    // Growth is wired into the weekly pipeline: settlement re-derives the
    // country aggregate from the grown POP sizes. The aggregate must match the
    // post-run POP sum exactly and exceed the pre-run sum (the week's growth
    // flowed through).
    for (std::size_t i = 0; i < f.world.pops.size(); ++i)
        f.world.pops.set_standard_of_living_milli(PopId{static_cast<PopId::rep_type>(i)}, 10'000);
    std::uint64_t pop_sum_before = 0;
    for (std::size_t i = 0; i < f.world.pops.size(); ++i)
        pop_sum_before += f.world.pops.population(PopId{static_cast<PopId::rep_type>(i)});
    JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);
    std::uint64_t pop_sum_after = 0;
    for (std::size_t i = 0; i < f.world.pops.size(); ++i)
        pop_sum_after += f.world.pops.population(PopId{static_cast<PopId::rep_type>(i)});
    assert(f.world.countries.population(CountryId{0u}) == static_cast<double>(pop_sum_after));
    assert(pop_sum_after > pop_sum_before);
}

static void test_wages_paid_per_building_at_own_offer() {
    // Regression: a POP that splits across multiple buildings must be paid
    // segment by segment — each building debited for exactly the workers IT
    // hired at ITS OWN offer. The old scheme charged the POP's whole headcount
    // to the LAST (lowest-wage) employer, giving earlier employers free labour
    // and making the last one pay phantom wages.
    EconomyDefinitions definitions;
    const auto coal = definitions.add_good({"coal", 1400});
    const auto iron = definitions.add_good({"iron", 1800});
    const std::array<NeedFlow, 0> no_needs{};
    const auto needs = definitions.add_need_profile("workers", no_needs);
    const std::array<RecipeFlow, 0> no_inputs{};
    const std::array<RecipeFlow, 1> coal_out{{{coal, 8'000}}};
    const std::array<RecipeFlow, 1> iron_out{{{iron, 7'000}}};
    const auto coal_type = definitions.add_building_type("coal_mine", 1000, no_inputs, coal_out);
    const auto iron_type = definitions.add_building_type("iron_mine", 1000, no_inputs, iron_out);

    World world;
    // Zero taxes so the POP income assertion isolates the wage legs exactly.
    world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, CountryId{0});

    // Cash is enough to pay wages in full but below the dividend reserve cap,
    // so settlement adds no dividend leg to the building cash columns.
    const auto high = world.buildings.create({MarketId{0}, iron_type, 1u, 1000, 2'000'000});
    const auto low = world.buildings.create({MarketId{0}, coal_type, 1u, 600, 2'000'000});
    world.pops.create({MarketId{0}, 1500u, BuildingId{}, needs});

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    economy.run_weekly(world, jobs);

    // Competitive hiring fills the higher offer first: 1000 at 1000 milli,
    // then the 500 remainder at 600 milli.
    assert(world.buildings.employees(high) == 1000u);
    assert(world.buildings.employees(low) == 500u);
    // Each building's cash fell by exactly its own wage bill. Unsold outputs
    // (nobody demands coal/iron) contribute zero revenue, so cash deltas are
    // purely the wage legs.
    assert(world.buildings.cash(high) == 2'000'000 - 1000 * 1000);
    assert(world.buildings.cash(low) == 2'000'000 - 500 * 600);
    // POP income is the exact segment sum (the old code paid 1500 x 600).
    assert(world.pops.income(PopId{0u}) == 1000 * 1000 + 500 * 600);
}

static void test_population_growth_huge_pop_saturation() {
    // Robustness boundary: a POP near the uint32 ceiling must saturate instead
    // of overflowing the int32 fractional accumulator or wrapping the column.
    auto f = make_fixture(1u, 1u, 1u, 100u);
    EconomySystem economy{f.definitions};
    const PopId giant{0u};
    f.world.pops.set_population(giant, 4'000'000'000u);
    f.world.pops.set_standard_of_living_milli(giant, 10'000); // transition peak: +140 ppm/week
    economy.population_growth(f.world);
    const auto after = f.world.pops.population(giant);
    assert(after > 4'000'000'000u); // grew without UB/wraparound
    // Weekly delta: 4e9 * 140/1000 milli-persons = 560'000'000 milli = 560'000 persons.
    assert(after == 4'000'560'000u);
}

static void test_metal_market_price_feeds_fx_anchor() {
    // R1: bullion is an ordinary market good whose price moves the metallic
    // anchor. A gold glut (market price 2x base) must drag gold-standard
    // parities up with it instead of the hardcoded 1'000'000 reference.
    EconomyDefinitions definitions;
    GoodDefinition gold_def;
    gold_def.key = "gold_bullion";
    gold_def.base_price_milli = 32'000;
    gold_def.category = GoodCategory::Currency;
    gold_def.monetary_metal_key = metal_gold_key;
    const auto gold = definitions.add_good(std::move(gold_def));
    const std::array<NeedFlow, 0> no_needs{};
    (void)definitions.add_need_profile("workers", no_needs);

    World world;
    world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, CountryId{0});
    const CurrencyKey gcur = economy_stable_key("currency.gold_test");
    world.currencies.register_currency(gcur, "Gold Test", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    world.markets.set_currency_key(MarketId{0}, gcur);

    // Flood the market: bullion at twice its base price. (The weekly price
    // phase pulls it partway back toward base, so the test reads the settled
    // price back and derives the expected anchor from it.)
    world.markets.price_row(MarketId{0})[gold.value()] = 64'000;

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    economy.run_weekly(world, jobs);

    // The anchor tracks the settled market price 1:1 against base, and the
    // glut keeps it above the 1.0 fallback.
    const auto settled_price = world.markets.price(MarketId{0}, gold);
    const EconomyPrice expected_ppm = settled_price * 1'000'000 / 32'000;
    assert(expected_ppm > 1'000'000);
    assert(world.currencies.exchange_rate_ppm(metal_gold_key) == expected_ppm);
    assert(world.currencies.target_rate_ppm(gcur) == expected_ppm);
}

static void test_market_access_wedge_discounts_remote_producers() {
    // R3: state market access reaches settlement money. Two identical farms,
    // one in a fully integrated state and one at 25% access: the remote one
    // sells below market and buys inputs above it, so it keeps less cash.
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1000});
    const std::array<NeedFlow, 1> grain_need{{{grain, 8000}}};
    (void)definitions.add_need_profile("workers", grain_need);
    const std::array<RecipeFlow, 0> no_inputs{};
    const std::array<RecipeFlow, 1> grain_out{{{grain, 12'000}}};
    const auto farm = definitions.add_building_type("farm", 1000, no_inputs, grain_out);

    World world;
    world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0});
    world.markets.resize(2, definitions);
    world.markets.set_owner(MarketId{0}, CountryId{0});
    world.markets.set_owner(MarketId{1}, CountryId{0});
    const auto state_near = world.geography.create_state({"near", CountryId{0}, MarketId{0}, ProvinceId{0}});
    const auto state_far = world.geography.create_state({"far", CountryId{0}, MarketId{1}, ProvinceId{1}});
    const auto prov_near = world.geography.create_province({"p_near", state_near, CountryId{0}, MarketId{0}, 0.0, 0.0, 100u});
    const auto prov_far = world.geography.create_province({"p_far", state_far, CountryId{0}, MarketId{1}, 10.0, 10.0, 100u});
    world.geography.set_state_market_access_ppm(state_far, 250'000u); // 25% access
    const auto near = world.buildings.create({MarketId{0}, farm, 1u, 1000, 2'000'000, prov_near});
    const auto far = world.buildings.create({MarketId{1}, farm, 1u, 1000, 2'000'000, prov_far});
    world.pops.create({MarketId{0}, 1000u, near, NeedProfileId{0u}});
    world.pops.create({MarketId{1}, 1000u, far, NeedProfileId{0u}});

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    economy.run_weekly(world, jobs);

    // Same labour, same output, same market prices — but the remote farm
    // settled its sales at a 18.75% discount ((1-0.25)/4 wedge).
    assert(world.buildings.cash(far) < world.buildings.cash(near));
}

static void test_blockade_throttles_cross_border_trade() {
    // R4: a blockaded sea zone closes its controller's ports for cross-border
    // legs. 50% blockade on the exporter's zone halves the shipped volume.
    auto run_case = [](bool blockaded) {
        EconomyDefinitions definitions;
        const auto grain = definitions.add_good({"grain", 1000});
        const std::array<NeedFlow, 0> no_needs{};
        (void)definitions.add_need_profile("workers", no_needs);
        World world;
        world.countries.create({"EXP", 0.0, 0.0, 0.0, 0.0});
        world.countries.create({"IMP", 0.0, 0.0, 0.0, 0.0});
        world.markets.resize(2, definitions);
        world.markets.set_owner(MarketId{0}, CountryId{0});
        world.markets.set_owner(MarketId{1}, CountryId{1});
        world.markets.inventory_row(MarketId{0})[grain.value()] = 100'000;
        world.markets.price_row(MarketId{0})[grain.value()] = 500;
        world.markets.shortage_row(MarketId{1})[grain.value()] = 100'000;
        world.markets.price_row(MarketId{1})[grain.value()] = 3'000;
        world.grand_strategy.add_trade_route({MarketId{0}, MarketId{1}, grain, 4'000, 1, true});
        world.trade_policies.resize(2);
        if (blockaded) {
            const auto zone = world.grand_strategy.add_sea_zone({0x424C4B44u, CountryId{0}, 500'000u});
            (void)zone;
        }
        EconomySystem economy{definitions};
        economy.rebuild_indices(world);
        JobSystem jobs{0u};
        economy.run_weekly(world, jobs);
        return world.markets.inventory_row(MarketId{1})[grain.value()];
    };
    const auto free_flow = run_case(false);
    const auto blocked_flow = run_case(true);
    assert(free_flow == 4'000);
    assert(blocked_flow == 2'000); // (1 - 50%) of the route volume
}

static void test_construction_consumes_materials() {
    // R5: the construction basket draws real goods from the project's market
    // and pays sellers into clearing. Materials, not just money, gate progress.
    EconomyDefinitions definitions;
    const auto iron = definitions.add_good({"iron", 2000});
    const std::array<NeedFlow, 0> no_needs{};
    (void)definitions.add_need_profile("workers", no_needs);
    const std::array<RecipeFlow, 0> no_inputs{};
    const std::array<RecipeFlow, 1> iron_out{{{iron, 1'000}}};
    const auto mine = definitions.add_building_type("iron_mine", 1000, no_inputs, iron_out);
    const std::array<RecipeFlow, 1> basket{{{iron, 100}}}; // 100 milli-units per point
    definitions.set_construction_inputs(basket);

    World world;
    world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, CountryId{0});
    const auto building = world.buildings.create({MarketId{0}, mine, 1u, 1000, 1'000'000});
    world.pops.create({MarketId{0}, 100u, building, NeedProfileId{0u}});
    world.countries.set_treasury(CountryId{0}, 500.0); // funds the money leg
    world.markets.inventory_row(MarketId{0})[iron.value()] = 50'000;
    const auto project_id = world.construction.enqueue_expansion(CountryId{0}, building, 100u, 10'000);

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    economy.run_weekly(world, jobs);

    const auto* project = world.construction.find(project_id);
    assert(project != nullptr && project->progress_points > 0u); // advanced
    // Materials left the market stock and sellers were paid into clearing.
    assert(world.markets.inventory_row(MarketId{0})[iron.value()] < 50'000);
    assert(world.markets.clearing_cash(MarketId{0}) > 0);
}

static void test_investment_pool_expansion_queues_project() {
    // R6: pool-funded expansion enters the construction queue instead of
    // appearing instantly — capital formation takes time.
    auto f = make_fixture(1u, 1u, 1u, 1000u);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    // Profitable, fully utilized building + flush pool: the old code would
    // level up immediately; now it must enqueue exactly one project.
    f.world.buildings.set_last_profit(BuildingId{0u}, 1'000'000);
    f.world.grand_strategy.add_investment_pool_funds(CountryId{0u}, 100'000'000);
    JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);
    assert(f.world.buildings.level(BuildingId{0u}) == 1u); // not instant
    assert(f.world.construction.size() == 1u);             // queued instead
    const auto& project = f.world.construction.projects()[0];
    assert(project.kind == ConstructionKind::ExpandBuilding);
    assert(project.target_building == BuildingId{0u});
    assert(project.total_cost_milli > 0); // the bill travels with the queue
}
static void test_sovereign_restructuring_writes_down_debt() {
    // R8: a month of missed debt service triggers a 30% write-down. Domestic
    // banks absorb their pro-rata share through equity (the bank-crisis
    // channel); the saver pool absorbs the rest. The exclusion clock resets
    // to a year of market exile.
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1000});
    const std::array<NeedFlow, 0> no_needs{};
    (void)definitions.add_need_profile("workers", no_needs);

    World world;
    const auto country = world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0}); // zero taxes: no income
    world.countries.set_gdp(country, 1'000'000.0); // deep annual GDP: AAA ceiling
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, country);
    const auto bank = world.banks.create({bank_stable_key("bank.test.restructure"), country,
        default_currency_key, 2'000'000, 0, 100'000, 80'000, 0, 50'000});
    assert(world.banks.balance_sheet_balanced(bank));

    const auto borrowed = world.countries.issue_sovereign_bonds(country, 1'000'000, world);
    assert(borrowed == 1'000'000);
    assert(world.banks.bank(bank).sovereign_bonds_milli == 1'000'000);
    world.countries.set_treasury(country, 0.0); // insolvent from here on

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    for (int w = 0; w < 4; ++w) economy.run_weekly(world, jobs);

    // 30% haircut applied on the fourth missed week.
    assert(world.countries.national_debt_milli(country) == 700'000);
    assert(world.banks.bank(bank).sovereign_bonds_milli == 700'000);
    assert(world.banks.validate(world.countries.size(), world.buildings.size()));
    assert(world.banks.balance_sheet_balanced(bank));
    assert(world.countries.default_weeks(country) == 52); // a year of exile
    assert(world.countries.credit_rating(country) == CreditRating::D);
}

static void test_bankrupt_building_downsizes_and_dies_on_debt_failure() {
    // R10: a building pinned at the credit limit while operating at a loss
    // shrinks one level per week. A one-level building dies only when its
    // bank debt has demonstrably failed (non-performing): mere credit use
    // never liquidates a young business.
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1000});
    const std::array<NeedFlow, 0> no_needs{};
    (void)definitions.add_need_profile("workers", no_needs);
    const std::array<RecipeFlow, 0> no_inputs{};
    const std::array<RecipeFlow, 1> grain_out{{{grain, 1}}}; // token output
    const auto big_type = definitions.add_building_type("estate", 1000, no_inputs, grain_out);

    World world;
    world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, CountryId{0});
    // Level-3 estate with no demand and wage obligations: bleeds into the
    // credit limit and must shed a level per loss week.
    const auto estate = world.buildings.create({MarketId{0}, big_type, 3u, 1000, 100'000});
    world.pops.create({MarketId{0}, 2500u, estate, NeedProfileId{0u}});

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    economy.run_weekly(world, jobs);
    assert(world.buildings.level(estate) == 2u); // downsized, not destroyed
    assert(world.buildings.slot_pool().is_index_alive(estate.value()));

    // Terminal case: a one-level shop whose bank loan went non-performing
    // (four missed weekly payments) dies once it is also at the limit.
    const auto shop = world.buildings.create({MarketId{0}, big_type, 1u, 1000, 0});
    const auto bank = world.banks.create({bank_stable_key("bank.test.terminal"), CountryId{0},
        default_currency_key, 1'000'000, 500'000, 100'000, 80'000, 10'000, 52'000});
    assert(world.banks.fund_building(bank, shop, 20'000, 52'000, 52) == 20'000);
    world.buildings.cash_mut()[shop.value()] = 0;
    for (int w = 0; w < 4; ++w) world.banks.run_weekly(world); // -> NonPerforming
    assert(world.banks.loan(BankLoanId{0u}).status == BankLoanStatus::NonPerforming);
    world.pops.create({MarketId{0}, 500u, shop, NeedProfileId{0u}});
    economy.rebuild_indices(world);
    economy.run_weekly(world, jobs);
    assert(!world.buildings.slot_pool().is_index_alive(shop.value())); // dead
}

static void test_company_receives_ownership_dividends() {
    // R11: ownership stakes divert the owned share of dividends into company
    // operating cash; the remainder still reaches the national pool.
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1000});
    const std::array<NeedFlow, 0> no_needs{};
    (void)definitions.add_need_profile("workers", no_needs);
    const std::array<RecipeFlow, 0> no_inputs{};
    const std::array<RecipeFlow, 1> grain_out{{{grain, 1}}};
    const auto farm = definitions.add_building_type("farm", 1000, no_inputs, grain_out);

    World world;
    const auto country = world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, country);
    // Flush cash: reserve cap is 4 weeks of wages (4M); half the excess pays out.
    const auto building = world.buildings.create({MarketId{0}, farm, 1u, 1000, 10'000'000});
    const auto company = world.grand_strategy.add_company({country, 0xC0FFEEu, 0, 1'000'000, true});
    (void)world.grand_strategy.add_ownership_stake({country, company, building, 500'000u}); // 50%

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    economy.run_weekly(world, jobs);

    // Dividend = (10M - 4M cap) / 2 = 3M, no dividend tax in this fixture.
    // Company takes 50% = 1.5M; the pool receives the other 1.5M.
    assert(world.grand_strategy.company_cash(company) == 1'500'000);
    assert(world.grand_strategy.investment_pool_cash(country) == 1'500'000);
}

static void test_inventory_carry_cost_is_audited_sink() {
    // R12: idle stock is financed — a settled inventory bleeds 0.1%/week into
    // the clearing sink, and the monetary audit names the leg exactly.
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1000});
    const std::array<NeedFlow, 0> no_needs{};
    (void)definitions.add_need_profile("workers", no_needs);

    World world;
    world.countries.create({"TST", 0.0, 0.0, 0.0, 0.0});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, CountryId{0});
    world.markets.inventory_row(MarketId{0})[grain.value()] = 9'000'000;

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{0u};
    EconomyTickProfile profile;
    economy.run_weekly(world, jobs, &profile);

    // No demand: 4000 survives the storage cap (4x liquidity floor of 1000),
    // the rest spoils. Carry is booked on the SETTLED (post-clearing) price:
    // expected = kept_units x settled_price / 1e6. The glut also crushes the
    // price toward its floor in the same phase.
    const auto kept_inventory = world.markets.inventory(MarketId{0}, grain);
    const auto settled_price = world.markets.price(MarketId{0}, grain);
    assert(kept_inventory == 4'000);
    const EconomyAmount expected_carry = kept_inventory * settled_price / 1'000'000;
    assert(expected_carry > 0);
    assert(profile.inventory_carry_cost_milli == expected_carry);
    assert(profile.monetary_delta_milli == -expected_carry);
    assert(profile.unexplained_monetary_delta_milli == 0);
}

static void test_money_closed_loop_conservation() {
    auto f = make_fixture(2u, 6u, 40u, 100u);
    // Give initial POPs some cash
    for (std::size_t i = 0; i < f.world.pops.size(); ++i) {
        f.world.pops.set_cash(PopId{static_cast<PopId::rep_type>(i)}, 5'000);
    }
    // Set custom multi-tax policy on country 0
    TaxPolicy policy;
    policy.income_tax_ppm = 150'000;       // 15% income tax
    policy.consumption_tax_ppm = 50'000;   // 5% consumption tax
    policy.per_capita_tax_ppm = 10;        // 10 per capita
    f.world.countries.set_tax_policy(CountryId{0u}, policy);

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    const double initial_treasury = f.world.countries.treasury(CountryId{0u});
    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);
    // Closed loop with one explicit sink: inventory carrying cost bleeds idle
    // stock against market clearing. The audit identity must hold exactly —
    // the delta equals the explained carry leg, nothing unexplained.
    assert(profile.unexplained_monetary_delta_milli == 0);
    assert(profile.monetary_delta_milli == -profile.inventory_carry_cost_milli);
    assert(profile.inventory_carry_cost_milli >= 0);

    // Verify treasury increased from taxes deducted from POPs
    const double final_treasury = f.world.countries.treasury(CountryId{0u});
    assert(final_treasury >= initial_treasury);

    // Verify building cash changed and respected credit limit. Destroyed
    // (bankrupt) buildings are dead slots: skip them via the slot pool.
    for (std::size_t i = 0; i < f.world.buildings.size(); ++i) {
        if (!f.world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        assert(f.world.buildings.cash(BuildingId{static_cast<BuildingId::rep_type>(i)}) >= building_credit_limit_milli);
    }

    // Run for 12 weeks to verify stability
    for (int w = 0; w < 12; ++w) {
        economy.run_weekly(f.world, jobs, &profile);
        assert(profile.unexplained_monetary_delta_milli == 0);
        assert(profile.monetary_delta_milli == -profile.inventory_carry_cost_milli);
    }
    assert(f.world.countries.treasury(CountryId{0u}) >= final_treasury);
}

static void test_currency_store_basics() {
    CurrencyStore store;
    const CurrencyKey gbp = economy_stable_key("currency.gbp");
    const CurrencyKey usd = economy_stable_key("currency.usd");
    const CurrencyKey gold = economy_stable_key("currency.gold");
    const CurrencyKey bimetal = economy_stable_key("currency.bimetal");

    store.register_currency(gbp, "British Pound", MonetaryStandard::GoldStandard, 7322, 113491, 1'000'000);
    store.register_currency(usd, "US Dollar", MonetaryStandard::FiatFloating, 1000, 15500, 500'000);
    store.register_currency(gold, "Gold Franc", MonetaryStandard::GoldStandard, 290, 4500, 2'000'000);
    store.register_currency(bimetal, "Latin Union Franc", MonetaryStandard::Bimetallism, 290, 4500, 1'000'000);

    assert(store.contains(gbp));
    assert(store.contains(usd));
    assert(store.contains(gold));
    assert(store.contains(bimetal));
    assert(store.monetary_standard(gbp) == MonetaryStandard::GoldStandard);
    assert(store.monetary_standard(usd) == MonetaryStandard::FiatFloating);
    assert(store.monetary_standard(bimetal) == MonetaryStandard::Bimetallism);
    assert(store.exchange_rate_ppm(gbp) == 1'000'000);
    assert(store.exchange_rate_ppm(usd) == 500'000);
    assert(store.validate(0u));
    store.set_monetary_standard(usd, static_cast<MonetaryStandard>(255u));
    assert(!store.validate(0u));
    store.set_monetary_standard(usd, MonetaryStandard::FiatFloating);
    assert(store.validate(0u));

    // 100 GBP @ 1.0 = 200 USD @ 0.5
    assert(store.convert(100'000, gbp, usd) == 200'000);
    // 200 USD @ 0.5 = 100 GBP @ 1.0
    assert(store.convert(200'000, usd, gbp) == 100'000);
    // Saturating conversion must also handle the most negative signed amount
    // without negating INT64_MIN in the signed domain.
    assert(store.convert(std::numeric_limits<EconomyAmount>::min(), gbp, usd) ==
           std::numeric_limits<EconomyAmount>::min());

    // Price conversions
    assert(store.convert_price(1000, gbp, usd) == 2000);
    assert(store.convert_price(2000, usd, gbp) == 1000);
}

static void test_metal_currency_class() {
    // Precious metals are first-class currencies (CurrencyClass::Metal) that act
    // as the FX anchor. Class is derived from the canonical metal keys.
    CurrencyStore store;
    assert(store.is_metal_currency(metal_gold_key));
    assert(store.is_metal_currency(metal_silver_key));
    assert(!store.is_metal_currency(default_currency_key));
    assert(store.currency_class(metal_gold_key) == CurrencyClass::Metal);
    assert(store.currency_class(default_currency_key) == CurrencyClass::National);

    // Both metal numeraires are registered at bootstrap and validate cleanly
    // alongside any national currency.
    assert(store.contains(metal_gold_key));
    assert(store.contains(metal_silver_key));
    assert(store.validate(0u));

    // Gold numeraire starts at the reference metal price (1000 mg gold = 1.0 base).
    assert(store.exchange_rate_ppm(metal_gold_key) == 1'000'000);
    assert(store.exchange_rate_ppm(metal_silver_key) == 64'516);

    // Metal currencies are convertible like any other currency: 1'000'000 base
    // milli at rate 1'000'000 == 1'000'000 gold milli (1:1 numeraire).
    assert(store.convert(1'000'000, default_currency_key, metal_gold_key) == 1'000'000);

    // The supplied metal prices pin the numeraires and drive mint parity. After a
    // tick at gold=2'000'000, the gold numeraire tracks it and a gold-standard
    // national currency's target rate scales with gold_ref.
    const CurrencyKey franc = economy_stable_key("currency.franc");
    store.register_currency(franc, "Gold Franc", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    store.update_exchange_rates(2'000'000, 129'032, nullptr);
    assert(store.exchange_rate_ppm(metal_gold_key) == 2'000'000);
    // 1000 mg gold parity -> target = 2'000'000 * 1000 / 1000 = 2'000'000
    assert(store.target_rate_ppm(franc) == 2'000'000);
    assert(store.currency_class(metal_gold_key) == CurrencyClass::Metal);
}

static void test_cross_currency_trade_and_fx_market() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey gbp = economy_stable_key("currency.gbp");
    const CurrencyKey usd = economy_stable_key("currency.usd");

    f.world.currencies.register_currency(gbp, "British Pound", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    f.world.currencies.register_currency(usd, "US Dollar", MonetaryStandard::FiatFloating, 1000, 15500, 1'000'000);

    const MarketId m0{0u};
    const MarketId m1{1u};
    const CountryId c0{0u};
    const CountryId c1{1u};

    f.world.markets.set_currency_key(m0, gbp);
    f.world.markets.set_currency_key(m1, usd);
    f.world.countries.set_primary_currency(c0, gbp);
    f.world.countries.set_primary_currency(c1, usd);
    f.world.countries.set_prestige(c0, 100.0);
    f.world.countries.set_prestige(c1, 50.0);

    // Give POPs initial cash
    for (std::size_t i = 0; i < f.world.pops.size(); ++i) {
        f.world.pops.set_cash(PopId{static_cast<PopId::rep_type>(i)}, 10'000);
    }

    // Set Market 0 to have excess grain inventory and Market 1 to have high grain shortage
    f.world.markets.inventory_row(m0)[f.grain.value()] = 50'000;
    f.world.markets.price_row(m0)[f.grain.value()] = 500;  // Cheap in m0

    f.world.markets.shortage_row(m1)[f.grain.value()] = 30'000;
    f.world.markets.price_row(m1)[f.grain.value()] = 2000; // Expensive in m1

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    const auto initial_gbp_rate = f.world.currencies.exchange_rate_ppm(gbp);
    const auto initial_c0_reserves = f.world.countries.foreign_reserves_milli(c0);

    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);

    // Cross-currency trade should have shipped goods from m0 to m1
    assert(f.world.markets.inventory_row(m0)[f.grain.value()] < 50'000);
    assert(f.world.markets.inventory_row(m1)[f.grain.value()] > 0);

    // Exporter market m0 and country c0 received revenue and accumulated foreign exchange
    assert(f.world.countries.foreign_reserves_milli(c0) > initial_c0_reserves);
    assert(f.world.countries.balance_of_payments_milli(c0) > 0);
    assert(f.world.countries.balance_of_payments_milli(c1) < 0);

    // Exporter currency (GBP) experienced trade surplus demand and appreciated
    assert(f.world.currencies.exchange_rate_ppm(gbp) >= initial_gbp_rate);
    assert(std::abs(profile.unexplained_monetary_delta_milli) <= 4);
}

static void test_monetary_sovereignty_and_seigniorage() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey union_cur = economy_stable_key("currency.sterling_zone");

    f.world.currencies.register_currency(union_cur, "Sterling Area Pound", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);

    const CountryId gbr{0u};
    const CountryId aus{1u};

    // Both countries in the same currency zone
    f.world.countries.set_primary_currency(gbr, union_cur);
    f.world.countries.set_primary_currency(aus, union_cur);

    // GBR has higher prestige (hegemon)
    f.world.countries.set_prestige(gbr, 150.0);
    f.world.countries.set_gdp(gbr, 5000.0);
    f.world.countries.set_prestige(aus, 20.0);
    f.world.countries.set_gdp(aus, 500.0);

    f.world.currencies.evaluate_monetary_sovereignty(f.world.countries);
    assert(f.world.currencies.sovereign_leader(union_cur) == gbr);

    // Simulate turnover generating seigniorage
    const CurrencyKey foreign_cur = economy_stable_key("currency.foreign");
    f.world.currencies.register_currency(foreign_cur, "Foreign Franc", MonetaryStandard::FiatFloating, 1000, 15500, 1'000'000);
    f.world.currencies.record_fx_flow(foreign_cur, union_cur, 10'000'000); // 10k currency units

    assert(f.world.currencies.seigniorage_accrued_milli(union_cur) > 0);

    // Run weekly tick to distribute seigniorage to sovereign leader
    const double gbr_treasury_before = f.world.countries.treasury(gbr);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);

    // GBR received seigniorage
    assert(f.world.countries.treasury(gbr) > gbr_treasury_before);

    // Dynamic shift of monetary sovereignty: AUS prestige and power surges past GBR
    f.world.countries.set_prestige(aus, 300.0);
    f.world.countries.set_gdp(aus, 10000.0);
    f.world.currencies.evaluate_monetary_sovereignty(f.world.countries);
    assert(f.world.currencies.sovereign_leader(union_cur) == aus);

    // Leadership is derived and must be cleared when the last member leaves
    // a currency zone; zero is reserved as an invalid stable key.
    f.world.countries.set_primary_currency(gbr, default_currency_key);
    f.world.countries.set_primary_currency(aus, default_currency_key);
    f.world.currencies.evaluate_monetary_sovereignty(f.world.countries);
    assert(!f.world.currencies.sovereign_leader(union_cur).valid());
    const auto currency_count = f.world.currencies.size();
    f.world.currencies.set_exchange_rate_ppm(0u, 123'000);
    f.world.currencies.set_target_rate_ppm(0u, 123'000);
    f.world.currencies.set_convertibility_suspended(0u, true);
    assert(f.world.currencies.size() == currency_count);
}

static void test_bimetallism_and_greshams_law() {
    CurrencyStore store;
    const CurrencyKey bimetal = economy_stable_key("currency.bimetal");
    // Fine metal: 1000 mg Gold, 15500 mg Silver (15.5:1 ratio)
    store.register_currency(bimetal, "Bimetallic Franc", MonetaryStandard::Bimetallism, 1000, 15500, 1'000'000);

    // Initial equilibrium: Gold = 1'000'000 ppm, Silver = 64'516 ppm (15.5 ratio)
    store.update_exchange_rates(1'000'000, 64'516);
    assert(store.exchange_rate_ppm(bimetal) >= 980'000 && store.exchange_rate_ppm(bimetal) <= 1'020'000);

    // Market shift: Silver becomes much cheaper (silver market price plunges to 30'000 ppm)
    // Gresham's Law: Silver is overvalued at the mint -> Silver circulates as the cheaper metal
    store.update_exchange_rates(1'000'000, 30'000);
    // Target rate drops following the circulating silver value (15.5 * 30'000 = 465'000)
    assert(store.exchange_rate_ppm(bimetal) < 900'000);
}

static void test_gdp_real_numeraire_and_domestic_wages() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey gold_cur = economy_stable_key("currency.gold_standard");
    const CurrencyKey devalued_cur = economy_stable_key("currency.devalued");

    // Gold currency: rate = 1.0 (1'000'000 ppm)
    // Devalued currency: rate = 0.5 (500'000 ppm)
    f.world.currencies.register_currency(gold_cur, "Gold Mark", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    f.world.currencies.register_currency(devalued_cur, "Paper Lira", MonetaryStandard::FiatFloating, 1000, 15500, 500'000);

    const MarketId m0{0u};
    const MarketId m1{1u};
    const CountryId c0{0u};
    const CountryId c1{1u};

    f.world.markets.set_currency_key(m0, gold_cur);
    f.world.markets.set_currency_key(m1, devalued_cur);
    f.world.countries.set_primary_currency(c0, gold_cur);
    f.world.countries.set_primary_currency(c1, devalued_cur);

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    economy.run_weekly(f.world, jobs);

    // Verify Real Global GDP vs Nominal Local GDP:
    assert(f.world.countries.nominal_gdp_milli(c1) > 0);
    assert(f.world.countries.gdp(c1) > 0.0);
    // Real GDP is normalized via exchange rate convert to numeraire
    const double expected_real_gdp = static_cast<double>(
        f.world.currencies.convert(f.world.countries.nominal_gdp_milli(c1), devalued_cur, default_currency_key)) / 1000.0;
    assert(std::abs(f.world.countries.gdp(c1) - expected_real_gdp) < 1.0);
}

static void test_gold_points_specie_arbitrage_and_drain() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey gbp = economy_stable_key("currency.gbp");
    const CurrencyKey usd = economy_stable_key("currency.usd");

    // GBP on Gold Standard with 1000 mg fine gold per unit
    f.world.currencies.register_currency(gbp, "Pound Sterling", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    f.world.currencies.register_currency(usd, "US Dollar", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);

    const CountryId gbr{0u};
    f.world.countries.set_primary_currency(gbr, gbp);
    f.world.countries.set_foreign_reserves_milli(gbr, 50'000); // 50 units gold reserves
    f.world.currencies.set_sovereign_leader(gbp, gbr, 100.0);

    // Continuous trade deficit: demand for USD (selling GBP) pushes GBP exchange rate down
    f.world.currencies.record_fx_flow(gbp, usd, 20'000); // 20 units deficit
    f.world.currencies.update_exchange_rates(1'000'000, 64'516, &f.world.countries);

    // 1. Specie Arbitrage triggered: rate is pegged at the Gold Export Point (980,000 ppm)
    assert(f.world.currencies.exchange_rate_ppm(gbp) >= 980'000);
    // Gold was physically shipped!
    assert(f.world.currencies.specie_export_mg(gbp) > 0);
    // Central reserves were debited
    assert(f.world.countries.foreign_reserves_milli(gbr) < 50'000);
    assert(!f.world.currencies.convertibility_suspended(gbp));

    // 2. Heavy drain that exhausts remaining reserves
    f.world.countries.set_foreign_reserves_milli(gbr, 0); // Out of gold!
    f.world.currencies.record_fx_flow(gbp, usd, 100'000);
    f.world.currencies.update_exchange_rates(1'000'000, 64'516, &f.world.countries);

    // Specie drained -> Forced suspension of gold convertibility!
    assert(f.world.currencies.convertibility_suspended(gbp));
}

static void test_sovereign_debt_issuance_credit_rating_and_default() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CountryId c0{0u};
    f.world.countries.set_gdp(c0, 1000.0); // Real GDP = 1000
    f.world.countries.set_treasury(c0, 100.0); // Treasury = 100
    const auto bank = f.world.banks.create({bank_stable_key("bank.test.central"), c0,
        default_currency_key, 1'000'000, 0, 100'000, 80'000, 0, 50'000});
    assert(f.world.banks.balance_sheet_balanced(bank));

    // Initial state: AAA rating, 3.0% yield, 0 debt
    f.world.countries.evaluate_credit_rating(c0);
    assert(f.world.countries.credit_rating(c0) == CreditRating::AAA);
    assert(f.world.countries.bond_yield_ppm(c0) == 25'000 || f.world.countries.bond_yield_ppm(c0) == 30'000);

    // 1. Issue sovereign bonds: borrow 200,000 milli (200 units)
    const auto borrowed = f.world.countries.issue_sovereign_bonds(c0, 200'000, f.world);
    assert(borrowed == 200'000);
    assert(f.world.countries.national_debt_milli(c0) == 200'000);
    // Treasury received cash
    assert(f.world.countries.treasury_milli(c0) >= 300'000);

    // 2. Over-borrowing degrades credit rating. gdp(1000.0) is the WEEKLY
    // value-added flow; ratings compare the debt stock against ANNUAL GDP
    // (1000 x 52 = 52'000 units = 52'000'000 milli). 180% of annual GDP sits
    // in the BB band (150%-200%).
    f.world.countries.set_national_debt_milli(c0, 93'600'000); // 180% of annual GDP
    f.world.countries.evaluate_credit_rating(c0);
    assert(f.world.countries.credit_rating(c0) == CreditRating::BB);
    assert(f.world.countries.bond_yield_ppm(c0) >= 80'000); // Higher risk yield

    // 3. Weekly interest deduction in economy settlement
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    economy.run_weekly(f.world, jobs);
    // Treasury was charged weekly debt service interest
    assert(f.world.countries.weekly_debt_service_milli(c0) > 0);

    // 4. Repay debt: use surplus treasury to reduce debt
    const auto debt_before = f.world.countries.national_debt_milli(c0);
    const auto repaid = f.world.countries.repay_sovereign_debt(c0, 1'000'000, f.world);
    assert(repaid > 0);
    assert(f.world.countries.national_debt_milli(c0) < debt_before);
}

static void test_bank_balance_sheet_credit_service_and_default() {
    auto f = make_fixture(1u, 1u, 1u, 100u);
    const CountryId country{0u};
    const BuildingId building{0u};
    const auto bank = f.world.banks.create({bank_stable_key("bank.test.commercial"), country,
        default_currency_key, 1'000'000, 500'000, 100'000, 80'000, 10'000, 52'000});
    const auto before = f.world.banks.bank(bank);
    const auto funded = f.world.banks.fund_building(bank, building, 100'000, 52'000, 52);
    assert(funded == 100'000);
    f.world.buildings.cash_mut()[building.value()] = saturating_add(
        f.world.buildings.cash(building), funded);
    const auto originated = f.world.banks.bank(bank);
    assert(originated.loan_assets_milli == before.loan_assets_milli + funded);
    assert(originated.deposits_milli == before.deposits_milli + funded);
    assert(f.world.banks.balance_sheet_balanced(bank));
    assert(f.world.banks.validate(f.world.countries.size(), f.world.buildings.size()));

    const auto cash_before = f.world.buildings.cash(building);
    f.world.banks.run_weekly(f.world);
    assert(f.world.buildings.cash(building) < cash_before);
    assert(f.world.banks.loan(BankLoanId{0u}).principal_milli < funded);
    assert(f.world.banks.balance_sheet_balanced(bank));

    // A second borrower with no cash becomes non-performing and is charged
    // against bank equity after twelve deterministic missed payments.
    const auto second = f.world.buildings.create({MarketId{0u}, f.building_types[0], 1u, 1000, 0});
    const auto second_funded = f.world.banks.fund_building(bank, second, 50'000, 52'000, 52);
    assert(second_funded == 50'000);
    for (int week = 0; week < 12; ++week) f.world.banks.run_weekly(f.world);
    assert(f.world.banks.loan(BankLoanId{1u}).status == BankLoanStatus::Defaulted);
    assert(f.world.banks.balance_sheet_balanced(bank));
    assert(f.world.banks.validate(f.world.countries.size(), f.world.buildings.size()));

    // Closing a contract must not make a stable bank/building key unusable
    // forever.  The next draw reuses the same authoritative loan row.
    const auto recycled = f.world.banks.fund_building(bank, second, 10'000, 52'000, 52);
    assert(recycled == 10'000);
    assert(f.world.banks.loan_count() == 2u);
    assert(f.world.banks.loan(BankLoanId{1u}).status == BankLoanStatus::Performing);
    assert(f.world.banks.loan(BankLoanId{1u}).principal_milli == 10'000);
    assert(f.world.banks.validate(f.world.countries.size(), f.world.buildings.size()));

    bool rejected_closed_loan = false;
    try {
        (void)f.world.banks.restore_loan({bank_stable_key("invalid.closed.loan"), bank,
                                          BankBorrowerKind::Building, building.value(), 1,
                                          50'000, 52, 0, BankLoanStatus::Repaid});
    } catch (const std::invalid_argument&) {
        rejected_closed_loan = true;
    }
    assert(rejected_closed_loan);

    auto scripts = ScriptRegistry::make_builtin();
    const auto scope = ScopeRef::country(country);
    assert(scripts.evaluate_trigger("bank_reserves_above", f.world, scope, 100.0));
    scripts.execute_effect("set_import_tariff", f.world, scope, 0.15);
    scripts.execute_effect("set_export_tariff", f.world, scope, 0.05);
    scripts.execute_effect("set_trade_logistics_capacity", f.world, scope, 250.0);
    assert(f.world.trade_policies.get(country).import_tariff_ppm == 150'000);
    assert(f.world.trade_policies.get(country).export_tariff_ppm == 50'000);
    assert(f.world.trade_policies.get(country).logistics_capacity_milli == 250'000);
}

static void test_nonperforming_credit_draws_keep_npl_ledger_balanced() {
    auto f = make_fixture(1u, 1u, 1u, 100u);
    const CountryId country{0u};
    const BuildingId building{0u};
    const auto bank = f.world.banks.create({bank_stable_key("bank.test.npl"), country,
        default_currency_key, 1'000'000, 500'000, 100'000, 80'000, 10'000, 52'000});

    // Force the first contract into arrears.  A follow-up draw on an NPL
    // contract must increase both the loan principal and the NPL bucket by
    // exactly the same amount.
    const auto first = f.world.banks.fund_building(bank, building, 20'000, 52'000, 52);
    assert(first == 20'000);
    f.world.buildings.cash_mut()[building.value()] = 0;
    for (int week = 0; week < 4; ++week) f.world.banks.run_weekly(f.world);

    const auto npl_loan = f.world.banks.loan(BankLoanId{0u});
    const auto before = f.world.banks.bank(bank);
    assert(npl_loan.status == BankLoanStatus::NonPerforming);
    assert(before.nonperforming_milli == npl_loan.principal_milli);
    assert(before.nonperforming_milli == before.loan_assets_milli);
    assert(f.world.banks.validate(f.world.countries.size(), f.world.buildings.size()));

    const auto second = f.world.banks.fund_building(bank, building, 5'000, 52'000, 52);
    assert(second == 5'000);
    const auto after = f.world.banks.bank(bank);
    const auto after_loan = f.world.banks.loan(BankLoanId{0u});
    assert(after_loan.status == BankLoanStatus::NonPerforming);
    assert(after.loan_assets_milli == before.loan_assets_milli + second);
    assert(after.nonperforming_milli == before.nonperforming_milli + second);
    assert(after_loan.principal_milli == npl_loan.principal_milli + second);
    assert(after.nonperforming_milli == after.loan_assets_milli);
    assert(f.world.banks.validate(f.world.countries.size(), f.world.buildings.size()));
}

static void test_authored_trade_route_tariff_and_logistics_capacity() {
    auto f = make_fixture(2u, 1u, 1u, 100u);
    const MarketId exporter{0u}, importer{1u};
    const CountryId export_country{0u}, import_country{1u};
    f.world.markets.inventory_row(exporter)[f.grain.value()] = 50'000;
    f.world.markets.shortage_row(importer)[f.grain.value()] = 50'000;
    f.world.markets.price_row(exporter)[f.grain.value()] = 500;
    f.world.markets.price_row(importer)[f.grain.value()] = 3'000;
    f.world.grand_strategy.add_trade_route({exporter, importer, f.grain, 4'000, 1, true});
    f.world.trade_policies.resize(2);
    f.world.trade_policies.set(import_country, {100'000, 0, 3'000});
    f.world.trade_policies.set(export_country, {0, 50'000, 10'000});
    const auto importer_treasury = f.world.countries.treasury_milli(import_country);
    const auto exporter_treasury = f.world.countries.treasury_milli(export_country);

    EconomySystem economy{f.definitions}; economy.rebuild_indices(f.world); JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);
    assert(f.world.trade_policies.used_capacity(import_country) == 3'000);
    assert(f.world.trade_policies.used_capacity(export_country) == 3'000);
    assert(f.world.countries.treasury_milli(import_country) > importer_treasury);
    assert(f.world.countries.treasury_milli(export_country) > exporter_treasury);
}

static void test_construction_queue_and_pm_gradual_transition() {
    auto f = make_fixture(1u, 1u, 1u, 100u);
    const CountryId country{0u};
    const BuildingId building{0u};
    const ProductionMethodId initial_pm = f.world.buildings.production_method(building);

    // Register an advanced production method (e.g. Steam Weaving / Machinery)
    const thunder::RecipeFlow inputs[] = {{f.coal, 1000}};
    const thunder::RecipeFlow outputs[] = {{f.clothes, 3000}};
    const auto advanced_pm = f.definitions.add_production_method(
        "pm.advanced_steam_machinery", f.building_types[0], 100'000, inputs, outputs);

    f.world.countries.set_treasury(country, 500.0);
    f.world.countries.set_gdp(country, 1000.0);

    // 1. Enqueue PM Upgrade: initial PM is untouched, transition progress is 0%
    const auto proj_pm = f.world.construction.enqueue_pm_upgrade(country, building, advanced_pm, 50u, 5'000);
    assert(f.world.construction.size() == 1);
    assert(f.world.construction.pm_transition_progress_ppm(building) == 0);
    assert(f.world.buildings.production_method(building) == initial_pm);

    // 2. Enqueue Monument: Statue of Liberty
    const auto proj_monument = f.world.construction.enqueue_monument(country, ProvinceId{0u}, "monument.statue_of_liberty", 50u, 10'000);
    assert(f.world.construction.size() == 2);

    // Test priority reordering and pause
    assert(f.world.construction.move_down(proj_pm));
    assert(f.world.construction.move_up(proj_pm));
    assert(f.world.construction.set_paused(proj_monument, true));
    assert(f.world.construction.find(proj_monument)->paused);
    assert(f.world.construction.set_paused(proj_monument, false));

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    // 3. Weekly execution: Construction advances gradually
    economy.run_weekly(f.world, jobs);
    assert(f.world.construction.pm_transition_progress_ppm(building) > 0);

    // Run until completion (bounded: pool-funded auto-expansion may enqueue
    // follow-up projects for profitable buildings, so never loop forever).
    int completion_weeks = 0;
    while (f.world.construction.size() > 0 && completion_weeks < 200) {
        economy.run_weekly(f.world, jobs);
        ++completion_weeks;
    }
    assert(f.world.construction.size() == 0u);

    // 4. Verification: PM was successfully upgraded to advanced_pm after queue execution!
    assert(f.world.buildings.production_method(building) == advanced_pm);
    assert(f.world.construction.pm_transition_progress_ppm(building) == 1'000'000);
    // Monument provided national prestige
    assert(f.world.countries.prestige(country) >= 25.0);

    // Saturated building levels are a valid long-game boundary: completing an
    // expansion must not wrap the uint16 level back to zero.
    auto saturated = make_fixture(1u, 1u, 1u, 100u);
    saturated.world.buildings.set_level(
        BuildingId{0u}, std::numeric_limits<std::uint16_t>::max());
    saturated.world.countries.set_treasury(CountryId{0u}, 100.0);
    saturated.world.construction.enqueue_expansion(CountryId{0u}, BuildingId{0u}, 1u, 1);
    EconomySystem saturated_economy{saturated.definitions};
    saturated_economy.rebuild_indices(saturated.world);
    saturated_economy.run_weekly(saturated.world, jobs);
    assert(saturated.world.buildings.level(BuildingId{0u}) ==
           std::numeric_limits<std::uint16_t>::max());

    // A zero total cost is the documented default-cost sentinel. After a
    // partially funded week it must remain a valid queue record even though
    // paid_cost_milli is now non-zero.
    auto default_cost = make_fixture(1u, 1u, 1u, 100u);
    default_cost.world.countries.set_treasury(CountryId{0u}, 100.0);
    const auto default_project = default_cost.world.construction.enqueue_expansion(
        CountryId{0u}, BuildingId{0u}, 100u, 0);
    EconomySystem default_cost_economy{default_cost.definitions};
    default_cost_economy.rebuild_indices(default_cost.world);
    default_cost_economy.run_weekly(default_cost.world, jobs);
    const auto* default_record = default_cost.world.construction.find(default_project);
    assert(default_record != nullptr && default_record->paid_cost_milli > 0);
    assert(default_cost.world.construction.validate(default_cost.world));
}

void test_realistic_gradual_transitions_and_governance() {
    auto f = make_fixture(2u, 1u, 1u, 100u);
    const CountryId country{0u};
    const auto state = f.world.geography.create_state({"state.california", country, MarketId{0u}, ProvinceId{0u}, 750'000u});
    assert(f.world.geography.state_resistance_ppm(state) == 750'000u);

    // 1. Institution Budget Funded Directly from Treasury (no abstract points)
    f.world.countries.set_treasury(country, 50.0); // 50,000 milli
    const auto inst = f.world.grand_strategy.add_institution({country, 0x1234u, 2u});
    assert(f.world.grand_strategy.institutions()[inst.value()].level == 2u);

    const auto treasury_before = f.world.countries.treasury_milli(country);
    f.world.grand_strategy.run_institutions_weekly(f.world);
    const auto treasury_after = f.world.countries.treasury_milli(country);
    assert(treasury_after < treasury_before); // Institution maintenance deducted from Treasury

    // 2. Autonomous Trade Route Ramp-Up
    const MarketId market_a{0u};
    const MarketId market_b{1u};

    // Give market_a excess clothes, market_b shortage
    f.world.markets.inventory_row(market_a)[f.clothes.value()] = 50'000;
    f.world.markets.price_row(market_a)[f.clothes.value()] = 100;
    f.world.markets.shortage_row(market_b)[f.clothes.value()] = 50'000;
    f.world.markets.price_row(market_b)[f.clothes.value()] = 3'000;

    const auto route_id = f.world.grand_strategy.add_trade_route({market_a, market_b, f.clothes, 1'000, 1u, true});
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    const auto initial_qty = f.world.grand_strategy.trade_routes()[route_id.value()].quantity_milli;
    economy.run_weekly(f.world, jobs);
    const auto ramped_qty = f.world.grand_strategy.trade_routes()[route_id.value()].quantity_milli;
    assert(ramped_qty > initial_qty); // Market scaled volume adaptively!

    // 3. Pop Qualification Progression & Resistance Decay
    const auto pop_id = PopId{0u};
    const auto init_qual = f.world.pops.qualification_permyriad(pop_id);
    for (int week = 0; week < 20; ++week) {
        economy.run_weekly(f.world, jobs);
        f.world.grand_strategy.run_weekly_reference_tick(f.world);
    }
    const auto end_qual = f.world.pops.qualification_permyriad(pop_id);
    assert(end_qual > init_qual); // Continuous qualification accumulation

    // State resistance decays under good governance / high SoL
    const auto res_after = f.world.geography.state_resistance_ppm(state);
    assert(res_after < 750'000u); // Naturally decayed towards integration
}

static void test_raw_material_shortage_zero_throughput() {
    EconomyDefinitions definitions;
    const auto iron = definitions.add_good({"iron", 1000});
    const auto tools = definitions.add_good({"tools", 2000});
    const std::array<NeedFlow, 0> no_needs{};
    const auto worker_needs = definitions.add_need_profile("workers", no_needs);

    const std::array<RecipeFlow, 1> tools_in{{{iron, 1000}}};
    const std::array<RecipeFlow, 1> tools_out{{{tools, 1000}}};
    const auto toolworks_type = definitions.add_building_type("toolworks", 1000, tools_in, tools_out);

    World world;
    const auto country = world.countries.create({"TST", 1000.0, 100.0, 100.0, 0.2});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, country);
    const auto b = world.buildings.create({MarketId{0}, toolworks_type, 1u, 1000, 50'000});
    world.pops.create({MarketId{0}, 1000u, b, worker_needs});

    EconomySystem economy{definitions};
    economy.rebuild_indices(world);
    JobSystem jobs{1u};

    // Week 1: Market has 0 iron. Toolworks has workers, but 0 iron available!
    assert(world.markets.supply(MarketId{0}, iron) == 0);
    assert(world.markets.inventory(MarketId{0}, iron) == 0);

    economy.run_weekly(world, jobs);

    // Throughput MUST be 0, tools produced MUST be 0!
    assert(world.markets.supply(MarketId{0}, tools) == 0);

    // Now supply iron to the market inventory
    world.markets.inventory_row(MarketId{0})[iron.value()] = 50'000;

    economy.run_weekly(world, jobs);

    // With iron available, throughput recovers and tools are produced!
    assert(world.markets.supply(MarketId{0}, tools) > 0);
}

static void test_material_closed_loop_conservation() {
    // Material loop: goods are created only by production, removed only by
    // fulfilled demand (POP consumption + intermediate input draws), and the
    // only physical sink is the explicit storage-cap spoilage. Every other
    // unit must be accounted for, so the world inventory identity must hold
    // exactly across many weeks and across worker counts.
    auto f = make_fixture(3u, 8u, 120u, 100u);
    for (std::size_t i = 0; i < f.world.pops.size(); ++i) {
        f.world.pops.set_cash(PopId{static_cast<PopId::rep_type>(i)}, 5'000);
    }
    // Seed some starting inventory so the storage cap / spoilage path is exercised.
    for (std::size_t m = 0; m < f.world.markets.size(); ++m) {
        const MarketId market{static_cast<MarketId::rep_type>(m)};
        f.world.markets.inventory_row(market)[f.grain.value()] = 9'000'000;
        f.world.markets.inventory_row(market)[f.coal.value()] = 9'000'000;
    }

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);

    // Smoke + closure over a long horizon with real parallelism.
    {
        JobSystem jobs{4u};
        EconomyTickProfile profile;
        for (int w = 0; w < 24; ++w) {
            economy.run_weekly(f.world, jobs, &profile);
            // No physical stock may go negative: the loop neither mints nor
            // borrows goods from nothing.
            for (std::size_t m = 0; m < f.world.markets.size(); ++m) {
                const MarketId market{static_cast<MarketId::rep_type>(m)};
                for (std::size_t g = 0; g < f.definitions.good_count(); ++g) {
                    const GoodId good{static_cast<GoodId::rep_type>(g)};
                    assert(f.world.markets.inventory(market, good) >= 0);
                    assert(f.world.markets.shortage(market, good) >= 0);
                }
            }
            // World-level material conservation identity.
            assert(std::abs(profile.unexplained_material_delta_milli) <= 1);
            // Spoilage is the ONLY sink and is non-negative.
            assert(profile.spoilage_milli >= 0);
        }
    }

    // Determinism: material closure must hold identically across worker counts.
    World serial_world = f.world;
    World parallel_world = f.world;
    EconomySystem serial_economy{f.definitions};
    EconomySystem parallel_economy{f.definitions};
    serial_economy.rebuild_indices(serial_world);
    parallel_economy.rebuild_indices(parallel_world);
    JobSystem serial_jobs{0u};
    JobSystem parallel_jobs{4u};
    EconomyTickProfile serial_profile, parallel_profile;
    for (int w = 0; w < 12; ++w) {
        serial_economy.run_weekly(serial_world, serial_jobs, &serial_profile);
        parallel_economy.run_weekly(parallel_world, parallel_jobs, &parallel_profile);
        assert(std::abs(serial_profile.unexplained_material_delta_milli) <= 1);
        assert(std::abs(parallel_profile.unexplained_material_delta_milli) <= 1);
    }
    assert(serial_world.checksum() == parallel_world.checksum());
}

static void test_manual_convertibility_toggle() {
    CurrencyStore store;
    const CurrencyKey gold = economy_stable_key("currency.gold_toggle");
    const CurrencyKey fiat = economy_stable_key("currency.fiat_toggle");
    store.register_currency(gold, "Gold Toggle", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    store.register_currency(fiat, "Fiat Toggle", MonetaryStandard::FiatFloating, 1000, 15500, 1'000'000);

    // Gold standard currency: manual suspend then resume via the shared helper.
    assert(!store.convertibility_suspended(gold));
    assert(store.toggle_convertibility(gold));   // suspends
    assert(store.convertibility_suspended(gold));
    assert(store.toggle_convertibility(gold));   // resumes
    assert(!store.convertibility_suspended(gold));

    // Fiat currency has no specie to suspend: the manual toggle is a no-op, and
    // an unknown currency is likewise a no-op.
    assert(!store.convertibility_suspended(fiat));
    assert(!store.toggle_convertibility(fiat));
    assert(!store.convertibility_suspended(fiat));
    assert(!store.toggle_convertibility(economy_stable_key("currency.nonexistent")));
}

static void test_coin_commodity_value_milli() {
    // FX design: foreign money settles EITHER at the unit-of-account rate
    // (convert()) OR as commodity-valued coin (melt value = metal content x
    // market metal price). Both paths must agree at mint parity.
    CurrencyStore store;
    const auto gold = economy_stable_key("currency.test.goldcoin");
    const auto silver = economy_stable_key("currency.test.silvercoin");
    const auto bimetal = economy_stable_key("currency.test.bimetalcoin");
    const auto fiat = economy_stable_key("currency.test.paper");
    store.register_currency(gold, "GoldCoin", MonetaryStandard::GoldStandard, 1000, 15500);
    store.register_currency(silver, "SilverCoin", MonetaryStandard::SilverStandard, 1000, 15500);
    store.register_currency(bimetal, "BimetalCoin", MonetaryStandard::Bimetallism, 1000, 15500);
    store.register_currency(fiat, "Paper", MonetaryStandard::FiatFloating, 0, 0);

    // Standard baseline: 1000 mg gold and 15500 mg silver mint to the same
    // reference value at the default 15.5:1 market ratio.
    const EconomyPrice gold_price = 1'000'000;
    const EconomyPrice silver_price = 64'516; // 1'000'000 / 15.5
    assert(store.coin_commodity_value_milli(gold, gold_price, silver_price) == 1'000'000);
    const auto silver_value = store.coin_commodity_value_milli(silver, gold_price, silver_price);
    assert(silver_value >= 999'000 && silver_value <= 1'000'000);
    // Bimetallism melts the richer leg (max); legal circulation tracks the
    // cheaper leg (min) — the spread between the two is Gresham pressure.
    const auto bimetal_value = store.coin_commodity_value_milli(bimetal, gold_price, silver_price);
    assert(bimetal_value >= silver_value);
    assert(bimetal_value <= 1'000'000);
    // Fiat has no metal content: the commodity valuation is zero, and the
    // coin settles purely at the FX rate.
    assert(store.coin_commodity_value_milli(fiat, gold_price, silver_price) == 0);
    // Unknown currency: zero, never a crash.
    assert(store.coin_commodity_value_milli(
        economy_stable_key("currency.nonexistent"), gold_price, silver_price) == 0);
}

int main() {
    test_fixed_point_math();
    test_headline_tax_rate_drives_income_tax_policy();
    test_economy_definitions_reject_unsafe_content();
    test_market_entity_index_and_weekly_flow();
    test_market_index_rebuilds_on_membership_changes();
    test_scarcity_raises_price();
    test_economy_is_deterministic_across_worker_counts();
    test_pop_storage_is_compact();
    test_population_growth_tracks_standard_of_living();
    test_wages_paid_per_building_at_own_offer();
    test_population_growth_huge_pop_saturation();
    test_metal_market_price_feeds_fx_anchor();
    test_market_access_wedge_discounts_remote_producers();
    test_blockade_throttles_cross_border_trade();
    test_construction_consumes_materials();
    test_investment_pool_expansion_queues_project();
    test_sovereign_restructuring_writes_down_debt();
    test_bankrupt_building_downsizes_and_dies_on_debt_failure();
    test_company_receives_ownership_dividends();
    test_inventory_carry_cost_is_audited_sink();
    test_money_closed_loop_conservation();
    test_currency_store_basics();
    test_metal_currency_class();
    test_cross_currency_trade_and_fx_market();
    test_monetary_sovereignty_and_seigniorage();
    test_bimetallism_and_greshams_law();
    test_gdp_real_numeraire_and_domestic_wages();
    test_manual_convertibility_toggle();
    test_coin_commodity_value_milli();
    test_gold_points_specie_arbitrage_and_drain();
    test_sovereign_debt_issuance_credit_rating_and_default();
    test_bank_balance_sheet_credit_service_and_default();
    test_nonperforming_credit_draws_keep_npl_ledger_balanced();
    test_authored_trade_route_tariff_and_logistics_capacity();
    test_construction_queue_and_pm_gradual_transition();
    test_realistic_gradual_transitions_and_governance();
    test_raw_material_shortage_zero_throughput();
    test_material_closed_loop_conservation();
    std::cout << "All Thunder 1.0 economy tests passed.\n";
    return 0;
}
