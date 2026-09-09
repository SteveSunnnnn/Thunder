#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

class CountryStore;

enum class MonetaryStandard : std::uint8_t {
    GoldStandard = 0,    // Pure gold standard with specie-flow mechanism
    SilverStandard = 1,  // Silver standard
    Bimetallism = 2,     // Bimetallic standard (gold & silver with Gresham's law)
    FiatFloating = 3     // Unbacked paper / credit currency
};

struct CurrencyRecord {
    CurrencyKey key = 0;
    std::string name;
    MonetaryStandard standard = MonetaryStandard::GoldStandard;
    CountryId sovereign_leader{};               // Sovereign nation with monetary hegemony
    double leader_prestige = 0.0;               // Power score of current sovereign leader
    
    // Metallic parities in milligrams of fine metal per currency unit
    // Standard baseline: 1000 mg gold = 1 standard gold unit, 15500 mg silver = 15.5:1 ratio
    std::uint32_t gold_parity_mg = 1000;
    std::uint32_t silver_parity_mg = 15500;

    EconomyPrice exchange_rate_ppm = 1'000'000; // 1'000'000 ppm = 1.0 base unit
    EconomyPrice target_rate_ppm = 1'000'000;   // Legal mint parity rate
    EconomyAmount trade_demand_milli = 0;       // Foreign currency demanded this tick
    EconomyAmount trade_supply_milli = 0;       // Foreign currency offered this tick
    EconomyAmount seigniorage_accrued_milli = 0;// Seigniorage collected for sovereign leader
    std::uint64_t specie_export_mg = 0;         // Physical specie (gold/silver) exported this tick
    std::uint64_t specie_import_mg = 0;         // Physical specie (gold/silver) imported this tick
    bool convertibility_suspended = false;      // True if gold/silver redemption is suspended (specie drain)
    std::vector<EconomyPrice> history_rates_ppm;// Rolling weekly exchange rate history for UI charts
};

class CurrencyStore {
public:
    CurrencyStore();

    void clear() noexcept;
    void register_currency(CurrencyKey key, std::string_view name,
                           MonetaryStandard standard = MonetaryStandard::GoldStandard,
                           std::uint32_t gold_parity_mg = 1000,
                           std::uint32_t silver_parity_mg = 15500,
                           EconomyPrice initial_rate_ppm = 1'000'000);

    [[nodiscard]] std::size_t size() const noexcept { return currencies_.size(); }
    [[nodiscard]] bool contains(CurrencyKey key) const noexcept;
    [[nodiscard]] std::span<const CurrencyRecord> currencies() const noexcept { return currencies_; }

    [[nodiscard]] EconomyPrice exchange_rate_ppm(CurrencyKey key) const noexcept;
    void set_exchange_rate_ppm(CurrencyKey key, EconomyPrice rate_ppm) noexcept;
    void set_target_rate_ppm(CurrencyKey key, EconomyPrice rate_ppm) noexcept;
    [[nodiscard]] EconomyPrice target_rate_ppm(CurrencyKey key) const noexcept;
    [[nodiscard]] std::span<const EconomyPrice> exchange_rate_history(CurrencyKey key) const noexcept;

    [[nodiscard]] MonetaryStandard monetary_standard(CurrencyKey key) const noexcept;
    void set_monetary_standard(CurrencyKey key, MonetaryStandard standard) noexcept;

    // Currency taxonomy. Metal numeraires (gold/silver) are first-class currencies
    // used as the FX anchor; class is derived from the canonical metal keys so it
    // survives save round-trip without a disk field.
    [[nodiscard]] CurrencyClass currency_class(CurrencyKey key) const noexcept;
    [[nodiscard]] bool is_metal_currency(CurrencyKey key) const noexcept;
    [[nodiscard]] static constexpr CurrencyKey gold_metal_key() noexcept { return metal_gold_key; }
    [[nodiscard]] static constexpr CurrencyKey silver_metal_key() noexcept { return metal_silver_key; }

    [[nodiscard]] std::uint32_t gold_parity_mg(CurrencyKey key) const noexcept;
    void set_gold_parity_mg(CurrencyKey key, std::uint32_t mg) noexcept;

    [[nodiscard]] std::uint32_t silver_parity_mg(CurrencyKey key) const noexcept;
    void set_silver_parity_mg(CurrencyKey key, std::uint32_t mg) noexcept;

    [[nodiscard]] CountryId sovereign_leader(CurrencyKey key) const noexcept;
    void set_sovereign_leader(CurrencyKey key, CountryId leader, double prestige) noexcept;

    [[nodiscard]] EconomyAmount seigniorage_accrued_milli(CurrencyKey key) const noexcept;
    void clear_seigniorage(CurrencyKey key) noexcept;
    void set_seigniorage_accrued_milli(CurrencyKey key, EconomyAmount milli) noexcept;

    // Per-tick FX flow accumulators. These participate in `checksum()`, so
    // saves must round-trip them or the restored world fails verification.
    void set_trade_demand_milli(CurrencyKey key, EconomyAmount milli) noexcept;
    void set_trade_supply_milli(CurrencyKey key, EconomyAmount milli) noexcept;

    [[nodiscard]] std::uint64_t specie_export_mg(CurrencyKey key) const noexcept;
    [[nodiscard]] std::uint64_t specie_import_mg(CurrencyKey key) const noexcept;
    [[nodiscard]] bool convertibility_suspended(CurrencyKey key) const noexcept;
    void set_convertibility_suspended(CurrencyKey key, bool suspended) noexcept;

    // Manual player-driven toggle of specie (gold/silver) convertibility.
    // Only meaningful for metallic standards (Gold/Silver/Bimetallism); a fiat
    // currency has no specie to suspend, so the call is a no-op returning false.
    [[nodiscard]] bool toggle_convertibility(CurrencyKey key) noexcept;

    // Convert an amount in `from` currency to `to` currency:
    // Amount_to = (Amount_from * Rate_from) / Rate_to
    [[nodiscard]] EconomyAmount convert(EconomyAmount amount_milli,
                                        CurrencyKey from,
                                        CurrencyKey to) const noexcept;

    // Convert a unit price in `from` currency to `to` currency:
    // Price_to = (Price_from * Rate_from) / Rate_to
    [[nodiscard]] EconomyPrice convert_price(EconomyPrice price_milli,
                                             CurrencyKey from,
                                             CurrencyKey to) const noexcept;

    // Commodity-side valuation of one unit of `coin`: what the coin is worth
    // as metal (melt value), derived from its mint parity and the current
    // market metal prices. This is the "foreign money circulates as a
    // commodity" settlement path; `convert()` is the unit-of-account path.
    //   Gold/Silver standard -> own-metal content x market metal price
    //   Bimetallism          -> max(gold melt, silver melt) — the melt side of
    //                           Gresham's law (the legal circulation rate in
    //                           update_exchange_rates tracks the cheaper side)
    //   FiatFloating         -> 0 (no metal content; settles at FX rate only)
    [[nodiscard]] EconomyAmount coin_commodity_value_milli(
        CurrencyKey coin, EconomyPrice gold_price_ppm,
        EconomyPrice silver_price_ppm) const noexcept;

    // Record international trade payment leg creating FX demand for `bought_currency`
    // paid using `sold_currency`.
    void record_fx_flow(CurrencyKey sold_currency, CurrencyKey bought_currency,
                        EconomyAmount volume_in_bought_milli) noexcept;

    // Evaluate monetary sovereignty for all currency zones based on member nations' prestige & power
    void evaluate_monetary_sovereignty(const CountryStore& countries) noexcept;

    // Clear weekly FX trade flows and deterministically adjust exchange rates
    // according to metallic standards (Gold Points, Gresham's law, physical specie shipments)
    // and trade balance.
    void update_exchange_rates(EconomyPrice gold_price_ppm = 1'000'000,
                               EconomyPrice silver_price_ppm = 64'516,
                               CountryStore* countries = nullptr) noexcept;

    // Validate the authoritative currency table and its country references
    // before a tick or save boundary. Exchange-rate history and per-tick flow
    // counters are derived/UI state but remain bounded here as well.
    [[nodiscard]] bool validate(std::size_t country_count) const noexcept;

    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] std::size_t find_index(CurrencyKey key) const noexcept;
    [[nodiscard]] std::size_t find_or_add_index(CurrencyKey key) noexcept;

    std::vector<CurrencyRecord> currencies_;
    // Lookup-only accelerator; authoritative iteration remains stable vector order.
    std::map<CurrencyKey, std::size_t> index_;
};

} // namespace thunder
