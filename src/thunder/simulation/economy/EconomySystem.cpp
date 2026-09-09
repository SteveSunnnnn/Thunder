#include "thunder/simulation/economy/EconomySystem.hpp"
#include "thunder/simulation/economy/BuildingStore.hpp"
#include "thunder/simulation/economy/MarketStore.hpp"
#include "thunder/simulation/economy/PopStore.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace thunder {
namespace {
using Clock = std::chrono::steady_clock;

 // Fallback metal prices (ppm) when no bullion good is authored. Silver sits at
// the classical 15.5:1 ratio: 1'000'000 / 15.5 = 64'516.
inline constexpr EconomyPrice kFallbackGoldPricePpm = 1'000'000;
inline constexpr EconomyPrice kFallbackSilverPricePpm = 64'516;

// Market-driven metal price (R1): the numeraire-averaged market price of the
// bullion good bound to the metal numeraire, normalized by its base price.
// A gold rush that floods markets with bullion depreciates the metal price and
// drags metallic-standard parities with it; scarcity appreciates them.
EconomyPrice metal_market_price_ppm(const World& world, GoodId metal_good,
                                    EconomyPrice base_price_milli,
                                    EconomyPrice fallback_ppm) noexcept {
    if (!metal_good.valid() ||
        static_cast<std::size_t>(metal_good.value()) >= world.markets.good_count())
        return fallback_ppm;
    const std::size_t markets = world.markets.size();
    if (markets == 0) return fallback_ppm;
    if (base_price_milli <= 0) return fallback_ppm;
    EconomyAmount total_numeraire = 0;
    for (std::size_t mi = 0; mi < markets; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        total_numeraire = saturating_add(total_numeraire,
            world.currencies.convert_price(world.markets.price(market, metal_good),
                                           world.markets.currency_key(market),
                                           default_currency_key));
    }
    const auto average_numeraire = total_numeraire / static_cast<EconomyAmount>(markets);
    const auto ppm = mul_div_nonnegative(average_numeraire, ppm_scale, base_price_milli);
    return std::max<EconomyPrice>(1, ppm);
}
} // namespace

MonetaryBalanceSheet EconomySystem::monetary_balance_sheet(const World& world) noexcept {
    MonetaryBalanceSheet sheet;    const auto pop_cash = world.pops.cash_all();
    const auto pop_markets = world.pops.markets();
    const auto num_pops = world.pops.size();
    const bool single_currency = world.currencies.currencies().size() <= 1;

    for (std::size_t i = 0; i < num_pops; ++i) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const auto cash = pop_cash[i];
        if (cash == 0) continue;
        if (single_currency) {
            sheet.pop_deposits_milli = saturating_add(sheet.pop_deposits_milli, cash);
        } else {
            const auto market = pop_markets[i];
            const auto currency = market.valid() && market.value() < world.markets.size()
                ? world.markets.currency_key(market) : default_currency_key;
            sheet.pop_deposits_milli = saturating_add(sheet.pop_deposits_milli,
                world.currencies.convert(cash, currency, default_currency_key));
        }
    }
    const auto b_cash = world.buildings.cash_all();
    const auto b_markets = world.buildings.markets();
    const auto num_buildings = world.buildings.size();
    for (std::size_t i = 0; i < num_buildings; ++i) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const auto cash = b_cash[i];
        if (cash == 0) continue;
        if (single_currency) {
            sheet.building_deposits_milli = saturating_add(sheet.building_deposits_milli, cash);
        } else {
            const auto market = b_markets[i];
            const auto currency = market.valid() && market.value() < world.markets.size()
                ? world.markets.currency_key(market) : default_currency_key;
            sheet.building_deposits_milli = saturating_add(sheet.building_deposits_milli,
                world.currencies.convert(cash, currency, default_currency_key));
        }
    }
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        sheet.market_clearing_milli = saturating_add(
            sheet.market_clearing_milli,
            world.currencies.convert(
                world.markets.clearing_cash(MarketId{static_cast<MarketId::rep_type>(mi)}),
                world.markets.currency_key(MarketId{static_cast<MarketId::rep_type>(mi)}),
                default_currency_key));
    }
    for (std::size_t ci = 0; ci < world.countries.size(); ++ci) {
        sheet.treasury_deposits_milli = saturating_add(
            sheet.treasury_deposits_milli,
            world.currencies.convert(
                world.countries.treasury_milli(CountryId{static_cast<CountryId::rep_type>(ci)}),
                world.countries.primary_currency(CountryId{static_cast<CountryId::rep_type>(ci)}),
                default_currency_key));
    }
    for (const auto& company : world.grand_strategy.companys()) {
        const auto currency = company.country.valid() && company.country.value() < world.countries.size()
            ? world.countries.primary_currency(company.country) : default_currency_key;
        sheet.company_deposits_milli = saturating_add(sheet.company_deposits_milli,
            world.currencies.convert(company.cash_milli, currency, default_currency_key));
    }
    for (const auto& pool : world.grand_strategy.investment_pools()) {
        const auto currency = pool.country.valid() && pool.country.value() < world.countries.size()
            ? world.countries.primary_currency(pool.country) : default_currency_key;
        sheet.investment_pool_deposits_milli = saturating_add(
            sheet.investment_pool_deposits_milli,
            world.currencies.convert(pool.cash_milli, currency, default_currency_key));
    }
    for (std::size_t i = 0; i < world.banks.size(); ++i) {
        const auto bank = world.banks.bank(BankId{static_cast<BankId::rep_type>(i)});
        sheet.bank_deposit_liabilities_milli = saturating_add(sheet.bank_deposit_liabilities_milli,
            world.currencies.convert(bank.deposits_milli, bank.currency, default_currency_key));
        sheet.bank_credit_assets_milli = saturating_add(sheet.bank_credit_assets_milli,
            world.currencies.convert(saturating_add(bank.loan_assets_milli, bank.sovereign_bonds_milli),
                                     bank.currency, default_currency_key));
    }
    sheet.total_milli = saturating_add(sheet.pop_deposits_milli, sheet.building_deposits_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.market_clearing_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.treasury_deposits_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.company_deposits_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.investment_pool_deposits_milli);
    return sheet;
}

MaterialBalanceSheet EconomySystem::material_balance_sheet(const World& world) noexcept {
    MaterialBalanceSheet m{};
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (std::size_t gi = 0; gi < world.markets.good_count(); ++gi) {
            const GoodId good{static_cast<GoodId::rep_type>(gi)};
            m.total_supply_milli = saturating_add(m.total_supply_milli, world.markets.supply(market, good));
            m.total_demand_milli = saturating_add(m.total_demand_milli, world.markets.demand(market, good));
            m.total_inventory_milli = saturating_add(m.total_inventory_milli, world.markets.inventory(market, good));
            m.total_shortage_milli = saturating_add(m.total_shortage_milli, world.markets.shortage(market, good));
        }
    }
    // Closed-loop invariant: available = supply+inventory, wanted = demand,
    // fulfilled = min(available,wanted), inventory' = available-fulfilled, shortage' = wanted-fulfilled
    // so inventory - shortage should be stable modulo trade/price pressure caps
    m.net_material_milli = saturating_add(saturating_sub(m.total_supply_milli, m.total_demand_milli),
                                          saturating_sub(m.total_inventory_milli, m.total_shortage_milli));
    return m;
}

void EconomySystem::rebuild_indices(const World& world) {
    index_.rebuild(world.markets.size(), world.pops, world.buildings);
    ensure_market_scratch(world.markets.size());
}

void EconomySystem::ensure_market_scratch(std::size_t markets) {
    if (market_tax_milli_.size() != markets) market_tax_milli_.assign(markets, 0);
    if (market_dividend_milli_.size() != markets) market_dividend_milli_.assign(markets, 0);
    if (market_gdp_milli_.size() != markets) market_gdp_milli_.assign(markets, 0);
    if (market_population_.size() != markets) market_population_.assign(markets, 0u);
    if (market_loan_demand_milli_.size() != markets) market_loan_demand_milli_.assign(markets, 0);
    if (market_produced_scratch_.size() != markets) market_produced_scratch_.assign(markets, 0);
    if (market_demanded_scratch_.size() != markets) market_demanded_scratch_.assign(markets, 0);
    if (market_fulfilled_scratch_.size() != markets) market_fulfilled_scratch_.assign(markets, 0);
    if (market_spoilage_scratch_.size() != markets) market_spoilage_scratch_.assign(markets, 0);
    if (market_carry_scratch_.size() != markets) market_carry_scratch_.assign(markets, 0);    const std::size_t profile_cells = markets * definitions_.need_profile_count();
    if (profile_population_.size() != profile_cells) profile_population_.assign(profile_cells, 0u);
    if (profile_basket_cost_milli_.size() != profile_cells) profile_basket_cost_milli_.assign(profile_cells, 0);
    if (profile_fulfillment_ppm_.size() != profile_cells) profile_fulfillment_ppm_.assign(profile_cells, ppm_scale);
    const std::size_t good_cells = markets * definitions_.good_count();
    if (market_fulfillment_ppm_.size() != good_cells) market_fulfillment_ppm_.assign(good_cells, ppm_scale);
    if (market_sales_ppm_.size() != good_cells) market_sales_ppm_.assign(good_cells, ppm_scale);
    if (base_price_milli_.size() != definitions_.good_count()) {
        base_price_milli_.resize(definitions_.good_count());
        for (std::size_t gi = 0; gi < base_price_milli_.size(); ++gi) {
            base_price_milli_[gi] = definitions_.good(GoodId{static_cast<GoodId::rep_type>(gi)}).base_price_milli;
        }
    }
}

std::size_t EconomySystem::scratch_memory_bytes() const noexcept {
    return index_.memory_bytes()
        + market_tax_milli_.capacity() * sizeof(EconomyAmount)
        + market_dividend_milli_.capacity() * sizeof(EconomyAmount)
        + market_gdp_milli_.capacity() * sizeof(EconomyAmount)
        + market_population_.capacity() * sizeof(std::uint64_t)
        + country_gdp_milli_.capacity() * sizeof(EconomyAmount)
        + country_nominal_gdp_milli_.capacity() * sizeof(EconomyAmount)
        + country_population_.capacity() * sizeof(std::uint64_t)
        + profile_population_.capacity() * sizeof(std::uint64_t)
        + profile_basket_cost_milli_.capacity() * sizeof(EconomyAmount)
        + building_remaining_.capacity() * sizeof(PopulationCount)
        + base_price_milli_.capacity() * sizeof(EconomyPrice)
        + market_fulfillment_ppm_.capacity() * sizeof(std::int64_t)
        + market_sales_ppm_.capacity() * sizeof(std::int64_t)
        + profile_fulfillment_ppm_.capacity() * sizeof(std::int64_t)
        + market_loan_demand_milli_.capacity() * sizeof(EconomyAmount)
        + building_loan_demand_milli_.capacity() * sizeof(EconomyAmount)
        + building_throughput_ppm_.capacity() * sizeof(std::int32_t)
        + market_produced_scratch_.capacity() * sizeof(EconomyAmount)
        + market_demanded_scratch_.capacity() * sizeof(EconomyAmount)
        + market_fulfilled_scratch_.capacity() * sizeof(EconomyAmount)
        + market_spoilage_scratch_.capacity() * sizeof(EconomyAmount)
        + market_carry_scratch_.capacity() * sizeof(EconomyAmount)
        + building_loan_demand_milli_.capacity() * sizeof(EconomyAmount)
        + country_blockade_ppm_.capacity() * sizeof(std::uint32_t)
        + company_stake_head_.capacity() * sizeof(std::uint32_t)
        + company_stake_next_.capacity() * sizeof(std::uint32_t)
        + stake_dividend_milli_.capacity() * sizeof(EconomyAmount)
        + bankrupt_scratch_.capacity() * sizeof(std::vector<BuildingId>)
        + expansion_best_.capacity() * sizeof(std::size_t)
        + expansion_best_score_.capacity() * sizeof(EconomyAmount)
        + pop_hot_.capacity() * sizeof(PopHotRow)
        + pop_hot_offsets_.capacity() * sizeof(std::uint32_t);
}

EconomyAmount EconomySystem::apply_blockade_scale(EconomyAmount shipped, CountryId country) const noexcept {
    if (shipped <= 0 || !country.valid()) return shipped;
    const auto ci = static_cast<std::size_t>(country.value());
    if (ci >= country_blockade_ppm_.size()) return shipped;
    const auto blockade = country_blockade_ppm_[ci];
    if (blockade == 0u) return shipped;
    if (blockade >= static_cast<std::uint32_t>(ppm_scale)) return 0;
    return mul_div_nonnegative(shipped, static_cast<EconomyAmount>(ppm_scale - blockade), ppm_scale);
}

std::vector<std::pair<std::size_t,std::size_t>> EconomySystem::deterministic_subpartitions(    std::size_t count, std::size_t desired_shards) noexcept {
    if (count == 0 || desired_shards <= 1) return {{0, count}};
    const std::size_t shards = std::min(desired_shards, count);
    std::vector<std::pair<std::size_t,std::size_t>> out;
    out.reserve(shards);
    const std::size_t base = count / shards;
    const std::size_t rem = count % shards;
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < shards; ++i) {
        const std::size_t sz = base + (i < rem ? 1 : 0);
        out.emplace_back(cursor, cursor + sz);
        cursor += sz;
    }
    return out;
}

void EconomySystem::run_weekly(World& world, JobSystem& jobs, EconomyTickProfile* profile) {
    if (index_.market_count() != world.markets.size() || !index_.current_for(world.pops, world.buildings)) rebuild_indices(world);
    if (building_remaining_.size() != world.buildings.size()) building_remaining_.assign(world.buildings.size(), 0u);
    if (building_throughput_ppm_.size() != world.buildings.size())
        building_throughput_ppm_.assign(world.buildings.size(), static_cast<std::int32_t>(ppm_scale));
    if (building_loan_demand_milli_.size() != world.buildings.size())
        building_loan_demand_milli_.assign(world.buildings.size(), 0);
    const auto total_begin=Clock::now();
    produced_milli_ = 0; demanded_milli_ = 0; fulfilled_milli_ = 0; spoilage_milli_ = 0;
    carry_cost_milli_ = 0;
    if(profile) profile->money_before=monetary_balance_sheet(world);
    if(profile) profile->material_before=material_balance_sheet(world);
    std::size_t workers_used=1u;
    auto run_phase=[&](auto&& fn, std::chrono::nanoseconds* out){
        const auto begin=Clock::now();
        const auto stats=fn();
        const auto end=Clock::now();
        workers_used=std::max(workers_used,stats.workers_used);
        if(out!=nullptr)*out=std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin);
    };
    // Natural population growth leads the week: grown POPs join this week's
    // employment, production and demand passes.
    run_phase([&]{return population_growth(world);},nullptr);
    world.banks.run_weekly(world);
    run_phase([&]{return settle_investment_pool_contributions(world);},nullptr);
    run_phase([&]{return gather_pop_hot(world,jobs);},nullptr);
    run_phase([&]{return employment(world,jobs);},profile?&profile->employment:nullptr);
    run_phase([&]{return production(world,jobs);},profile?&profile->production:nullptr);
    run_phase([&]{return consumption(world,jobs);},profile?&profile->consumption:nullptr);
    run_phase([&]{return trade(world);},nullptr);
    run_phase([&]{return update_prices(world,jobs);},profile?&profile->prices:nullptr);
    if(profile){
        profile->material_after=material_balance_sheet(world);
        // Material closed-loop audit (world totals; trade nets to zero across markets):
        //   dInventory == produced - fulfilled - spoilage
        const EconomyAmount d_inventory = saturating_sub(
            profile->material_after.total_inventory_milli,
            profile->material_before.total_inventory_milli);
        const EconomyAmount consumed = saturating_sub(
            saturating_sub(produced_milli_, fulfilled_milli_), spoilage_milli_);
        profile->produced_milli = produced_milli_;
        profile->demanded_milli = demanded_milli_;
        profile->fulfilled_milli = fulfilled_milli_;
        profile->spoilage_milli = spoilage_milli_;
        profile->unexplained_material_delta_milli = saturating_sub(d_inventory, consumed);
    }
    run_phase([&]{return settlement(world,jobs);},profile?&profile->settlement:nullptr);
    // Commit gathered POP cash before FX revaluation diagnostics so the
    // numeraire change is measured on the actual post-settlement balances.
    run_phase([&]{return scatter_pop_hot(world,jobs);},nullptr);
    const auto pre_fx_sheet = profile ? monetary_balance_sheet(world) : MonetaryBalanceSheet{};
    world.currencies.evaluate_monetary_sovereignty(world.countries);
    // Monetary metals are ordinary market goods: bullion supply gluts and
    // shortages move the anchor that metallic standards track. No bound good
    // keeps the classical fallback constants.
    const auto gold_good = definitions_.find_good_by_monetary_metal(metal_gold_key);
    const auto silver_good = definitions_.find_good_by_monetary_metal(metal_silver_key);
    const auto gold_base = gold_good.valid()
        ? definitions_.good(gold_good).base_price_milli : EconomyPrice{0};
    const auto silver_base = silver_good.valid()
        ? definitions_.good(silver_good).base_price_milli : EconomyPrice{0};
    world.currencies.update_exchange_rates(
        metal_market_price_ppm(world, gold_good, gold_base, kFallbackGoldPricePpm),
        metal_market_price_ppm(world, silver_good, silver_base, kFallbackSilverPricePpm),
        &world.countries);
    const auto post_fx_sheet = profile ? monetary_balance_sheet(world) : MonetaryBalanceSheet{};
    EconomyAmount central_issuance = 0;
    for (const auto& cur : world.currencies.currencies()) {
        const auto seigniorage = cur.seigniorage_accrued_milli;
        if (seigniorage > 0 && cur.sovereign_leader.valid()) {
            world.countries.add_treasury_milli(cur.sovereign_leader, seigniorage);
            central_issuance = saturating_add(central_issuance,
                world.currencies.convert(seigniorage, cur.key, default_currency_key));
            world.currencies.clear_seigniorage(cur.key);
        }
    }
    run_phase([&]{return construction(world);},nullptr);
    if(profile){
        profile->money_after=monetary_balance_sheet(world);
        profile->monetary_delta_milli=saturating_sub(
            profile->money_after.total_milli,profile->money_before.total_milli);
        profile->private_credit_delta_milli=saturating_sub(
            pre_fx_sheet.bank_deposit_liabilities_milli,
            profile->money_before.bank_deposit_liabilities_milli);
        profile->central_issuance_milli=central_issuance;
        profile->currency_revaluation_milli=saturating_sub(
            post_fx_sheet.total_milli,pre_fx_sheet.total_milli);
        profile->inventory_carry_cost_milli=carry_cost_milli_;
        // The carry leg is money destroyed (clearing sink), so it enters the
        // identity with a plus: delta - credit - issuance - revaluation + carry.
        profile->unexplained_monetary_delta_milli=saturating_add(
            saturating_sub(saturating_sub(saturating_sub(profile->monetary_delta_milli,
                profile->private_credit_delta_milli),profile->central_issuance_milli),
                profile->currency_revaluation_milli),profile->inventory_carry_cost_milli);
        profile->total=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-total_begin);
        profile->workers_used=workers_used;
    }
}

} // namespace thunder
