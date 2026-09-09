#include "thunder/runtime/save/SaveGameInternal.hpp"

#include <cstdint>

namespace thunder::save_detail {

// Thunder 1.0 save schema v1 checksum compatibility. The original v1 world checksum
// ended after the first 19 GrandStrategyStore collections. New operational
// strategy-state collections are intentionally excluded here so a v1 save can be
// authenticated before being migrated to the current in-memory schema.
void legacy_hash_record(Fnv1a64& h, const TechnologyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.progress_ppm);h.add(r.unlocked);}
void legacy_hash_record(Fnv1a64& h, const LawRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.enacted);}
void legacy_hash_record(Fnv1a64& h, const InstitutionRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.level);}
void legacy_hash_record(Fnv1a64& h, const CompanyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.cash_milli);h.add(r.productivity_ppm);h.add(r.active);}
void legacy_hash_record(Fnv1a64& h, const TradeRouteRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.good.value());h.add(r.quantity_milli);h.add(r.level);h.add(r.active);}
void legacy_hash_record(Fnv1a64& h, const OwnershipStakeRecord& r){h.add(r.owner_country.value());h.add(r.owner_company.value());h.add(r.building.value());h.add(r.share_ppm);}
void legacy_hash_record(Fnv1a64& h, const TreatyRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(static_cast<std::uint8_t>(r.kind));h.add(r.article_hash);h.add(r.active);}
void legacy_hash_record(Fnv1a64& h, const ArmyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.manpower);h.add(r.organization_ppm);}
void legacy_hash_record(Fnv1a64& h, const NavyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.sailors);h.add(r.strength_ppm);h.add(r.design.value());}
void legacy_hash_record(Fnv1a64& h, const MigrationFlowRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.population);h.add(r.weeks_remaining);}
void legacy_hash_record(Fnv1a64& h, const InterestGroupRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.clout_ppm);h.add(r.approval_milli);}
void legacy_hash_record(Fnv1a64& h, const PoliticalPartyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.support_ppm);}
void legacy_hash_record(Fnv1a64& h, const PowerBlocRecord& r){h.add(r.leader.value());h.add(r.key_hash);h.add(r.cohesion_ppm);}
void legacy_hash_record(Fnv1a64& h, const DiplomaticPlayRecord& r){h.add(r.initiator.value());h.add(r.target.value());h.add(static_cast<std::uint8_t>(r.phase));h.add(r.war_goal_hash);}
void legacy_hash_record(Fnv1a64& h, const FrontRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(r.state.value());h.add(r.progress_milli);}
void legacy_hash_record(Fnv1a64& h, const BattleRecord& r){h.add(r.front.value());h.add(r.attackers);h.add(r.defenders);h.add(r.progress_milli);h.add(r.resolved);}
void legacy_hash_record(Fnv1a64& h, const ColonyRecord& r){h.add(r.country.value());h.add(r.province.value());h.add(r.progress_ppm);}
void legacy_hash_record(Fnv1a64& h, const ShipDesignRecord& r){h.add(r.country.value());h.add(r.hull_hash);h.add(r.modules_hash);h.add(r.combat_power_milli);h.add(r.build_cost_milli);}
void legacy_hash_record(Fnv1a64& h, const InvestmentPoolRecord& r){h.add(r.country.value());h.add(r.cash_milli);h.add(r.weekly_contribution_milli);}

template<class T>
void legacy_hash_vec(Fnv1a64& h, std::span<const T> values) {
    h.add(values.size());
    for (const auto& record : values) legacy_hash_record(h, record);
}

std::uint64_t legacy_grand_checksum_v1(const GrandStrategyStore& gs) noexcept {
    Fnv1a64 h;
    legacy_hash_vec(h, gs.technologys()); legacy_hash_vec(h, gs.laws());
    legacy_hash_vec(h, gs.institutions()); legacy_hash_vec(h, gs.companys());
    legacy_hash_vec(h, gs.trade_routes()); legacy_hash_vec(h, gs.ownership_stakes());
    legacy_hash_vec(h, gs.treatys()); legacy_hash_vec(h, gs.armys());
    legacy_hash_vec(h, gs.navys()); legacy_hash_vec(h, gs.migration_flows());
    legacy_hash_vec(h, gs.interest_groups()); legacy_hash_vec(h, gs.political_partys());
    legacy_hash_vec(h, gs.power_blocs()); legacy_hash_vec(h, gs.diplomatic_plays());
    legacy_hash_vec(h, gs.fronts()); legacy_hash_vec(h, gs.battles());
    legacy_hash_vec(h, gs.colonys()); legacy_hash_vec(h, gs.ship_designs());
    legacy_hash_vec(h, gs.investment_pools());
    return h.value();
}

std::uint64_t legacy_country_checksum_v1(const CountryStore& countries) noexcept {
    Fnv1a64 h;
    const auto n = countries.size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        h.add(countries.tag(id));
        h.add(countries.population(id));
        h.add(countries.gdp(id));
        h.add(countries.treasury(id));
        h.add(countries.tax_rate(id));
    }
    return h.value();
}

std::uint64_t legacy_pop_checksum_v1(const PopStore& pops) noexcept {
    Fnv1a64 h;
    h.add(pops.size());
    for (std::size_t i = 0; i < pops.size(); ++i) {
        const PopId id{static_cast<PopId::rep_type>(i)};
        h.add(pops.market(id).value());
        h.add(pops.province(id).value());
        h.add(pops.population(id));
        h.add(pops.employed(id));
        h.add(pops.employer(id).value());
        h.add(pops.need_profile(id).value());
        h.add(pops.income(id));
        h.add(pops.standard_of_living_milli(id));
        h.add(pops.culture(id).value());
        h.add(pops.religion(id).value());
        h.add(pops.profession(id).value());
        h.add(pops.interest_group(id).value());
        h.add(pops.literacy_permyriad(id));
        h.add(pops.qualification_permyriad(id));
        h.add(pops.wealth_milli(id));
        h.add(pops.political_strength_milli(id));
    }
    return h.value();
}

std::uint64_t legacy_market_checksum_v1(const MarketStore& markets) noexcept {
    // Pre-inventory market checksum layout, kept byte-stable so v1 saves keep
    // validating after MarketStore gained the inventory column.
    Fnv1a64 h;
    h.add(markets.good_count());
    const std::size_t count = markets.size();
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        h.add(markets.owner(m).value());
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        for (const auto v : markets.price_row(m)) h.add(v);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        for (const auto v : markets.supply_row(m)) h.add(v);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        for (const auto v : markets.demand_row(m)) h.add(v);
    }
    return h.value();
}

// Save v4 originally gained inventory/shortage but predates the authoritative
// market settlement account, currency and clearing-cash columns. Keep its exact
// MarketStore hash layout so an absent MON1 section can be authenticated before
// the deterministic defaults installed by MarketStore::resize are accepted.
std::uint64_t legacy_market_checksum_v4_pre_mon1(const MarketStore& markets) noexcept {
    Fnv1a64 h;
    h.add(markets.good_count());
    const std::size_t count = markets.size();
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        h.add(markets.owner(market).value());
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.price_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.supply_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.demand_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.inventory_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.shortage_row(market)) h.add(value);
    }
    return h.value();
}

std::uint64_t legacy_country_checksum_pre_fx(const CountryStore& countries) noexcept {
    Fnv1a64 h;
    const auto n = countries.size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        h.add(countries.tag(id));
        h.add(countries.population(id));
        h.add(countries.gdp(id));
        h.add(countries.treasury(id));
        h.add(countries.tax_rate(id));
        const auto& tp = countries.tax_policy(id);
        h.add(tp.income_tax_ppm);
        h.add(tp.consumption_tax_ppm);
        h.add(tp.land_tax_ppm);
        h.add(tp.per_capita_tax_ppm);
        h.add(tp.dividends_tax_ppm);
    }
    return h.value();
}

std::uint64_t legacy_geography_checksum_pre_resistance(const GeographyStore& g) noexcept {
    Fnv1a64 h; h.add(g.state_count()); h.add(g.province_count());
    for (std::size_t i=0;i<g.state_count();++i) {
        const StateId id{static_cast<StateId::rep_type>(i)};
        h.add(g.state_key(id)); h.add(g.state_owner(id).value());
        h.add(g.state_market(id).value()); h.add(g.state_capital(id).value());
    }
    for (std::size_t i=0;i<g.province_count();++i) {
        const ProvinceId id{static_cast<ProvinceId::rep_type>(i)};
        h.add(g.province_key(id)); h.add(g.province_state(id).value());
        h.add(g.province_owner(id).value()); h.add(g.province_market(id).value());
        h.add(std::bit_cast<std::uint64_t>(g.province_center_x(id)));
        h.add(std::bit_cast<std::uint64_t>(g.province_center_y(id)));
        h.add(g.province_areas_km2()[i]);
    }
    return h.value();
}

// CQ01 originally omitted weekly_progress_ppm from the construction checksum.
// Keep that historical layout available for authentic pre-hardening saves while
// the current checksum covers every serialized project field.
std::uint64_t legacy_construction_checksum_pre_weekly(const ConstructionStore& construction) noexcept {
    Fnv1a64 h;
    h.add(construction.size());
    for (const auto& p : construction.projects()) {
        h.add(p.id.value());
        h.add(p.country.value());
        h.add(p.province.value());
        h.add(p.target_building.value());
        h.add(static_cast<std::uint8_t>(p.kind));
        h.add(p.target_pm.value());
        h.add(p.monument_key_hash);
        h.add(p.progress_points);
        h.add(p.total_points_required);
        h.add(p.total_cost_milli);
        h.add(p.paid_cost_milli);
        h.add(p.paused ? 1u : 0u);
        h.add(p.priority);
    }
    return h.value();
}

std::uint64_t legacy_world_checksum_v4_pre_fx(const World& world) noexcept {
    Fnv1a64 h;
    h.add(legacy_country_checksum_pre_fx(world.countries));
    h.add(world.markets.checksum());
    h.add(world.buildings.checksum());
    h.add(world.pops.checksum());
    h.add(legacy_geography_checksum_pre_resistance(world.geography));
    h.add(world.grand_strategy.checksum());
    return h.value();
}

// v5 added per-POP fractional growth accumulators to the pop digest. This is
// the SLT1-era world digest exactly as v4 writers produced it: the current
// composition with the pre-growth pop checksum in place of the current one.
std::uint64_t legacy_pop_checksum_pre_growth(const PopStore& pops) noexcept {
    Fnv1a64 h;
    const auto n = pops.size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) {
            h.add(std::uint32_t{0});
            continue;
        }
        const PopId id{static_cast<PopId::rep_type>(i)};
        h.add(pops.market(id).value());
        h.add(pops.province(id).value());
        h.add(pops.population(id));
        h.add(pops.employed(id));
        h.add(pops.employer(id).value());
        h.add(pops.need_profile(id).value());
        h.add(pops.income(id));
        h.add(pops.cash(id));
        h.add(pops.standard_of_living_milli(id));
        h.add(pops.culture(id).value());
        h.add(pops.religion(id).value());
        h.add(pops.profession(id).value());
        h.add(pops.interest_group(id).value());
        h.add(pops.literacy_permyriad(id));
        h.add(pops.qualification_permyriad(id));
        h.add(pops.wealth_milli(id));
        h.add(pops.political_strength_milli(id));
    }
    for (auto w : pops.slot_pool().bitmap()) h.add(w);
    return h.value();
}

std::uint64_t legacy_world_checksum_v4_pre_growth(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum());
    h.add(world.markets.checksum());
    h.add(world.buildings.checksum());
    hash_slot_allocator_state(h, world.buildings.slot_pool());
    h.add(legacy_pop_checksum_pre_growth(world.pops));
    hash_slot_allocator_state(h, world.pops.slot_pool());
    h.add(world.geography.checksum());
    h.add(world.grand_strategy.checksum());
    h.add(world.currencies.checksum());
    h.add(world.banks.checksum());
    h.add(world.trade_policies.checksum());
    h.add(world.construction.checksum());
    if (!world.global_scripts.empty()) {
        h.add(0x31424c47u);
        h.add(world.global_scripts.checksum());
    }
    return h.value();
}

std::uint64_t legacy_currency_checksum_pre_integrity(const CurrencyStore& currencies) noexcept {
    Fnv1a64 h;
    h.add(currencies.size());
    for (const auto& c : currencies.currencies()) {
        h.add(c.key); h.add(c.name); h.add(static_cast<std::uint8_t>(c.standard));
        h.add(c.sovereign_leader.value()); h.add(c.gold_parity_mg); h.add(c.silver_parity_mg);
        h.add(c.exchange_rate_ppm); h.add(c.target_rate_ppm); h.add(c.trade_demand_milli);
        h.add(c.trade_supply_milli); h.add(c.seigniorage_accrued_milli);
    }
    return h.value();
}

std::uint64_t legacy_world_checksum_pre_financial(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum()); h.add(world.markets.checksum());
    h.add(world.buildings.checksum()); h.add(world.pops.checksum());
    h.add(legacy_geography_checksum_pre_resistance(world.geography)); h.add(world.grand_strategy.checksum());
    h.add(legacy_currency_checksum_pre_integrity(world.currencies));
    return h.value();
}

std::uint64_t legacy_world_checksum_pre_construction(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum()); h.add(world.markets.checksum());
    h.add(world.buildings.checksum()); h.add(world.pops.checksum());
    h.add(legacy_geography_checksum_pre_resistance(world.geography));
    h.add(world.grand_strategy.checksum());
    h.add(world.currencies.checksum()); h.add(world.banks.checksum());
    h.add(world.trade_policies.checksum());
    return h.value();
}

std::uint64_t legacy_world_checksum_pre_resistance(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum()); h.add(world.markets.checksum());
    h.add(world.buildings.checksum()); h.add(world.pops.checksum());
    h.add(legacy_geography_checksum_pre_resistance(world.geography));
    h.add(world.grand_strategy.checksum());
    h.add(world.currencies.checksum()); h.add(world.banks.checksum());
    h.add(world.trade_policies.checksum());
    h.add(legacy_construction_checksum_pre_weekly(world.construction));
    return h.value();
}

std::uint64_t legacy_world_checksum_v3_pre_mon1(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum());
    h.add(legacy_market_checksum_v1(world.markets));
    h.add(world.buildings.checksum());
    h.add(world.pops.checksum());
    h.add(legacy_geography_checksum_pre_resistance(world.geography));
    h.add(world.grand_strategy.checksum());
    return h.value();
}

std::uint64_t legacy_world_checksum_v4_pre_mon1(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum());
    h.add(legacy_market_checksum_v4_pre_mon1(world.markets));
    h.add(world.buildings.checksum());
    h.add(world.pops.checksum());
    h.add(legacy_geography_checksum_pre_resistance(world.geography));
    h.add(world.grand_strategy.checksum());
    return h.value();
}

std::uint64_t legacy_world_checksum_v1(const World& world) noexcept {
    Fnv1a64 h;
    h.add(legacy_country_checksum_v1(world.countries));
    h.add(legacy_market_checksum_v1(world.markets));
    h.add(world.buildings.checksum());
    h.add(legacy_pop_checksum_v1(world.pops));
    h.add(legacy_geography_checksum_pre_resistance(world.geography));
    h.add(legacy_grand_checksum_v1(world.grand_strategy));
    return h.value();
}


} // namespace thunder::save_detail
