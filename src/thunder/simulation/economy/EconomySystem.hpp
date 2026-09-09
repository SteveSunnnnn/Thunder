#pragma once
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/economy/MarketEntityIndex.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {
class World;

struct MonetaryBalanceSheet {
    EconomyAmount pop_deposits_milli = 0;
    EconomyAmount building_deposits_milli = 0;
    EconomyAmount market_clearing_milli = 0;
    EconomyAmount treasury_deposits_milli = 0;
    EconomyAmount company_deposits_milli = 0;
    EconomyAmount investment_pool_deposits_milli = 0;
    EconomyAmount bank_deposit_liabilities_milli = 0;
    EconomyAmount bank_credit_assets_milli = 0;
    EconomyAmount total_milli = 0;
};

// Material closed-loop snapshot: totals across all markets/goods.
struct MaterialBalanceSheet {
    EconomyAmount total_supply_milli = 0;
    EconomyAmount total_demand_milli = 0;
    EconomyAmount total_inventory_milli = 0;
    EconomyAmount total_shortage_milli = 0;
    EconomyAmount net_material_milli = 0; // supply+inventory - demand - shortage
};

struct EconomyTickProfile {
    std::chrono::nanoseconds employment{};
    std::chrono::nanoseconds production{};
    std::chrono::nanoseconds consumption{};
    std::chrono::nanoseconds prices{};
    std::chrono::nanoseconds settlement{};
    std::chrono::nanoseconds total{};
    std::size_t workers_used = 1u;
    MonetaryBalanceSheet money_before{};
    MonetaryBalanceSheet money_after{};
    EconomyAmount monetary_delta_milli = 0;
    EconomyAmount private_credit_delta_milli = 0;
    EconomyAmount central_issuance_milli = 0;
    EconomyAmount currency_revaluation_milli = 0;
    // Inventory carrying cost booked against market clearing accounts this
    // tick (the physical sink for idle-stock finance). Subtracted alongside
    // the other explained legs so the closed-loop residual stays exact.
    EconomyAmount inventory_carry_cost_milli = 0;
    EconomyAmount unexplained_monetary_delta_milli = 0;
    // Material closed-loop: goods are created by production, removed by
    // fulfilled demand (consumption + intermediate input draws) and destroyed
    // only by an explicit, accounted spoilage sink (the storage-cap overflow).
    // Identity (world totals, trade nets to zero across markets):
    //   dInventory == produced - fulfilled - spoilage
    // so unexplained_material_delta_milli must be ~0 for a closed loop.
    MaterialBalanceSheet material_before{};
    MaterialBalanceSheet material_after{};
    EconomyAmount produced_milli = 0;   // sum of this tick's supply
    EconomyAmount demanded_milli = 0;   // sum of this tick's demand
    EconomyAmount fulfilled_milli = 0;  // sum of min(available, demand)
    EconomyAmount spoilage_milli = 0;   // storage-cap overflow sink
    EconomyAmount unexplained_material_delta_milli = 0;
};

class EconomySystem {
public:
    explicit EconomySystem(const EconomyDefinitions& definitions) : definitions_(definitions) {}

    // One cache-friendly row per POP for the weekly scan phases. POPs are not
    // stored grouped by market, so dereferencing the eight SoA hot columns
    // through a market's id list is a stride-N scatter; the tick gathers the
    // working set into market-contiguous rows once, runs the phases on them,
    // and scatters back at the end.
    struct PopHotRow {
        PopulationCount population;
        PopulationCount employed;
        BuildingId employer;
        NeedProfileId need_profile;
        EconomyAmount income_milli;
        EconomyAmount cash_milli;
        std::int32_t sol_milli;
        std::uint16_t literacy_permyriad;
        std::uint16_t qualification_permyriad;
        std::uint16_t province_r16;
    };
    static_assert(sizeof(PopHotRow) == 48u);


    void rebuild_indices(const World& world);
    void run_weekly(World& world, JobSystem& jobs, EconomyTickProfile* profile = nullptr);
    // Weekly natural population growth driven by standard of living. Runs at
    // the head of run_weekly so this week's labour and demand include it.
    JobDispatchStats population_growth(World& world);
    [[nodiscard]] static MonetaryBalanceSheet monetary_balance_sheet(const World& world) noexcept;
    [[nodiscard]] static MaterialBalanceSheet material_balance_sheet(const World& world) noexcept;
    [[nodiscard]] const MarketEntityIndex& index() const noexcept { return index_; }
    [[nodiscard]] const EconomyDefinitions& definitions() const noexcept { return definitions_; }
    [[nodiscard]] std::size_t scratch_memory_bytes() const noexcept;

private:
    void ensure_market_scratch(std::size_t markets);
    JobDispatchStats gather_pop_hot(World& world, JobSystem& jobs);
    JobDispatchStats scatter_pop_hot(World& world, JobSystem& jobs);
    JobDispatchStats employment(World& world, JobSystem& jobs);
    JobDispatchStats production(World& world, JobSystem& jobs);
    JobDispatchStats consumption(World& world, JobSystem& jobs);
    // Inter-market arbitrage: ships carried inventory from low-price glut
    // markets to high-price shortage markets while the price gap exceeds the
    // transport band. Serial — the goods×markets product is tiny.
    JobDispatchStats trade(World& world);
    JobDispatchStats update_prices(World& world, JobSystem& jobs);
    JobDispatchStats settlement(World& world, JobSystem& jobs);
    JobDispatchStats settle_investment_pool_contributions(World& world);
    // Spends country investment pools on expanding the best-utilized,
    // best-performing buildings so pool funds re-enter the real economy.
    JobDispatchStats construction(World& world);
    // Deterministic single-market sub-partition helper: when the world has
    // only 1-3 markets the market-level grain cannot saturate cores.
    // This splits the entity range by stable ID hash (not worker ID) so the
    // result is identical for 1 vs N workers. Exposed for testing.
    static std::vector<std::pair<std::size_t,std::size_t>> deterministic_subpartitions(
        std::size_t count, std::size_t desired_shards) noexcept;
    // Scales a cross-border shipment by the shipper's blockade throttling:
    // a fully blockaded country pushes nothing through, a free one all of it.
    [[nodiscard]] EconomyAmount apply_blockade_scale(EconomyAmount shipped, CountryId country) const noexcept;

    const EconomyDefinitions& definitions_;
    MarketEntityIndex index_;
    std::vector<EconomyAmount> market_tax_milli_;
    std::vector<EconomyAmount> market_dividend_milli_;
    std::vector<EconomyAmount> market_gdp_milli_;
    std::vector<std::uint64_t> market_population_;
    std::vector<EconomyAmount> country_gdp_milli_;
    std::vector<EconomyAmount> country_nominal_gdp_milli_;
    std::vector<std::uint64_t> country_population_;
    std::vector<std::uint64_t> profile_population_;
    std::vector<EconomyAmount> profile_basket_cost_milli_;
    std::vector<PopulationCount> building_remaining_;
    // Hoisted flat base-price row so update_prices avoids the bounds-checked
    // throwing good() accessor per (market, good, tick).
    std::vector<EconomyPrice> base_price_milli_;
    // Per (market, good) share of demand fulfilled after clearing against
    // supply plus stock. Also feeds next tick's input availability so input
    // shortages throttle throughput.
    std::vector<std::int64_t> market_fulfillment_ppm_;
    // Per (market, good) fraction of this tick's newly produced output that
    // actually sold. Carried inventory is anonymous market stock; new output
    // receives first claim on current demand in this Thunder 1.0 settlement slice.
    std::vector<std::int64_t> market_sales_ppm_;
    // Per (market, need profile) basket-weighted fulfillment used to ration
    // consumption payments and standard of living.
    std::vector<std::int64_t> profile_fulfillment_ppm_;
    // Per-market credit drawn by loss-making buildings this tick; settled
    // serially against investment pools and treasuries after the parallel pass.
    std::vector<EconomyAmount> market_loan_demand_milli_;
    std::vector<EconomyAmount> building_loan_demand_milli_;
    // R10: per-market hopeless buildings (at credit limit AND operating at a
    // loss) collected in settlement, resolved serially afterwards (downsize or
    // destroy). Each market slot is written by exactly one thread.
    std::vector<std::vector<BuildingId>> bankrupt_scratch_;
    // R11: company ownership CSR over grand-strategy stakes. stake_next_
    // chains stakes by owned building (stake order preserved, deterministic);
    // stake_head_[building] is the chain head (UINT32_MAX when none);
    // stake_dividend_milli_ accumulates each stake's dividend leg in the
    // parallel phase for a serial company-cash fold afterwards.
    std::vector<std::uint32_t> company_stake_head_;
    std::vector<std::uint32_t> company_stake_next_;
    std::vector<EconomyAmount> stake_dividend_milli_;
    // Material-flow accumulators captured during the price-clearing phase so the
    // closed-loop identity can be verified against the persisted inventory.
    EconomyAmount produced_milli_ = 0;
    EconomyAmount demanded_milli_ = 0;
    EconomyAmount fulfilled_milli_ = 0;
    EconomyAmount spoilage_milli_ = 0;
    // Per-market scratch for parallel accumulation (each market written by
    // exactly one thread — no contention).  Summed after parallel_for.
    std::vector<EconomyAmount> market_produced_scratch_;
    std::vector<EconomyAmount> market_demanded_scratch_;
    std::vector<EconomyAmount> market_fulfilled_scratch_;
    std::vector<EconomyAmount> market_spoilage_scratch_;
    // Per-market inventory carrying cost (R12): idle stock is financed, so
    // glut markets bleed carry against their clearing account each week.
    std::vector<EconomyAmount> market_carry_scratch_;
    // Actual per-building throughput chosen in production after input
    // shortages. Settlement consumes this exact value rather than recreating
    // an unconstrained theoretical production plan.
    std::vector<std::int32_t> building_throughput_ppm_;
    // Per-country blockade throttling for the trade phase: the maximum
    // blockade_efficiency_ppm over sea zones the country controls. Recomputed
    // at trade start; cross-border shipments scale by (1 - blockade).
    std::vector<std::uint32_t> country_blockade_ppm_;
    // Inventory carrying cost accumulated by this tick's price phase (the
    // finance sink for idle stock, booked against market clearing).
    EconomyAmount carry_cost_milli_ = 0;
    // Reused market index lists for the trade phase (importers price-desc,
    // exporters price-asc, id-asc tie break).
    std::vector<std::uint32_t> trade_importers_;
    std::vector<std::uint32_t> trade_exporters_;
    // Reused scratch arrays for the construction phase (eliminates per-tick allocation)
    std::vector<std::size_t> expansion_best_;
    std::vector<EconomyAmount> expansion_best_score_;
    // Market-contiguous POP working set (see PopHotRow) with per-market
    // offsets; rebuilt each tick, never persisted.
    std::vector<PopHotRow> pop_hot_;
    std::vector<std::uint32_t> pop_hot_offsets_;
};

} // namespace thunder
