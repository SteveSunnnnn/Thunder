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

namespace {
// Weekly inventory carrying rate: 1000 ppm (0.1%) of settled inventory value.
inline constexpr std::int64_t kInventoryCarryRatePpm = 1000;
} // namespace


JobDispatchStats EconomySystem::trade(World& world) {
    const std::size_t markets = world.markets.size();
    const std::size_t goods = world.markets.good_count();
    if (world.trade_policies.size() != world.countries.size())
        world.trade_policies.resize(world.countries.size());
    world.trade_policies.begin_week();

    // R4: naval blockade throttles the victim's trade. A blockaded sea zone
    // closes its controller's ports: cross-border shipments scale by
    // (1 - max blockade over zones the country controls). Local scratch only.
    if (country_blockade_ppm_.size() != world.countries.size())
        country_blockade_ppm_.assign(world.countries.size(), 0u);
    else
        std::fill(country_blockade_ppm_.begin(), country_blockade_ppm_.end(), 0u);
    for (const auto& zone : world.grand_strategy.sea_zones()) {
        if (zone.blockade_efficiency_ppm == 0 || !zone.controller.valid()) continue;
        const auto ci = static_cast<std::size_t>(zone.controller.value());
        if (ci >= country_blockade_ppm_.size()) continue;
        country_blockade_ppm_[ci] = std::max(country_blockade_ppm_[ci], zone.blockade_efficiency_ppm);
    }

    const auto routes = world.grand_strategy.trade_routes();
    bool has_authored_routes = false;
    for (const auto& route : routes) if (route.active && route.quantity_milli > 0) { has_authored_routes = true; break; }

    const auto ship_leg = [&](MarketId exp, MarketId imp, GoodId good,
                              EconomyAmount capacity) -> EconomyAmount {
        if (!exp.valid() || !imp.valid() || exp == imp || good.value() >= goods ||
            exp.value() >= markets || imp.value() >= markets || capacity <= 0) return 0;
        const auto gi = static_cast<std::size_t>(good.value());
        auto exp_inventory = world.markets.inventory_row(exp);
        auto imp_inventory = world.markets.inventory_row(imp);
        auto imp_shortage = world.markets.shortage_row(imp);
        if (exp_inventory[gi] <= 0 || imp_shortage[gi] <= 0) return 0;

        const CurrencyKey exp_cur = world.markets.currency_key(exp);
        const CurrencyKey imp_cur = world.markets.currency_key(imp);
        const CountryId exp_country = world.markets.owner(exp);
        const CountryId imp_country = world.markets.owner(imp);
        const auto raw_exp_price = world.markets.price(exp, good);
        const auto raw_imp_price = world.markets.price(imp, good);
        const auto exp_price_in_imp = world.currencies.convert_price(raw_exp_price, exp_cur, imp_cur);
        const auto transport_in_imp = world.currencies.convert_price(
            std::max<EconomyPrice>(1, base_price_milli_[gi] / 10), default_currency_key, imp_cur);
        const auto& imp_policy = world.trade_policies.get(imp_country);
        const auto& exp_policy = world.trade_policies.get(exp_country);
        const auto import_price_tax = mul_div_nonnegative(exp_price_in_imp, imp_policy.import_tariff_ppm, ppm_scale);
        const auto export_price_tax = mul_div_nonnegative(exp_price_in_imp, exp_policy.export_tariff_ppm, ppm_scale);
        const auto landed_price = saturating_add(exp_price_in_imp,
            saturating_add(transport_in_imp, saturating_add(import_price_tax, export_price_tax)));
        if (raw_imp_price <= landed_price) return 0;

        EconomyAmount shipped = std::min({capacity, exp_inventory[gi], imp_shortage[gi]});
        if (imp_country.valid() && exp_country.valid() && imp_country != exp_country) {
            shipped = std::min(shipped, world.trade_policies.available_capacity(imp_country));
            shipped = std::min(shipped, world.trade_policies.available_capacity(exp_country));
            // Blockade wedge: the blockaded side can only push/pull the
            // unthrottled share through its ports.
            shipped = apply_blockade_scale(shipped, imp_country);
            shipped = apply_blockade_scale(shipped, exp_country);
        }
        if (shipped <= 0) return 0;
        if (imp_country.valid() && exp_country.valid() && imp_country != exp_country) {
            (void)world.trade_policies.reserve_capacity(imp_country, shipped);
            (void)world.trade_policies.reserve_capacity(exp_country, shipped);
        }

        exp_inventory[gi] = saturating_sub(exp_inventory[gi], shipped);
        imp_inventory[gi] = saturating_add(imp_inventory[gi], shipped);
        imp_shortage[gi] = saturating_sub(imp_shortage[gi], shipped);

        const auto imp_price_in_exp = world.currencies.convert_price(raw_imp_price, imp_cur, exp_cur);
        const auto exp_invoice_price = saturating_add(raw_exp_price,
            saturating_sub(imp_price_in_exp, raw_exp_price) / 2);
        const auto exp_invoice = money_for_quantity(shipped, exp_invoice_price);
        const auto imp_cost = world.currencies.convert(exp_invoice, exp_cur, imp_cur);
        const auto import_tariff = mul_div_nonnegative(imp_cost, imp_policy.import_tariff_ppm, ppm_scale);
        const auto export_tariff = mul_div_nonnegative(exp_invoice, exp_policy.export_tariff_ppm, ppm_scale);

        world.markets.add_clearing_cash(imp, -saturating_add(imp_cost, import_tariff));
        world.markets.add_clearing_cash(exp, saturating_sub(exp_invoice, export_tariff));
        if (imp_country.valid() && import_tariff > 0) world.countries.add_treasury_milli(imp_country, import_tariff);
        if (exp_country.valid() && export_tariff > 0) world.countries.add_treasury_milli(exp_country, export_tariff);

        if (imp_cur != exp_cur) world.currencies.record_fx_flow(imp_cur, exp_cur, exp_invoice);
        if (imp_country.valid() && exp_country.valid() && imp_country != exp_country) {
            const auto numeraire_value = world.currencies.convert(exp_invoice, exp_cur, default_currency_key);
            world.countries.add_balance_of_payments_milli(imp_country, -numeraire_value);
            world.countries.add_balance_of_payments_milli(exp_country, numeraire_value);
            // Closed-loop foreign reserves: importer loses reserves, exporter gains them (FX settlement)
            if (imp_cur != exp_cur) {
                world.countries.add_foreign_reserves_milli(exp_country, numeraire_value);
                world.countries.add_foreign_reserves_milli(imp_country, -numeraire_value);
            }
        }
        return shipped;
    };

    if (has_authored_routes) {
        // Authored routes are already stable-ID ordered. Complexity is O(R),
        // independent of the number of unrelated markets.
        auto routes_mut = world.grand_strategy.trade_routes_mut();
        for (auto& route : routes_mut) {
            if (!route.active || route.quantity_milli <= 0) continue;
            const auto capacity = mul_div_nonnegative(route.quantity_milli,
                std::max<std::uint16_t>(1, route.level), 1);
            const auto shipped = ship_leg(route.source, route.destination, route.good, capacity);
            if (shipped > 0) {
                // Profitable and moving goods: ramp volume (+5% per week)
                route.quantity_milli = std::min<EconomyAmount>(
                    saturating_add(route.quantity_milli, std::max<EconomyAmount>(1, route.quantity_milli / 20)),
                    100'000'000LL);
            } else {
                // Zero volume or unprofitable: ramp down volume (-10% per week)
                route.quantity_milli = (route.quantity_milli * 9) / 10;
                if (route.quantity_milli < 10) {
                    route.quantity_milli = 0;
                    route.active = false;
                }
            }
        }
        return JobDispatchStats{};
    }

    // Compatibility/open-market mode: when content declares no routes, use
    // deterministic global arbitrage. Price ordering is normalized across
    // currencies before comparisons.
    std::vector<std::pair<std::uint32_t, EconomyPrice>> import_sorted, export_sorted;
    for (std::size_t gi = 0; gi < goods; ++gi) {
        trade_importers_.clear();
        trade_exporters_.clear();
        for (std::size_t mi = 0; mi < markets; ++mi) {
            const MarketId market{static_cast<MarketId::rep_type>(mi)};
            if (world.markets.shortage(market, GoodId{static_cast<GoodId::rep_type>(gi)}) > 0) {
                trade_importers_.push_back(static_cast<std::uint32_t>(mi));
            }
            if (world.markets.inventory(market, GoodId{static_cast<GoodId::rep_type>(gi)}) > 0) {
                trade_exporters_.push_back(static_cast<std::uint32_t>(mi));
            }
        }
        if (trade_importers_.empty() || trade_exporters_.empty()) continue;
        const auto price_of = [&](std::uint32_t mi) {
            const MarketId market{static_cast<MarketId::rep_type>(mi)};
            return world.currencies.convert_price(world.markets.price(
                market, GoodId{static_cast<GoodId::rep_type>(gi)}),
                world.markets.currency_key(market), default_currency_key);
        };
        import_sorted.clear();
        for (auto mi : trade_importers_) import_sorted.emplace_back(mi, price_of(mi));
        export_sorted.clear();
        for (auto mi : trade_exporters_) export_sorted.emplace_back(mi, price_of(mi));
        // Highest-price shortage markets buy first; cheapest glut markets
        // sell first; id order breaks ties so allocation is deterministic.
        std::sort(import_sorted.begin(), import_sorted.end(),
            [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second > b.second;
                return a.first < b.first;
            });
        std::sort(export_sorted.begin(), export_sorted.end(),
            [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second < b.second;
                return a.first < b.first;
            });
        for (const auto& [mi, _] : import_sorted) {
            const MarketId imp{static_cast<MarketId::rep_type>(mi)};
            for (const auto& [ej, __] : export_sorted) {
                const MarketId exp{static_cast<MarketId::rep_type>(ej)};
                if (imp != exp) (void)ship_leg(exp, imp,
                    GoodId{static_cast<GoodId::rep_type>(gi)}, std::numeric_limits<EconomyAmount>::max());
            }
        }
    }
    return JobDispatchStats{};
}

JobDispatchStats EconomySystem::update_prices(World& world, JobSystem& jobs) {
    std::fill(market_produced_scratch_.begin(), market_produced_scratch_.end(), EconomyAmount{0});
    std::fill(market_demanded_scratch_.begin(), market_demanded_scratch_.end(), EconomyAmount{0});
    std::fill(market_fulfilled_scratch_.begin(), market_fulfilled_scratch_.end(), EconomyAmount{0});
    std::fill(market_spoilage_scratch_.begin(), market_spoilage_scratch_.end(), EconomyAmount{0});
    std::fill(market_carry_scratch_.begin(), market_carry_scratch_.end(), EconomyAmount{0});
    const auto stats = jobs.parallel_for(world.markets.size(), 1u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                auto prices = world.markets.price_row(market);
                const auto supply = world.markets.supply_row(market);
                const auto demand = world.markets.demand_row(market);
                auto inventory = world.markets.inventory_row(market);
                auto shortage = world.markets.shortage_row(market);
                auto fulfillment = std::span<std::int64_t>{
                    market_fulfillment_ppm_.data() + mi * prices.size(), prices.size()};
                auto sales = std::span<std::int64_t>{
                    market_sales_ppm_.data() + mi * prices.size(), prices.size()};
                // Per-market material-flow sums feed the world-level closed-loop
                // audit. Trade (which runs before this phase) only shifts
                // inventory between markets, so these sums net to zero across
                // markets and the world identity stays exact.
                EconomyAmount produced = 0, demanded = 0, fulfilled = 0, spoilage = 0, carry = 0;
                for (std::size_t gi = 0; gi < prices.size(); ++gi) {
                    const auto base = base_price_milli_[gi];
                    // Market clearing: demand is matched against this tick's
                    // supply plus carried stock; the remainder is rationed and
                    // the unsold surplus becomes next tick's inventory.
                    const EconomyAmount available = saturating_add(supply[gi], inventory[gi]);
                    const EconomyAmount wanted = demand[gi];
                    const EconomyAmount f = std::min(available, wanted);
                    const EconomyAmount liquidity = std::max<EconomyAmount>({supply[gi], wanted, 1000});
                    const EconomyAmount surplus = saturating_sub(available, f);
                    const EconomyAmount cap = mul_div_nonnegative(liquidity, 4, 1);
                    const EconomyAmount kept = std::min(surplus, cap);
                    // The storage-cap overflow is the only legitimate physical
                    // sink: goods produced/stored but beyond capacity spoil. It
                    // is now measured explicitly instead of silently vanishing.
                    spoilage = saturating_add(spoilage, saturating_sub(surplus, kept));
                    inventory[gi] = kept;
                    shortage[gi] = saturating_sub(wanted, f);
                    fulfillment[gi] = wanted > 0
                        ? std::min(mul_div_nonnegative(f, ppm_scale, wanted), ppm_scale)
                        : ppm_scale;
                    const EconomyAmount current_output_sold = std::min(supply[gi], wanted);
                    sales[gi] = supply[gi] > 0
                        ? std::min(mul_div_nonnegative(current_output_sold, ppm_scale, supply[gi]), ppm_scale)
                        : 0;
                    // Price pressure follows the net stock position: unfilled
                    // demand pushes up, accumulated inventory pushes down.
                    const auto pressure = signed_ratio_ppm(saturating_sub(wanted, available), liquidity);
                    const auto target_factor = std::clamp<std::int64_t>(ppm_scale + pressure, 200'000, 5'000'000);
                    const EconomyPrice target = mul_div_nonnegative(base, target_factor, ppm_scale);
                    EconomyPrice delta = saturating_sub(target, prices[gi]);
                    // Correct the /4 truncation dead band so small gaps still close.
                    EconomyPrice step = delta / 4;
                    if (step == 0) step = delta > 0 ? 1 : (delta < 0 ? -1 : 0);
                    prices[gi] = saturating_add(prices[gi], step);
                    prices[gi] = std::clamp<EconomyPrice>(prices[gi], std::max<EconomyPrice>(1, base / 5), mul_div_nonnegative(base, 5, 1));
                    // Accumulate physical flows for the closed-loop identity.
                    produced = saturating_add(produced, supply[gi]);
                    demanded = saturating_add(demanded, wanted);
                    fulfilled = saturating_add(fulfilled, f);

                    // R12: inventory carrying cost. Idle stock is financed, so
                    // the settled (post-clearing) inventory value bleeds 0.1%
                    // per week against the market clearing account — the
                    // explicit, audited finance sink for warehousing. Glut
                    // markets pay for hoarding; empty markets pay nothing.
                    carry = saturating_add(carry, mul_div_nonnegative(
                        mul_div_nonnegative(inventory[gi], prices[gi], economy_scale),
                        kInventoryCarryRatePpm, ppm_scale));
                }
                // Per-market scratch — each market written by exactly one
                // thread (disjoint range), so no atomic needed.
                market_produced_scratch_[mi] = produced;
                market_demanded_scratch_[mi] = demanded;
                market_fulfilled_scratch_[mi] = fulfilled;
                market_spoilage_scratch_[mi] = spoilage;
                market_carry_scratch_[mi] = carry;
                world.markets.add_clearing_cash(market, -carry);
            }
        });
    // Sequential reduction — safe, no contention.
    produced_milli_ = 0; demanded_milli_ = 0; fulfilled_milli_ = 0; spoilage_milli_ = 0;
    carry_cost_milli_ = 0;
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        produced_milli_ = saturating_add(produced_milli_, market_produced_scratch_[mi]);
        demanded_milli_ = saturating_add(demanded_milli_, market_demanded_scratch_[mi]);
        fulfilled_milli_ = saturating_add(fulfilled_milli_, market_fulfilled_scratch_[mi]);
        spoilage_milli_ = saturating_add(spoilage_milli_, market_spoilage_scratch_[mi]);
        carry_cost_milli_ = saturating_add(carry_cost_milli_, market_carry_scratch_[mi]);
    }
    return stats;
}



JobDispatchStats EconomySystem::settlement(World& world, JobSystem& jobs) {
    std::fill(market_tax_milli_.begin(), market_tax_milli_.end(), EconomyAmount{0});
    std::fill(market_dividend_milli_.begin(), market_dividend_milli_.end(), EconomyAmount{0});
    std::fill(market_gdp_milli_.begin(), market_gdp_milli_.end(), EconomyAmount{0});
    std::fill(market_population_.begin(), market_population_.end(), std::uint64_t{0});
    std::fill(building_loan_demand_milli_.begin(), building_loan_demand_milli_.end(), EconomyAmount{0});
    if (bankrupt_scratch_.size() != world.markets.size()) bankrupt_scratch_.assign(world.markets.size(), {});
    else for (auto& slot : bankrupt_scratch_) slot.clear();
    // R11: rebuild the per-building company-ownership chains for this tick.
    // Stakes are content-level (few); buildings are many. A head/next CSR
    // keeps the parallel dividend split O(stakes of this building).
    {
        const auto stakes = world.grand_strategy.ownership_stakes();
        company_stake_head_.assign(world.buildings.size(), std::numeric_limits<std::uint32_t>::max());
        company_stake_next_.assign(stakes.size(), std::numeric_limits<std::uint32_t>::max());
        if (stake_dividend_milli_.size() != stakes.size()) stake_dividend_milli_.assign(stakes.size(), 0);
        else std::fill(stake_dividend_milli_.begin(), stake_dividend_milli_.end(), EconomyAmount{0});
        const auto company_count = world.grand_strategy.companys().size();
        for (std::uint32_t si = 0; si < stakes.size(); ++si) {
            const auto& stake = stakes[si];
            const auto bi = static_cast<std::size_t>(stake.building.value());
            if (!stake.building.valid() || bi >= world.buildings.size() ||
                !world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi)))
                continue;
            if (!stake.owner_company.valid() ||
                static_cast<std::size_t>(stake.owner_company.value()) >= company_count)
                continue;
            if (stake.share_ppm == 0) continue;
            if (company_stake_head_[bi] == std::numeric_limits<std::uint32_t>::max()) {
                company_stake_head_[bi] = si;
            } else {
                std::uint32_t tail = company_stake_head_[bi];
                while (company_stake_next_[tail] != std::numeric_limits<std::uint32_t>::max())
                    tail = company_stake_next_[tail];
                company_stake_next_[tail] = si;
            }
        }
    }
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();
    const auto building_employees = world.buildings.employees_all();
    const auto building_provinces = world.buildings.provinces();
    const auto building_methods = world.buildings.production_methods();
    auto wage_offers = world.buildings.wage_offers_mut();
    auto building_cash = world.buildings.cash_mut();
    auto building_profit = world.buildings.last_profits_mut();
    const auto type_defs = definitions_.building_types();
    const auto input_flows = definitions_.input_flows();
    const auto output_flows = definitions_.output_flows();
    const auto profile_defs = definitions_.need_profiles();
    const auto need_flows = definitions_.need_flows();

    const auto stats = jobs.parallel_for(world.markets.size(), 1u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                const auto prices = world.markets.price_row(market);
                EconomyAmount tax_total = 0;
                EconomyAmount dividend_total = 0;
                EconomyAmount gdp_total = 0;
                EconomyAmount clearing_delta = 0;
                std::uint64_t population_total = 0;
                const std::size_t profile_count = profile_defs.size();
                auto* basket_cost = profile_basket_cost_milli_.data() + mi * profile_count;
                auto* profile_fulfillment_row = profile_fulfillment_ppm_.data() + mi * profile_count;
                const auto good_fulfillment = std::span<const std::int64_t>{
                    market_fulfillment_ppm_.data() + mi * prices.size(), prices.size()};
                const auto good_sales = std::span<const std::int64_t>{
                    market_sales_ppm_.data() + mi * prices.size(), prices.size()};
                for (std::size_t profile_index = 0; profile_index < profile_count; ++profile_index) {
                    EconomyAmount total = 0;
                    std::int64_t weight_total = 0;
                    std::int64_t weighted_fulfillment = 0;
                    const auto& profile = profile_defs[profile_index];
                    for (const auto& need : need_flows.subspan(profile.flow_begin, profile.flow_count)) {
                        total = saturating_add(total, money_for_quantity(need.quantity_milli_per_1000_people, prices[need.good.value()]));
                        weight_total += need.quantity_milli_per_1000_people;
                        weighted_fulfillment = saturating_add(
                            weighted_fulfillment,
                            mul_div_nonnegative(need.quantity_milli_per_1000_people,
                                                good_fulfillment[need.good.value()], 1));
                    }
                    basket_cost[profile_index] = total;
                    profile_fulfillment_row[profile_index] = weight_total > 0
                        ? std::clamp<std::int64_t>(weighted_fulfillment / weight_total, 0, ppm_scale)
                        : ppm_scale;
                }
                const CountryId owner = world.markets.owner(market);
                const auto& tax_policy = owner.valid() ? world.countries.tax_policy(owner) : TaxPolicy{};
                EconomyAmount loan_demand = 0;

                // --- BUILDING SETTLEMENT: revenue/cost accounting + wage dynamics + dividends ---
                for (const auto b : index_.buildings(market)) {
                    const auto bi = static_cast<std::size_t>(b.value());
                    EconomyAmount revenue = 0, cost = 0;
                    const auto workers = building_employees[bi];
                    const auto& type = type_defs[building_types[bi].value()];
                    auto input_begin = type.input_begin;
                    auto input_count = type.input_count;
                    auto output_begin = type.output_begin;
                    auto output_count = type.output_count;
                    std::int32_t throughput_ppm = building_throughput_ppm_[bi];
                    // R3: market-access wedge for this building's state. Poor
                    // integration discounts realized output prices and marks up
                    // input prices by up to 25% (V3 MAPI behaviour): remote
                    // buildings earn less, pay more, and anchor lower wages.
                    std::int64_t mapi_sell_ppm = ppm_scale;
                    std::int64_t mapi_buy_ppm = ppm_scale;
                    const auto b_prov = building_provinces[bi];
                    if (b_prov.valid() && b_prov.value() < world.geography.province_count()) {
                        const auto b_state = world.geography.province_state(b_prov);
                        if (b_state.valid()) {
                            const auto mapi = static_cast<std::int64_t>(
                                world.geography.state_market_access_ppm(b_state));
                            const auto wedge = (ppm_scale - mapi) / 4;
                            mapi_sell_ppm = ppm_scale - wedge;
                            mapi_buy_ppm = ppm_scale + wedge;
                        }
                    }
                    const auto method_id = building_methods[bi];
                    if (method_id.valid()) {
                        const auto& method = definitions_.production_method(method_id);
                        if (method.building_type == building_types[bi]) {
                            input_begin = method.input_begin;
                            input_count = method.input_count;
                            output_begin = method.output_begin;
                            output_count = method.output_count;
                        }
                    }
                    for (const auto& flow : input_flows.subspan(input_begin, input_count)) {
                        auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        q = mul_div_nonnegative(q, throughput_ppm, ppm_scale);
                        q = mul_div_nonnegative(q, good_fulfillment[flow.good.value()], ppm_scale);
                        const EconomyPrice buy_price = static_cast<EconomyPrice>(
                            mul_div_nonnegative(prices[flow.good.value()], mapi_buy_ppm, ppm_scale));
                        cost = saturating_add(cost, money_for_quantity(q, buy_price));
                    }
                    for (const auto& flow : output_flows.subspan(output_begin, output_count)) {
                        auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        q = mul_div_nonnegative(q, throughput_ppm, ppm_scale);
                        q = mul_div_nonnegative(q, good_sales[flow.good.value()], ppm_scale);
                        const EconomyPrice sell_price = static_cast<EconomyPrice>(
                            mul_div_nonnegative(prices[flow.good.value()], mapi_sell_ppm, ppm_scale));
                        revenue = saturating_add(revenue, money_for_quantity(q, sell_price));
                    }

                    // Two-sided settlement: input purchases credit the market
                    // account and realized output sales debit it. Buildings no
                    // longer receive theoretical revenue from no counterparty.
                    const EconomyAmount goods_profit = saturating_sub(revenue, cost);
                    building_cash[bi] = saturating_add(building_cash[bi], goods_profit);
                    clearing_delta = saturating_sub(clearing_delta, goods_profit);

                    // Clamp building cash to credit limit. The overdraft is
                    // booked as credit demand and settled against the owner's
                    // investment pool/treasury after the parallel pass, so it
                    // is paired with an explicit funding leg below.
                    if (building_cash[bi] < building_credit_limit_milli) {
                        const auto requested_credit = building_credit_limit_milli - building_cash[bi];
                        building_loan_demand_milli_[bi] = requested_credit;
                        loan_demand = saturating_add(loan_demand, requested_credit);
                        building_cash[bi] = building_credit_limit_milli;
                    }

                    // Record accounting profit for UI/AI (operating profit = goods profit - wage obligations)
                    const EconomyAmount wages_accounting = mul_div_nonnegative(
                        static_cast<EconomyAmount>(workers), wage_offers[bi], 1);
                    const EconomyAmount display_profit = saturating_sub(goods_profit, wages_accounting);
                    building_profit[bi] = display_profit;

                    // R10 candidate: pinned at the credit limit AND operating at
                    // a loss. No private lender would roll this over — it is
                    // resolved serially after the parallel pass (downsize or
                    // destroy). Cash above the limit always gets another week.
                    if (building_cash[bi] <= building_credit_limit_milli && display_profit < 0)
                        bankrupt_scratch_[mi].push_back(b);

                    // Real GDP by the Production / Value Added Approach (增加值法):
                    // Value Added = Realized Output Revenue - Realized Intermediate Input Cost = goods_profit.
                    // In national accounting (SNA): Value Added = Compensation of Employees (Wages) + Gross Operating Surplus.
                    // This strictly reflects real market commodity prices, outputs, and intermediate costs.
                    const EconomyAmount value_added = std::max<EconomyAmount>(0, goods_profit);
                    gdp_total = saturating_add(gdp_total, value_added);



                    // 1. CAPITAL ACCUMULATION & DIVIDENDS:
                    // Maintain realistic cash reserve cap (4 weeks of full wages). Excess cash is paid out as dividends.
                    const std::uint64_t capacity = static_cast<std::uint64_t>(type.workers_per_level) * building_levels[bi];
                    const EconomyAmount cash_reserve_cap = std::max<EconomyAmount>(
                        100'000, mul_div_nonnegative(
                            mul_div_nonnegative(static_cast<EconomyAmount>(capacity), wage_offers[bi], 1), 4, 1));
                    if (building_cash[bi] > cash_reserve_cap) {
                        const EconomyAmount excess = building_cash[bi] - cash_reserve_cap;
                        // Payout 50% of surplus cash as dividends
                        const EconomyAmount dividend = excess / 2;
                        const EconomyAmount div_tax = mul_div_nonnegative(dividend, tax_policy.dividends_tax_ppm, ppm_scale);
                        tax_total = saturating_add(tax_total, div_tax);
                        const EconomyAmount net_dividend = saturating_sub(dividend, div_tax);
                        // R11: company-owned share of the dividend goes to the
                        // owning companies (pro-rata over their stakes, capped
                        // at full ownership); the remainder stays in the
                        // national investment pool. Rounding dust stays pooled.
                        EconomyAmount pooled_dividend = net_dividend;
                        if (bi < company_stake_head_.size() &&
                            company_stake_head_[bi] != std::numeric_limits<std::uint32_t>::max()) {
                            const auto stakes = world.grand_strategy.ownership_stakes();
                            std::uint32_t company_total = 0;
                            for (auto si = company_stake_head_[bi];
                                 si != std::numeric_limits<std::uint32_t>::max();
                                 si = company_stake_next_[si]) {
                                company_total = std::min<std::uint32_t>(
                                    1'000'000u, company_total + stakes[si].share_ppm);
                            }
                            if (company_total > 0u) {
                                const EconomyAmount company_take = mul_div_nonnegative(
                                    net_dividend, company_total, 1'000'000u);
                                EconomyAmount distributed = 0;
                                for (auto si = company_stake_head_[bi];
                                     si != std::numeric_limits<std::uint32_t>::max();
                                     si = company_stake_next_[si]) {
                                    const auto take = mul_div_nonnegative(
                                        company_take, stakes[si].share_ppm, company_total);
                                    stake_dividend_milli_[si] = saturating_add(
                                        stake_dividend_milli_[si], take);
                                    distributed = saturating_add(distributed, take);
                                }
                                pooled_dividend = saturating_sub(net_dividend, distributed);
                            }
                        }
                        dividend_total = saturating_add(dividend_total, pooled_dividend);
                        building_cash[bi] = saturating_sub(building_cash[bi], dividend);
                    }

                    // 2. WAGES ANCHORED TO MARGINAL REVENUE PRODUCT:
                    // The offer moves toward revenue per worker; a building
                    // paying above MRP cuts, a profitable one below MRP raises
                    // and thereby wins workers in next week's competitive
                    // employment pass. No workers means the wage decays to the
                    // survival floor.
                    {
                        const EconomyPrice current_wage = wage_offers[bi];
                        const EconomyAmount rev_per_worker = workers > 0u
                            ? mul_div_nonnegative(revenue, 1, static_cast<EconomyAmount>(workers))
                            : 0;
                        const EconomyPrice target = std::max<EconomyPrice>(100, static_cast<EconomyPrice>(rev_per_worker));
                        // The content boundary allows int64 fixed-point wages;
                        // subtract through the saturating helper so extreme
                        // authored values cannot invoke signed overflow.
                        EconomyPrice delta = static_cast<EconomyPrice>(
                            saturating_sub(target, current_wage) / 4);
                        const EconomyPrice step = std::clamp<EconomyPrice>(std::max<EconomyPrice>(current_wage / 20, 5), 5, 200);
                        delta = std::clamp<EconomyPrice>(delta, -step, step);
                        if (delta == 0 && target != current_wage) delta = target > current_wage ? 1 : -1;
                        wage_offers[bi] = std::max<EconomyPrice>(100, saturating_add(current_wage, delta));
                    }
                }

                // --- POP SETTLEMENT: consumption spending deduction + taxation ---
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], index_.pops(market).size());
                for (auto& row : rows) {
                    population_total += row.population;

                    // CLOSED-LOOP: Compute consumption cost and deduct from POP cash.
                    // Payment is rationed to the fulfilled share of the basket:
                    // shortages mean POPs buy less, not pay phantom prices.
                    // Index is clamped at gather time; fall back to a neutral
                    // fulfillment if no profile bucket exists for this POP.
                    const auto profile_index = (profile_count > 0u &&
                                                static_cast<std::size_t>(row.need_profile.value()) < profile_count)
                                                   ? static_cast<std::size_t>(row.need_profile.value())
                                                   : 0u;
                    const auto profile_fulfillment = profile_count > 0u ? profile_fulfillment_row[profile_index] : ppm_scale;
                    const EconomyAmount basket = mul_div_nonnegative(
                        static_cast<EconomyAmount>(row.population),
                        profile_count > 0u ? basket_cost[profile_index] : 0, 1000);
                    const EconomyAmount rationed_basket = mul_div_nonnegative(basket, profile_fulfillment, ppm_scale);
                    // POP pays for consumption from their cash (cannot go below 0)
                    const EconomyAmount consumption_payment = std::min(rationed_basket, std::max<EconomyAmount>(0, row.cash_milli));
                    row.cash_milli = saturating_sub(row.cash_milli, consumption_payment);
                    clearing_delta = saturating_add(clearing_delta, consumption_payment);

                    // CLOSED-LOOP: Multi-tax deductions from POP cash into national treasury.
                    // Compute resistance-adjusted tax BEFORE deducting from cash so
                    // only the amount the treasury actually receives is removed from
                    // the POP — the difference stays in the real economy.
                    const EconomyAmount income_tax = mul_div_nonnegative(
                        row.income_milli, tax_policy.income_tax_ppm, ppm_scale);
                    const EconomyAmount per_capita_tax = mul_div_nonnegative(
                        static_cast<EconomyAmount>(row.population),
                        tax_policy.per_capita_tax_ppm, ppm_scale);
                    const EconomyAmount consumption_tax = mul_div_nonnegative(
                        consumption_payment, tax_policy.consumption_tax_ppm, ppm_scale);

                    const EconomyAmount total_tax = saturating_add(
                        saturating_add(income_tax, per_capita_tax), consumption_tax);
                    // Can only pay tax from available cash
                    const EconomyAmount max_tax = std::min(total_tax, std::max<EconomyAmount>(0, row.cash_milli));

                    // State resistance reduces local effective tax collection
                    EconomyAmount effective_tax = max_tax;
                    if (row.province_r16 != 0xFFFFu) {
                        const ProvinceId prov{row.province_r16};
                        if (prov.valid() && prov.value() < world.geography.province_count()) {
                            const auto st = world.geography.province_state(prov);
                            if (st.valid()) {
                                const auto res_ppm = world.geography.state_resistance_ppm(st);
                                if (res_ppm > 0) {
                                    const auto tax_eff_ppm = ppm_scale - mul_div_nonnegative(res_ppm, 750'000u, ppm_scale);
                                    effective_tax = mul_div_nonnegative(max_tax, tax_eff_ppm, ppm_scale);
                                }
                            }
                        }
                    }
                    // Only deduct what the treasury actually collects —
                    // resistance portion stays with the POP (bribe/informal economy).
                    row.cash_milli = saturating_sub(row.cash_milli, effective_tax);
                    tax_total = saturating_add(tax_total, effective_tax);

                    // Continuous Qualification accumulation:
                    const auto qual_delta = (row.literacy_permyriad / 1000) + (row.sol_milli > 10'000 ? 5 : 1);
                    row.qualification_permyriad = static_cast<std::uint16_t>(
                        std::min<std::uint32_t>(10'000u, row.qualification_permyriad + qual_delta));

                    // 3. REALISTIC STANDARD OF LIVING (EMA SMOOTHING):
                    // Quality of life has inertia (assets, savings, long-term conditions)
                    // and drops under rationing: unfulfilled needs are not bought.
                    const EconomyAmount disposable = saturating_sub(row.income_milli, effective_tax);
                    std::int64_t target_sol = basket > 0 ? signed_ratio_ppm(disposable, basket) / 100 : 0;
                    target_sol = mul_div_nonnegative(std::max<std::int64_t>(target_sol, 0), profile_fulfillment, ppm_scale);
                    const auto clamped_target = static_cast<std::int32_t>(std::clamp<std::int64_t>(target_sol, 0, 100'000));
                    row.sol_milli = static_cast<std::int32_t>((static_cast<std::int64_t>(row.sol_milli) * 3LL + clamped_target) / 4LL);
                }
                market_tax_milli_[mi] = tax_total;
                market_dividend_milli_[mi] = dividend_total;
                market_gdp_milli_[mi] = gdp_total;
                market_population_[mi] = population_total;
                market_loan_demand_milli_[mi] = loan_demand;
                world.markets.set_clearing_cash(
                    market, saturating_add(world.markets.clearing_cash(market), clearing_delta));
            }
        });
    if (country_gdp_milli_.size() != world.countries.size()) country_gdp_milli_.assign(world.countries.size(), 0);
    else std::fill(country_gdp_milli_.begin(), country_gdp_milli_.end(), EconomyAmount{0});
    if (country_nominal_gdp_milli_.size() != world.countries.size()) country_nominal_gdp_milli_.assign(world.countries.size(), 0);
    else std::fill(country_nominal_gdp_milli_.begin(), country_nominal_gdp_milli_.end(), EconomyAmount{0});
    if (country_population_.size() != world.countries.size()) country_population_.assign(world.countries.size(), 0u);
    else std::fill(country_population_.begin(), country_population_.end(), std::uint64_t{0});
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        const auto owner = world.markets.owner(market);
        if (!owner.valid()) continue;
        const auto ci = static_cast<std::size_t>(owner.value());
        if (ci >= world.countries.size()) continue;

        // Convert local market value added into Global Real Numeraire (Gold Parity Standard):
        const CurrencyKey market_cur = world.markets.currency_key(market);
        const EconomyAmount gdp_in_numeraire = world.currencies.convert(
            market_gdp_milli_[mi], market_cur, default_currency_key);
        country_gdp_milli_[ci] = saturating_add(country_gdp_milli_[ci], gdp_in_numeraire);
        country_nominal_gdp_milli_[ci] = saturating_add(country_nominal_gdp_milli_[ci], market_gdp_milli_[mi]);
        country_population_[ci] += market_population_[mi];
        // Credit settlement: every amount already credited to an overdrawn
        // building has an equal funding leg. Private pools and the treasury
        // fund first; any uncovered amount becomes an explicit debit on this
        // market's stable clearing account (the transitional lender of last
        // resort) instead of disappearing as phantom money.
        if (market_loan_demand_milli_[mi] > 0) {
            const auto bank = world.banks.primary_bank(owner, world.markets.currency_key(market));
            for (const auto building : index_.buildings(market)) {
                const auto bi = static_cast<std::size_t>(building.value());
                const auto requested = building_loan_demand_milli_[bi];
                if (requested <= 0) continue;
                EconomyAmount covered = bank.valid()
                    ? world.banks.fund_building(bank, building, requested) : 0;
                auto remaining = saturating_sub(requested, covered);
                if (remaining > 0) {
                    const auto from_pool = world.grand_strategy.withdraw_investment_pool_funds(owner, remaining);
                    covered = saturating_add(covered, from_pool);
                    remaining = saturating_sub(remaining, from_pool);
                }
                if (remaining > 0) {
                    const auto treasury_milli = std::max<EconomyAmount>(0, world.countries.treasury_milli(owner));
                    const auto from_treasury = std::min(remaining, treasury_milli);
                    if (from_treasury > 0) {
                        world.countries.add_treasury_milli(owner, -from_treasury);
                        covered = saturating_add(covered, from_treasury);
                        remaining = saturating_sub(remaining, from_treasury);
                    }
                }
                // The stable market account remains the explicit lender of
                // last resort only for credit the regulated bank and funded
                // sectors could not supply.
                if (remaining > 0) world.markets.add_clearing_cash(market, -remaining);
            }
        }
        if (market_tax_milli_[mi] != 0) {
            // CLOSED-LOOP: Tax revenue credited to treasury
            world.countries.add_treasury_milli(owner, market_tax_milli_[mi]);
        }
        if (market_dividend_milli_[mi] > 0) {
            // CLOSED-LOOP: Net corporate dividends channeled to investment pool for reinvestment
            world.grand_strategy.add_investment_pool_funds(owner, market_dividend_milli_[mi]);
        }
    }
    // R11: fold stake dividends into company operating cash, in stable stake
    // order. Company cash participates in the monetary balance sheet and is
    // checksummed + persisted with the grand-strategy store.
    {
        const auto stakes = world.grand_strategy.ownership_stakes();
        auto companies = world.grand_strategy.companys_mut();
        for (std::size_t si = 0; si < stakes.size() && si < stake_dividend_milli_.size(); ++si) {
            const auto amount = stake_dividend_milli_[si];
            if (amount <= 0) continue;
            const auto ci = static_cast<std::size_t>(stakes[si].owner_company.value());
            if (ci < companies.size())
                companies[ci].cash_milli = saturating_add(companies[ci].cash_milli, amount);
        }
    }
    for (std::size_t ci = 0; ci < world.countries.size(); ++ci) {
        const CountryId country{static_cast<CountryId::rep_type>(ci)};
        world.countries.set_population(country, static_cast<double>(country_population_[ci]));
        world.countries.set_gdp(country, static_cast<double>(country_gdp_milli_[ci]) / static_cast<double>(economy_scale));
        world.countries.set_nominal_gdp_milli(country, country_nominal_gdp_milli_[ci]);

        // Service Sovereign Debt interest:
        const auto debt_service = world.countries.weekly_debt_service_milli(country);
        if (debt_service > 0) {
            const auto treasury_avail = std::max<EconomyAmount>(0, world.countries.treasury_milli(country));
            const auto paid = std::min(treasury_avail, debt_service);
            if (paid > 0) {
                world.countries.add_treasury_milli(country, -paid);
                const auto bank_interest = world.banks.receive_sovereign_interest(country, paid);
                const auto saver_interest = saturating_sub(paid, bank_interest);
                if (saver_interest > 0) world.grand_strategy.add_investment_pool_funds(country, saver_interest);
            }
            if (paid == debt_service) {
                if (world.countries.default_weeks(country) > 0) {
                    world.countries.set_default_weeks(country, world.countries.default_weeks(country) - 1);
                }
            } else {
                // Insolvent: enter sovereign default
                world.countries.set_default_weeks(country, world.countries.default_weeks(country) + 1);
                world.countries.set_credit_rating(country, CreditRating::D);
                world.countries.add_prestige(country, -5.0);
                // R8: restructuring after a month of missed service. The debt
                // is written down 30%: domestic banks absorb their pro-rata
                // share through equity (possibly going insolvent — the bank
                // crisis channel), the saver pool absorbs the rest via its
                // cash. The exclusion clock resets to a year; each fully
                // serviced week counts it back down.
                if (world.countries.default_weeks(country) >= 4) {
                    constexpr std::int32_t kWriteOffPpm = 300'000;
                    const auto debt = world.countries.national_debt_milli(country);
                    const auto write_off = mul_div_nonnegative(debt, kWriteOffPpm, ppm_scale);
                    if (write_off > 0) {
                        const auto bank_part = world.banks.write_down_sovereign_bonds(country, write_off);
                        const auto pool_part = saturating_sub(write_off, bank_part);
                        if (pool_part > 0)
                            world.grand_strategy.withdraw_investment_pool_funds(country, pool_part);
                        world.countries.add_national_debt_milli(country,
                            -saturating_add(bank_part, pool_part));
                        world.countries.set_default_weeks(country, 52);
                        world.countries.evaluate_credit_rating(country);
                    }
                }
            }
        }
        world.countries.evaluate_credit_rating(country);
    }
    // R10: bankruptcy resolution. A building pinned at the credit limit while
    // operating at a loss cannot roll over privately: multi-level buildings
    // shrink by one level per week (liquidation recovery sells the level's
    // capital to market clearing); a one-level building dies only when its
    // bank debt has demonstrably failed (non-performing or charged off), so
    // ordinary credit smoothing never liquidates a merely young business.
    // The residual cash balance is absorbed by market clearing so the monetary
    // identity stays exact, bank loans on dead buildings are charged off by
    // next week's bank run, and POP employer references are cleared in one
    // bounded pass.
    {
        std::vector<std::uint32_t> destroyed_raw;
        auto building_cash_serial = world.buildings.cash_mut();
        for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
            if (bankrupt_scratch_[mi].empty()) continue;
            const MarketId market{static_cast<MarketId::rep_type>(mi)};
            for (const auto b : bankrupt_scratch_[mi]) {
                const auto bi = static_cast<std::size_t>(b.value());
                if (bi >= world.buildings.size() ||
                    !world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi)))
                    continue;
                const auto level = world.buildings.level(b);
                if (level > 1u) {
                    // Liquidation: shed one level, recover 5 weeks of wages
                    // worth of capital from market clearing.
                    const auto recovery = mul_div_nonnegative(
                        mul_div_nonnegative(static_cast<EconomyAmount>(
                            type_defs[building_types[bi].value()].workers_per_level),
                            wage_offers[bi], 1), 5, 1);
                    world.buildings.set_level(b, static_cast<std::uint16_t>(level - 1u));
                    if (recovery > 0) {
                        building_cash_serial[bi] = saturating_add(building_cash_serial[bi], recovery);
                        world.markets.add_clearing_cash(market, -recovery);
                    }
                } else {
                    const auto loan_status = world.banks.building_loan_status(b);
                    if (loan_status != BankLoanStatus::NonPerforming &&
                        loan_status != BankLoanStatus::Defaulted)
                        continue; // credit smoothing, not insolvency: survive
                    // Terminal: clearing absorbs the residual books, then die.
                    world.markets.add_clearing_cash(market, building_cash_serial[bi]);
                    building_cash_serial[bi] = 0;
                    world.buildings.destroy(b);
                    destroyed_raw.push_back(static_cast<std::uint32_t>(bi));
                }
            }
        }
        if (!destroyed_raw.empty()) {
            auto pop_employers = world.pops.employers_mut();
            for (std::size_t pi = 0; pi < pop_employers.size(); ++pi) {
                const auto emp = pop_employers[pi];
                if (!emp.valid()) continue;
                for (const auto raw : destroyed_raw) {
                    if (emp.value() == raw) { pop_employers[pi] = BuildingId{}; break; }
                }
            }
        }
    }
    return stats;
}

JobDispatchStats EconomySystem::settle_investment_pool_contributions(World& world) {
    const auto pools = world.grand_strategy.investment_pools();
    for (std::size_t index = 0; index < pools.size(); ++index) {
        const CountryId country = pools[index].country;
        const EconomyAmount requested = pools[index].weekly_contribution_milli;
        if (requested <= 0 || !country.valid()
            || static_cast<std::size_t>(country.value()) >= world.countries.size()) continue;
        const EconomyAmount available = std::max<EconomyAmount>(
            0, world.countries.treasury_milli(country));
        const EconomyAmount transferred = std::min(requested, available);
        if (transferred <= 0) continue;
        world.countries.add_treasury_milli(country, -transferred);
        world.grand_strategy.add_investment_pool_funds(
            InvestmentPoolId{static_cast<InvestmentPoolId::rep_type>(index)}, transferred);
    }
    return JobDispatchStats{};
}

JobDispatchStats EconomySystem::construction(World& world) {
    // 1. Process queued construction projects (upgrades, PM transitions, monuments, manual expansions).
    // Construction consumes real materials from the project's market; the
    // basket is empty-content-safe (money-only legacy path).
    world.construction.tick_weekly(world, definitions_.construction_inputs());

    const auto building_markets = world.buildings.markets();
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();
    const auto building_employees = world.buildings.employees_all();
    const auto wage_offers = world.buildings.wage_offers();
    auto building_cash = world.buildings.cash_mut();
    const auto type_defs = definitions_.building_types();
    constexpr std::uint16_t level_cap = 1000u;
    constexpr std::size_t expansions_per_week = 8u;
    // Construction cost of one level: 20 weeks of full wages for its staff.
    constexpr EconomyAmount construction_weeks = 20;

    // Single pass picking each country's best expansion candidate: the most
    // profitable-per-slot building that is at least 90% utilized (id order
    // breaks ties). Only well-used, profitable capacity attracts investment.
    const std::size_t countries = world.countries.size();
    if (expansion_best_.size() != countries) {
        expansion_best_.assign(countries, std::numeric_limits<std::size_t>::max());
        expansion_best_score_.assign(countries, std::numeric_limits<EconomyAmount>::min());
    } else {
        std::fill(expansion_best_.begin(), expansion_best_.end(), std::numeric_limits<std::size_t>::max());
        std::fill(expansion_best_score_.begin(), expansion_best_score_.end(), std::numeric_limits<EconomyAmount>::min());
    }
    auto& best = expansion_best_;
    auto& best_score = expansion_best_score_;
    for (std::size_t bi = 0; bi < world.buildings.size(); ++bi) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi))) continue;
        if (building_levels[bi] >= level_cap) continue;
        const auto market = building_markets[bi];
        if (!market.valid()) continue;
        const auto owner = world.markets.owner(market);
        if (!owner.valid()) continue;
        const auto ci = static_cast<std::size_t>(owner.value());
        if (ci >= countries) continue;
        const std::uint64_t capacity = static_cast<std::uint64_t>(type_defs[building_types[bi].value()].workers_per_level) * building_levels[bi];
        if (capacity == 0u) continue;
        if (static_cast<std::uint64_t>(building_employees[bi]) * 10u < capacity * 9u) continue;
        const EconomyAmount score = world.buildings.last_profit(BuildingId{static_cast<BuildingId::rep_type>(bi)}) / static_cast<EconomyAmount>(capacity);
        if (score > best_score[ci] || (score == best_score[ci] && best[ci] == std::numeric_limits<std::size_t>::max())) {
            best_score[ci] = score;
            best[ci] = bi;
        }
    }

    for (std::size_t ci = 0; ci < countries; ++ci) {
        const CountryId owner{static_cast<CountryId::rep_type>(ci)};
        if (best[ci] == std::numeric_limits<std::size_t>::max()) continue;
        for (std::size_t n = 0; n < expansions_per_week; ++n) {
            const auto bi = best[ci];
            if (building_levels[bi] >= level_cap) break;
            const auto workers_per_level = type_defs[building_types[bi].value()].workers_per_level;
            const EconomyAmount cost = mul_div_nonnegative(
                mul_div_nonnegative(static_cast<EconomyAmount>(workers_per_level), wage_offers[bi], 1),
                construction_weeks, 1);
            if (world.grand_strategy.investment_pool_cash(owner) < cost) break;
            // R6: expansion enters the construction queue instead of appearing
            // instantly. Skip when an expansion for this building is already
            // queued so one project cannot stack-duplicate itself.
            bool already_queued = false;
            for (const auto& project : world.construction.projects()) {
                if (project.kind == ConstructionKind::ExpandBuilding &&
                    project.target_building == BuildingId{static_cast<BuildingId::rep_type>(bi)}) {
                    already_queued = true;
                    break;
                }
            }
            if (already_queued) break;
            if (world.grand_strategy.withdraw_investment_pool_funds(owner, cost) < cost) break;
            // Seed money moves pool -> building contractor cash as construction
            // escrow. The queue records the full bill and draws the building's
            // escrow first, then pool and treasury, so the bill is charged
            // exactly once while its materials flow through this tick's market.
            building_cash[bi] = saturating_add(building_cash[bi], cost);
            world.construction.enqueue_expansion(owner,
                BuildingId{static_cast<BuildingId::rep_type>(bi)}, 100u, cost);
        }
    }

    // R11: companies spend their operating cash on their own expansions. Same
    // escrow mechanics as the national pool path: seed the building, queue the
    // bill. Capped so one flush week cannot enqueue an unbounded backlog.
    {
        constexpr std::size_t company_expansions_per_week = 2u;
        const auto stakes = world.grand_strategy.ownership_stakes();
        const auto companies = world.grand_strategy.companys();
        for (std::size_t ci = 0; ci < companies.size(); ++ci) {
            const CompanyId company{static_cast<CompanyId::rep_type>(ci)};
            const auto& record = companies[ci];
            if (!record.active || !record.country.valid()) continue;
            std::size_t enqueued = 0;
            // Deterministic: scan stakes in index order, keep the best-owned
            // candidate by profit-per-slot at >= 90% utilization.
            std::size_t owned_best = std::numeric_limits<std::size_t>::max();
            EconomyAmount owned_best_score = std::numeric_limits<EconomyAmount>::min();
            for (const auto& stake : stakes) {
                if (stake.owner_company != company || stake.share_ppm == 0) continue;
                const auto bi = static_cast<std::size_t>(stake.building.value());
                if (bi >= world.buildings.size() ||
                    !world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi)))
                    continue;
                if (building_levels[bi] >= level_cap) continue;
                const auto market = building_markets[bi];
                if (!market.valid() || world.markets.owner(market) != record.country) continue;
                const std::uint64_t capacity = static_cast<std::uint64_t>(
                    type_defs[building_types[bi].value()].workers_per_level) * building_levels[bi];
                if (capacity == 0u) continue;
                if (static_cast<std::uint64_t>(building_employees[bi]) * 10u < capacity * 9u) continue;
                const EconomyAmount score = world.buildings.last_profit(
                    BuildingId{static_cast<BuildingId::rep_type>(bi)}) /
                    static_cast<EconomyAmount>(capacity);
                if (score > owned_best_score) {
                    owned_best_score = score;
                    owned_best = bi;
                }
            }
            while (owned_best != std::numeric_limits<std::size_t>::max() &&
                   enqueued < company_expansions_per_week) {
                const auto bi = owned_best;
                if (building_levels[bi] >= level_cap) break;
                bool already_queued = false;
                for (const auto& project : world.construction.projects()) {
                    if (project.kind == ConstructionKind::ExpandBuilding &&
                        project.target_building == BuildingId{static_cast<BuildingId::rep_type>(bi)}) {
                        already_queued = true;
                        break;
                    }
                }
                if (already_queued) break;
                const auto workers_per_level = type_defs[building_types[bi].value()].workers_per_level;
                const EconomyAmount cost = mul_div_nonnegative(
                    mul_div_nonnegative(static_cast<EconomyAmount>(workers_per_level), wage_offers[bi], 1),
                    construction_weeks, 1);
                if (world.grand_strategy.company_cash(company) < cost) break;
                world.grand_strategy.add_company_cash(company, -cost);
                building_cash[bi] = saturating_add(building_cash[bi], cost);
                world.construction.enqueue_expansion(record.country,
                    BuildingId{static_cast<BuildingId::rep_type>(bi)}, 100u, cost);
                ++enqueued;
                // One expansion per company per week keeps the weekly pass
                // bounded; the best candidate re-wins next week if still best.
                break;
            }
        }
    }
    return JobDispatchStats{};
}
} // namespace thunder
