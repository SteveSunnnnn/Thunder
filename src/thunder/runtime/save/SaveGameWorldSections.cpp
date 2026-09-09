#include "thunder/runtime/save/SaveGameInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace thunder::save_detail

{
namespace {

ScopeType read_on_action_scope_type(Reader& r) {
    const auto value = r.u8();
    if (value < static_cast<std::uint8_t>(ScopeType::Country) ||
        value > static_cast<std::uint8_t>(ScopeType::Market))
        throw std::runtime_error("invalid scope type in Thunder save");
    return static_cast<ScopeType>(value);
}

ScopeType read_optional_on_action_scope_type(Reader& r) {
    const auto value = r.u8();
    if (value > static_cast<std::uint8_t>(ScopeType::Market) &&
        value != static_cast<std::uint8_t>(ScopeType::None))
        throw std::runtime_error("invalid optional scope type in Thunder save");
    return static_cast<ScopeType>(value);
}

} // namespace

void encode_market_monetary_section(Writer& w, const MarketStore& markets) {
    if (markets.size() > max_markets)
        throw std::runtime_error("market monetary state exceeds save safety cap");
    w.u32(market_monetary_section_tag);
    w.u32(static_cast<std::uint32_t>(markets.size()));
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        const auto account = markets.settlement_account(market);
        const auto expected_account = market_settlement_account_id(market);
        const auto currency = markets.currency_key(market);
        if (!account.valid() || account != expected_account)
            throw std::runtime_error("market contains invalid stable settlement account");
        if (currency == 0u)
            throw std::runtime_error("market contains zero currency key");
        w.u64(account.value());
        w.u64(currency);
        w.i64(markets.clearing_cash(market));
    }
}

DecodedMarketMonetaryState decode_market_monetary_section(Reader& r,
                                                           MarketStore& markets) {
    DecodedMarketMonetaryState decoded;
    decoded.present = true;
    if (r.u32() != market_monetary_section_tag)
        throw std::runtime_error("invalid market monetary extension tag");
    const auto count = r.count(max_markets);
    if (count != markets.size())
        throw std::runtime_error("market monetary extension count mismatch");
    std::unordered_set<std::uint64_t> settlement_accounts;
    settlement_accounts.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        const SettlementAccountId account{r.u64()};
        const auto currency = r.u64();
        const auto clearing_cash = r.i64();
        if (!account.valid() || account != market_settlement_account_id(market) ||
            !settlement_accounts.insert(account.value()).second)
            throw std::runtime_error("invalid stable market settlement account in save");
        if (currency == 0u)
            throw std::runtime_error("zero market currency key in save");
        // The stable account ID is derived from MarketId and already installed
        // by resize; only independently mutable monetary columns need setters.
        markets.set_currency_key(market, currency);
        markets.set_clearing_cash(market, clearing_cash);
    }
    return decoded;
}

void encode_fx_section(Writer& w, const CurrencyStore& currencies, const CountryStore& countries) {
    w.u32(fx_section_tag);
    w.u32(static_cast<std::uint32_t>(currencies.size()));
    for (const auto& c : currencies.currencies()) {
        w.u64(c.key);
        w.string(c.name);
        w.u8(static_cast<std::uint8_t>(c.standard));
        w.u32(c.gold_parity_mg);
        w.u32(c.silver_parity_mg);
        w.u32(c.sovereign_leader.value());
        w.i64(c.exchange_rate_ppm);
        w.i64(c.target_rate_ppm);
        w.boolean(c.convertibility_suspended);
        // Per-tick FX flow accumulators are covered by CurrencyStore::checksum().
        // Dropping them makes every save taken after an FX round fail
        // verification on load with "save world checksum mismatch".
        w.i64(c.trade_demand_milli);
        w.i64(c.trade_supply_milli);
        w.i64(c.seigniorage_accrued_milli);
    }
    w.u32(static_cast<std::uint32_t>(countries.size()));
    for (std::size_t i = 0; i < countries.size(); ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        w.u64(countries.primary_currency(id));
        w.i64(countries.foreign_reserves_milli(id));
        w.i64(countries.balance_of_payments_milli(id));
        w.f64(countries.prestige(id));
        w.i64(countries.national_debt_milli(id));
        w.u8(static_cast<std::uint8_t>(countries.credit_rating(id)));
        w.i64(countries.bond_yield_ppm(id));
        w.u16(countries.default_weeks(id));
    }
}

DecodedFxState decode_fx_section(Reader& r, CurrencyStore& currencies, CountryStore& countries) {
    DecodedFxState decoded;
    decoded.present = true;
    if (r.u32() != fx_section_tag)
        throw std::runtime_error("invalid fx extension tag");
    const auto cur_count = r.count(1024u);
    currencies.clear();
    std::unordered_set<CurrencyKey> currency_keys;
    currency_keys.reserve(cur_count);
    for (std::uint32_t i = 0; i < cur_count; ++i) {
        const auto key = r.u64();
        const auto name = r.string();
        const auto std_val = static_cast<MonetaryStandard>(r.u8());
        const auto gold_mg = r.u32();
        const auto silver_mg = r.u32();
        const auto leader_val = r.u32();
        const auto rate = r.i64();
        const auto target = r.i64();
        const auto suspended = r.boolean();
        const auto trade_demand = r.i64();
        const auto trade_supply = r.i64();
        const auto seigniorage = r.i64();
        if (key == 0u || !currency_keys.insert(key).second ||
            static_cast<std::uint8_t>(std_val) >
                static_cast<std::uint8_t>(MonetaryStandard::FiatFloating) ||
            gold_mg == 0u || silver_mg == 0u || rate < 10'000 || rate > 100'000'000 ||
            target < 10'000 || target > 100'000'000 ||
            trade_demand < 0 || trade_supply < 0 || seigniorage < 0 ||
            (leader_val != 0xFFFFFFFFu && leader_val >= countries.size()))
            throw std::runtime_error("invalid currency record in save");
        currencies.register_currency(key, name, std_val, gold_mg, silver_mg, rate);
        currencies.set_exchange_rate_ppm(key, rate);
        currencies.set_target_rate_ppm(key, target);
        currencies.set_monetary_standard(key, std_val);
        currencies.set_gold_parity_mg(key, gold_mg);
        currencies.set_silver_parity_mg(key, silver_mg);
        currencies.set_convertibility_suspended(key, suspended);
        currencies.set_trade_demand_milli(key, trade_demand);
        currencies.set_trade_supply_milli(key, trade_supply);
        currencies.set_seigniorage_accrued_milli(key, seigniorage);
        if (leader_val != 0xFFFFFFFFu) {
            currencies.set_sovereign_leader(key, CountryId{leader_val}, 0.0);
        }
    }
    const auto country_count = r.count(static_cast<std::uint32_t>(countries.size()));
    if (country_count != countries.size())
        throw std::runtime_error("country count mismatch in fx extension");
    for (std::size_t i = 0; i < country_count; ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        const auto primary_cur = r.u64();
        const auto reserves = r.i64();
        const auto bop = r.i64();
        const auto prestige = r.f64();
        const auto debt = r.i64();
        const auto rating_val = r.u8();
        const auto bond_yield = r.i64();
        const auto def_weeks = r.u16();
        if (primary_cur == 0u || !currencies.contains(primary_cur) || reserves < 0 ||
            !std::isfinite(prestige) || debt < 0 || rating_val > 7u ||
            bond_yield < 10'000 || bond_yield > 500'000)
            throw std::runtime_error("invalid country monetary record in save");
        countries.set_primary_currency(id, primary_cur);
        countries.set_foreign_reserves_milli(id, reserves);
        countries.set_balance_of_payments_milli(id, bop);
        countries.set_prestige(id, prestige);
        countries.set_national_debt_milli(id, debt);
        countries.set_credit_rating(id, static_cast<CreditRating>(rating_val));
        countries.set_bond_yield_ppm(id, bond_yield);
        countries.set_default_weeks(id, def_weeks);
    }
    return decoded;
}

void encode_financial_section(Writer& w, const World& world) {
    w.u32(financial_section_tag);
    w.u32(static_cast<std::uint32_t>(world.trade_policies.size()));
    for (std::size_t i = 0; i < world.trade_policies.size(); ++i) {
        const CountryId country{static_cast<CountryId::rep_type>(i)};
        const auto& policy = world.trade_policies.get(country);
        w.i32(policy.import_tariff_ppm); w.i32(policy.export_tariff_ppm);
        w.i64(policy.logistics_capacity_milli); w.i64(world.trade_policies.used_capacity(country));
    }
    w.u32(static_cast<std::uint32_t>(world.banks.size()));
    for (std::size_t i = 0; i < world.banks.size(); ++i) {
        const auto b = world.banks.bank(BankId{static_cast<BankId::rep_type>(i)});
        w.u64(b.key); wid(w, b.country); w.u64(b.currency);
        w.i64(b.reserves_milli); w.i64(b.deposits_milli); w.i64(b.equity_milli);
        w.i64(b.loan_assets_milli); w.i64(b.sovereign_bonds_milli);
        w.i64(b.nonperforming_milli); w.i64(b.retained_earnings_milli);
        w.i32(b.reserve_requirement_ppm); w.i32(b.capital_requirement_ppm);
        w.i64(b.deposit_rate_ppm); w.i64(b.loan_rate_ppm); w.u8(static_cast<std::uint8_t>(b.status));
    }
    w.u32(static_cast<std::uint32_t>(world.banks.loan_count()));
    for (std::size_t i = 0; i < world.banks.loan_count(); ++i) {
        const auto l = world.banks.loan(BankLoanId{static_cast<BankLoanId::rep_type>(i)});
        w.u64(l.key); wid(w, l.bank); w.u8(static_cast<std::uint8_t>(l.borrower_kind));
        w.u32(l.borrower_id); w.i64(l.principal_milli); w.i64(l.annual_rate_ppm);
        w.u16(l.remaining_weeks); w.u16(l.arrears_weeks); w.u8(static_cast<std::uint8_t>(l.status));
    }
}

DecodedFinancialState decode_financial_section(Reader& r, World& world) {
    if (r.u32() != financial_section_tag) throw std::runtime_error("invalid financial extension tag");
    DecodedFinancialState decoded{true};
    const auto policy_count = r.count(max_countries);
    if (policy_count > world.countries.size()) throw std::runtime_error("financial policy country count mismatch");
    world.trade_policies.resize(policy_count);
    for (std::uint32_t i = 0; i < policy_count; ++i) {
        const CountryId country{i};
        TradePolicy policy;
        policy.import_tariff_ppm = r.i32(); policy.export_tariff_ppm = r.i32();
        policy.logistics_capacity_milli = r.i64(); const auto used = r.i64();
        world.trade_policies.set(country, policy);
        if (used < 0 || world.trade_policies.reserve_capacity(country, used) != used)
            throw std::runtime_error("invalid used trade logistics capacity");
    }
    const auto bank_count = r.count(max_banks);
    world.banks.clear();
    for (std::uint32_t i = 0; i < bank_count; ++i) {
        BankSnapshot b;
        b.key=r.u64(); b.country=rid<CountryId>(r); b.currency=r.u64();
        b.reserves_milli=r.i64(); b.deposits_milli=r.i64(); b.equity_milli=r.i64();
        b.loan_assets_milli=r.i64(); b.sovereign_bonds_milli=r.i64();
        b.nonperforming_milli=r.i64(); b.retained_earnings_milli=r.i64();
        b.reserve_requirement_ppm=r.i32(); b.capital_requirement_ppm=r.i32();
        b.deposit_rate_ppm=r.i64(); b.loan_rate_ppm=r.i64(); b.status=static_cast<BankStatus>(r.u8());
        world.banks.restore_bank(b);
    }
    const auto loan_count = r.count(max_bank_loans);
    for (std::uint32_t i = 0; i < loan_count; ++i) {
        BankLoanSnapshot l;
        l.key=r.u64(); l.bank=rid<BankId>(r); l.borrower_kind=static_cast<BankBorrowerKind>(r.u8());
        l.borrower_id=r.u32(); l.principal_milli=r.i64(); l.annual_rate_ppm=r.i64();
        l.remaining_weeks=r.u16(); l.arrears_weeks=r.u16(); l.status=static_cast<BankLoanStatus>(r.u8());
        world.banks.restore_loan(l);
    }
    return decoded;
}

void encode_construction_section(Writer& w, const ConstructionStore& construction,
                                 const World& world) {
    if (!construction.validate(world))
        throw std::runtime_error("construction queue contains invalid project state");
    w.u32(construction_section_tag);
    w.u32(static_cast<std::uint32_t>(construction.size()));
    for (const auto& p : construction.projects()) {
        wid(w, p.id);
        wid(w, p.country);
        wid(w, p.province);
        wid(w, p.target_building);
        w.u8(static_cast<std::uint8_t>(p.kind));
        wid(w, p.target_pm);
        w.u64(p.monument_key_hash);
        w.u32(p.progress_points);
        w.u32(p.total_points_required);
        w.u32(p.weekly_progress_ppm);
        w.i64(p.total_cost_milli);
        w.i64(p.paid_cost_milli);
        w.boolean(p.paused);
        w.u32(p.priority);
    }
}

DecodedConstructionState decode_construction_section(Reader& r, ConstructionStore& construction) {
    if (r.u32() != construction_section_tag) throw std::runtime_error("invalid construction extension tag");
    DecodedConstructionState decoded{true};
    const auto count = r.count(100'000u);
    construction.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        ConstructionProjectRecord p;
        p.id = rid<ConstructionProjectId>(r);
        p.country = rid<CountryId>(r);
        p.province = rid<ProvinceId>(r);
        p.target_building = rid<BuildingId>(r);
        p.kind = static_cast<ConstructionKind>(r.u8());
        p.target_pm = rid<ProductionMethodId>(r);
        p.monument_key_hash = r.u64();
        p.progress_points = r.u32();
        p.total_points_required = r.u32();
        p.weekly_progress_ppm = r.u32();
        p.total_cost_milli = r.i64();
        p.paid_cost_milli = r.i64();
        p.paused = r.boolean();
        p.priority = r.u32();
        construction.restore_project(p);
    }
    return decoded;
}

void encode_resistance_section(Writer& w, const GeographyStore& geography) {
    w.u32(resistance_section_tag);
    w.u32(static_cast<std::uint32_t>(geography.state_count()));
    for (std::size_t i = 0; i < geography.state_count(); ++i) {
        const StateId id{static_cast<StateId::rep_type>(i)};
        w.u32(geography.state_resistance_ppm(id));
    }
}

DecodedResistanceState decode_resistance_section(Reader& r, GeographyStore& geography) {
    if (r.u32() != resistance_section_tag) throw std::runtime_error("invalid resistance extension tag");
    DecodedResistanceState decoded{true};
    const auto count = r.u32();
    if (count != geography.state_count()) throw std::runtime_error("resistance state count mismatch");
    for (std::size_t i = 0; i < count; ++i) {
        const StateId id{static_cast<StateId::rep_type>(i)};
        geography.set_state_resistance_ppm(id, r.u32());
    }
    return decoded;
}

void encode_geography_section(Writer& w, const GeographyStore& geography) {
    w.u32(geography_section_tag);
    w.u32(static_cast<std::uint32_t>(geography.province_count()));
    for (std::size_t i = 0; i < geography.province_count(); ++i) {
        const auto id = ProvinceId{static_cast<ProvinceId::rep_type>(i)};
        w.u8(static_cast<std::uint8_t>(geography.province_kind(id)));
        w.u8(static_cast<std::uint8_t>(
            (geography.province_is_coastal(id) ? 1u : 0u) |
            (geography.province_is_impassable(id) ? 2u : 0u)));
        const auto compound = geography.province_compound_terrain(id);
        w.u8(static_cast<std::uint8_t>(compound.climate));
        w.u8(static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>(compound.topography) & 0x0fu) |
            ((static_cast<std::uint8_t>(compound.vegetation) & 0x0fu) << 4)));
    }
}

DecodedGeographyState decode_geography_section(Reader& r, GeographyStore& geography) {
    if (r.u32() != geography_section_tag) throw std::runtime_error("invalid geography extension tag");
    const auto count = r.u32();
    if (count != geography.province_count()) throw std::runtime_error("geography province count mismatch");
    for (std::size_t i = 0; i < count; ++i) {
        const auto kind = r.u8();
        const auto flags = r.u8();
        const auto climate_raw = r.u8();
        const auto topo_veg_raw = r.u8();
        if (kind > static_cast<std::uint8_t>(ProvinceKind::Lake) ||
            (flags & 0xfcu) != 0u)
            throw std::runtime_error("invalid geography province flags");
        const auto id = ProvinceId{static_cast<ProvinceId::rep_type>(i)};
        geography.set_province_kind(id, static_cast<ProvinceKind>(kind));
        geography.set_province_coastal(id, (flags & 1u) != 0u);
        geography.set_province_impassable(id, (flags & 2u) != 0u);
        if (climate_raw < static_cast<std::uint8_t>(ClimateType::Count)) {
            geography.set_province_climate(id, static_cast<ClimateType>(climate_raw));
        }
        const auto topo_raw = topo_veg_raw & 0x0fu;
        const auto veg_raw = (topo_veg_raw >> 4) & 0x0fu;
        if (topo_raw < static_cast<std::uint8_t>(TopographyType::Count)) {
            geography.set_province_topography(id, static_cast<TopographyType>(topo_raw));
        }
        if (veg_raw < static_cast<std::uint8_t>(VegetationType::Count)) {
            geography.set_province_vegetation(id, static_cast<VegetationType>(veg_raw));
        }
    }
    return {true};
}

void encode_slot_pool(Writer& w, const SlotPool& pool, std::size_t max_entities) {
    const auto generations = pool.generations();
    const auto bitmap = pool.bitmap();
    const auto free_list = pool.free_list();
    if (generations.size() > max_entities || free_list.size() > max_entities ||
        bitmap.size() > (max_entities + 63u) / 64u)
        throw std::runtime_error("slot pool state exceeds save safety cap");
    w.u32(static_cast<std::uint32_t>(generations.size()));
    for (const auto generation : generations) w.u32(generation);
    w.u32(static_cast<std::uint32_t>(bitmap.size()));
    for (const auto word : bitmap) w.u64(word);
    w.u32(static_cast<std::uint32_t>(free_list.size()));
    for (const auto index : free_list) w.u32(index);
}

void decode_slot_pool(Reader& r, SlotPool& pool, std::size_t expected_entities,
                      std::uint32_t max_entities) {
    const auto entity_count = r.count(max_entities);
    if (entity_count != expected_entities)
        throw std::runtime_error("slot pool entity count mismatch");
    std::vector<std::uint32_t> generations(entity_count);
    for (auto& generation : generations) generation = r.u32();
    const auto bitmap_count = r.count((max_entities + 63u) / 64u);
    const auto expected_bitmap_count = (expected_entities + 63u) / 64u;
    if (bitmap_count != expected_bitmap_count)
        throw std::runtime_error("slot pool bitmap count mismatch");
    std::vector<std::uint64_t> bitmap(bitmap_count);
    for (auto& word : bitmap) word = r.u64();
    const auto free_count = r.count(max_entities);
    std::vector<std::uint32_t> free_list(free_count);
    for (auto& index : free_list) index = r.u32();
    pool.restore_state(generations, bitmap, free_list);
}

void encode_slot_section(Writer& w, const World& world) {
    w.u32(slot_section_tag);
    encode_slot_pool(w, world.buildings.slot_pool(), max_buildings);
    encode_slot_pool(w, world.pops.slot_pool(), max_pops);
}

DecodedSlotState decode_slot_section(Reader& r, World& world) {
    if (r.u32() != slot_section_tag)
        throw std::runtime_error("invalid slot-pool extension tag");
    decode_slot_pool(r, world.buildings.slot_pool(), world.buildings.size(), max_buildings);
    decode_slot_pool(r, world.pops.slot_pool(), world.pops.size(), max_pops);
    return {true};
}

void encode_on_action_section(Writer& w, const OnActionRuntime& on_actions) {
    w.u32(on_action_section_tag);
    w.u64(on_actions.next_invocation_id());
    w.u32(static_cast<std::uint32_t>(on_actions.queue().size()));
    const auto definitions = on_actions.definitions();
    for (const auto& invocation : on_actions.queue()) {
        if (invocation.definition >= definitions.size())
            throw std::runtime_error("runtime contains invalid on_action definition");
        w.u64(invocation.id.value());
        w.string(definitions[invocation.definition].key);
        w.u8(static_cast<std::uint8_t>(invocation.scope.type));
        w.u32(invocation.scope.raw_id);
        w.u8(static_cast<std::uint8_t>(invocation.from.type));
        w.u32(invocation.from.raw_id);
        w.u64(invocation.due_tick);
    }
}

DecodedOnActionState decode_on_action_section(Reader& r,
                                              const OnActionRuntime& on_actions) {
    DecodedOnActionState decoded;
    if (r.u32() != on_action_section_tag)
        throw std::runtime_error("unknown extension section in Thunder save");
    decoded.present = true;
    decoded.next_invocation_id = r.u64();
    const auto count = r.count(max_on_action_invocations);
    decoded.queue.reserve(count);
    for (std::uint32_t index = 0u; index < count; ++index) {
        ScheduledOnActionInvocation invocation;
        invocation.id = OnActionInvocationId{r.u64()};
        const auto definition_key = r.string();
        invocation.definition = on_actions.find_definition(definition_key);
        if (invocation.definition == std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("save references missing on_action definition: " +
                                     definition_key);
        invocation.scope = {read_on_action_scope_type(r), r.u32()};
        invocation.from = {read_optional_on_action_scope_type(r), r.u32()};
        invocation.due_tick = r.u64();
        decoded.queue.push_back(invocation);
    }
    return decoded;
}

void encode_population_growth_section(Writer& w, const PopStore& pops) {
    w.u32(population_growth_section_tag);
    w.u32(static_cast<std::uint32_t>(pops.size()));
    for (const auto progress : pops.growth_progress_all()) w.i32(progress);
}

DecodedPopulationGrowthState decode_population_growth_section(Reader& r, PopStore& pops) {
    if (r.u32() != population_growth_section_tag)
        throw std::runtime_error("invalid population growth extension tag");
    DecodedPopulationGrowthState decoded{true};
    const auto count = r.u32();
    if (count != pops.size()) throw std::runtime_error("population growth count mismatch");
    auto progress = pops.growth_progress_mut();
    for (std::size_t i = 0; i < count; ++i) progress[i] = r.i32();
    return decoded;
}
} // namespace thunder::save_detail
