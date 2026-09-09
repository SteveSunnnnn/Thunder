#include "thunder/simulation/economy/CurrencyStore.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <cmath>

namespace thunder {

namespace {

inline constexpr EconomyPrice kMinExchangeRatePpm = 10'000;       // 0.01
inline constexpr EconomyPrice kMaxExchangeRatePpm = 100'000'000;   // 100.0
inline constexpr std::int64_t kMaxWeeklyRateDeltaPpm = 50'000;    // max 5% shift per tick
inline constexpr std::int64_t kGoldTransportPointPpm = 20'000;    // 2% gold shipping point band
inline constexpr EconomyAmount kSeigniorageRatePpm = 500;          // 0.05% seigniorage on turnover

} // namespace

CurrencyStore::CurrencyStore() {
    register_currency(default_currency_key, "core.currency.default",
                      MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    // Precious-metal numeraires: first-class Metal currencies that act as the FX
    // anchor for metallic-standard national currencies. Rate == reference metal price.
    register_currency(metal_gold_key, "core.metal.gold",
                      MonetaryStandard::FiatFloating, 1000, 15500, 1'000'000);
    register_currency(metal_silver_key, "core.metal.silver",
                      MonetaryStandard::FiatFloating, 1000, 15500, 64'516);
}

void CurrencyStore::clear() noexcept {
    currencies_.clear();
    index_.clear();
    register_currency(default_currency_key, "core.currency.default",
                      MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    register_currency(metal_gold_key, "core.metal.gold",
                      MonetaryStandard::FiatFloating, 1000, 15500, 1'000'000);
    register_currency(metal_silver_key, "core.metal.silver",
                      MonetaryStandard::FiatFloating, 1000, 15500, 64'516);
}

std::size_t CurrencyStore::find_index(CurrencyKey key) const noexcept {
    const auto it = index_.find(key);
    return it == index_.end() ? static_cast<std::size_t>(-1) : it->second;
}

std::size_t CurrencyStore::find_or_add_index(CurrencyKey key) noexcept {
    if (key == 0u) return static_cast<std::size_t>(-1);
    const auto idx = find_index(key);
    if (idx != static_cast<std::size_t>(-1)) return idx;
    const auto new_idx = currencies_.size();
    CurrencyRecord rec{};
    rec.key = key;
    rec.name = "currency." + std::to_string(key);
    rec.standard = MonetaryStandard::GoldStandard;
    rec.gold_parity_mg = 1000;
    rec.silver_parity_mg = 15500;
    rec.exchange_rate_ppm = 1'000'000;
    rec.target_rate_ppm = 1'000'000;
    currencies_.push_back(std::move(rec));
    index_.emplace(key, new_idx);
    return new_idx;
}

bool CurrencyStore::contains(CurrencyKey key) const noexcept {
    return find_index(key) != static_cast<std::size_t>(-1);
}

void CurrencyStore::register_currency(CurrencyKey key, std::string_view name,
                                     MonetaryStandard standard,
                                     std::uint32_t gold_parity_mg,
                                     std::uint32_t silver_parity_mg,
                                     EconomyPrice initial_rate_ppm) {
    if (key == 0u) return;
    const auto idx = find_index(key);
    if (idx != static_cast<std::size_t>(-1)) {
        currencies_[idx].name = std::string(name);
        currencies_[idx].standard = standard;
        currencies_[idx].gold_parity_mg = std::max(1u, gold_parity_mg);
        currencies_[idx].silver_parity_mg = std::max(1u, silver_parity_mg);
        currencies_[idx].exchange_rate_ppm = std::clamp(initial_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
        currencies_[idx].target_rate_ppm = currencies_[idx].exchange_rate_ppm;
        return;
    }
    CurrencyRecord rec{};
    rec.key = key;
    rec.name = std::string(name);
    rec.standard = standard;
    rec.gold_parity_mg = std::max(1u, gold_parity_mg);
    rec.silver_parity_mg = std::max(1u, silver_parity_mg);
    rec.exchange_rate_ppm = std::clamp(initial_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
    rec.target_rate_ppm = rec.exchange_rate_ppm;
    currencies_.push_back(std::move(rec));
    index_.emplace(key, currencies_.size() - 1u);
}

EconomyPrice CurrencyStore::exchange_rate_ppm(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 1'000'000;
    return currencies_[idx].exchange_rate_ppm;
}

void CurrencyStore::set_exchange_rate_ppm(CurrencyKey key, EconomyPrice rate_ppm) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].exchange_rate_ppm = std::clamp(rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
}

void CurrencyStore::set_target_rate_ppm(CurrencyKey key, EconomyPrice rate_ppm) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].target_rate_ppm = std::clamp(rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
}

EconomyPrice CurrencyStore::target_rate_ppm(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 1'000'000;
    return currencies_[idx].target_rate_ppm;
}

std::span<const EconomyPrice> CurrencyStore::exchange_rate_history(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return {};
    return currencies_[idx].history_rates_ppm;
}

MonetaryStandard CurrencyStore::monetary_standard(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return MonetaryStandard::GoldStandard;
    return currencies_[idx].standard;
}

void CurrencyStore::set_monetary_standard(CurrencyKey key, MonetaryStandard standard) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].standard = standard;
}

bool CurrencyStore::is_metal_currency(CurrencyKey key) const noexcept {
    return key == metal_gold_key || key == metal_silver_key;
}

CurrencyClass CurrencyStore::currency_class(CurrencyKey key) const noexcept {
    return is_metal_currency(key) ? CurrencyClass::Metal : CurrencyClass::National;
}

std::uint32_t CurrencyStore::gold_parity_mg(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 1000;
    return currencies_[idx].gold_parity_mg;
}

void CurrencyStore::set_gold_parity_mg(CurrencyKey key, std::uint32_t mg) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].gold_parity_mg = std::max(1u, mg);
}

std::uint32_t CurrencyStore::silver_parity_mg(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 15500;
    return currencies_[idx].silver_parity_mg;
}

void CurrencyStore::set_silver_parity_mg(CurrencyKey key, std::uint32_t mg) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].silver_parity_mg = std::max(1u, mg);
}

CountryId CurrencyStore::sovereign_leader(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return CountryId{};
    return currencies_[idx].sovereign_leader;
}

void CurrencyStore::set_sovereign_leader(CurrencyKey key, CountryId leader, double prestige) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].sovereign_leader = leader;
    currencies_[idx].leader_prestige = std::isfinite(prestige) ? std::max(0.0, prestige) : 0.0;
}

EconomyAmount CurrencyStore::seigniorage_accrued_milli(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 0;
    return currencies_[idx].seigniorage_accrued_milli;
}

void CurrencyStore::clear_seigniorage(CurrencyKey key) noexcept {
    const auto idx = find_index(key);
    if (idx != static_cast<std::size_t>(-1)) {
        currencies_[idx].seigniorage_accrued_milli = 0;
    }
}

std::uint64_t CurrencyStore::specie_export_mg(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 0;
    return currencies_[idx].specie_export_mg;
}

std::uint64_t CurrencyStore::specie_import_mg(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 0;
    return currencies_[idx].specie_import_mg;
}

bool CurrencyStore::convertibility_suspended(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return false;
    return currencies_[idx].convertibility_suspended;
}

void CurrencyStore::set_convertibility_suspended(CurrencyKey key, bool suspended) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].convertibility_suspended = suspended;
}

bool CurrencyStore::toggle_convertibility(CurrencyKey key) noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return false;
    const auto standard = currencies_[idx].standard;
    const bool metallic = standard == MonetaryStandard::GoldStandard ||
                          standard == MonetaryStandard::SilverStandard ||
                          standard == MonetaryStandard::Bimetallism;
    if (!metallic) return false;
    currencies_[idx].convertibility_suspended = !currencies_[idx].convertibility_suspended;
    return true;
}

void CurrencyStore::set_seigniorage_accrued_milli(CurrencyKey key, EconomyAmount milli) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].seigniorage_accrued_milli = std::max<EconomyAmount>(0, milli);
}

void CurrencyStore::set_trade_demand_milli(CurrencyKey key, EconomyAmount milli) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].trade_demand_milli = std::max<EconomyAmount>(0, milli);
}

void CurrencyStore::set_trade_supply_milli(CurrencyKey key, EconomyAmount milli) noexcept {
    const auto idx = find_or_add_index(key);
    if (idx == static_cast<std::size_t>(-1)) return;
    currencies_[idx].trade_supply_milli = std::max<EconomyAmount>(0, milli);
}

EconomyAmount CurrencyStore::convert(EconomyAmount amount_milli,
                                     CurrencyKey from,
                                     CurrencyKey to) const noexcept {
    if (amount_milli == 0 || from == to) return amount_milli;
    const auto rate_from = exchange_rate_ppm(from);
    const auto rate_to = exchange_rate_ppm(to);
    if (rate_from == rate_to) return amount_milli;
    if (rate_to <= 0) return amount_milli;

    if (amount_milli > 0) {
        return mul_div_nonnegative(amount_milli, rate_from, rate_to);
    }
    // Work in an unsigned magnitude domain so INT64_MIN remains a valid
    // input.  A negative conversion may legitimately saturate at INT64_MIN;
    // negating that value in signed arithmetic would be undefined behaviour.
    constexpr auto negative_magnitude_limit = std::uint64_t{1} << 63u;
    const auto magnitude = detail::mul_div_u64_saturating(
        detail::abs_u64(amount_milli),
        rate_from > 0 ? static_cast<std::uint64_t>(rate_from) : 0u,
        static_cast<std::uint64_t>(rate_to), negative_magnitude_limit);
    if (magnitude >= negative_magnitude_limit)
        return std::numeric_limits<EconomyAmount>::min();
    return -static_cast<EconomyAmount>(magnitude);
}

EconomyPrice CurrencyStore::convert_price(EconomyPrice price_milli,
                                         CurrencyKey from,
                                         CurrencyKey to) const noexcept {
    if (price_milli <= 0 || from == to) return price_milli;
    const auto rate_from = exchange_rate_ppm(from);
    const auto rate_to = exchange_rate_ppm(to);
    if (rate_from == rate_to) return price_milli;
    if (rate_to <= 0) return price_milli;

    return std::max<EconomyPrice>(1, mul_div_nonnegative(price_milli, rate_from, rate_to));
}

EconomyAmount CurrencyStore::coin_commodity_value_milli(
    CurrencyKey coin, EconomyPrice gold_price_ppm,
    EconomyPrice silver_price_ppm) const noexcept {
    const auto idx = find_index(coin);
    if (idx == static_cast<std::size_t>(-1)) return 0;
    const auto& record = currencies_[idx];
    const auto safe_gold = std::max<EconomyPrice>(1, gold_price_ppm);
    const auto safe_silver = std::max<EconomyPrice>(1, silver_price_ppm);

    switch (record.standard) {
    case MonetaryStandard::GoldStandard:
        return mul_div_nonnegative(safe_gold, record.gold_parity_mg, 1000);
    case MonetaryStandard::SilverStandard:
        return mul_div_nonnegative(safe_silver, record.silver_parity_mg, 1000);
    case MonetaryStandard::Bimetallism: {
        // Melt value takes the richer leg; the legal circulation rate in
        // update_exchange_rates takes the cheaper one (Gresham's law).
        const auto gold_melt = mul_div_nonnegative(safe_gold, record.gold_parity_mg, 1000);
        const auto silver_melt = mul_div_nonnegative(safe_silver, record.silver_parity_mg, 1000);
        return static_cast<EconomyAmount>(std::max(gold_melt, silver_melt));
    }
    case MonetaryStandard::FiatFloating:
        return 0;
    }
    return 0;
}

void CurrencyStore::record_fx_flow(CurrencyKey sold_currency, CurrencyKey bought_currency,
                                  EconomyAmount volume_in_bought_milli) noexcept {
    if (sold_currency == 0u || bought_currency == 0u || sold_currency == bought_currency ||
        volume_in_bought_milli <= 0) return;
    const auto bought_idx = find_or_add_index(bought_currency);
    const auto sold_idx = find_or_add_index(sold_currency);

    // Demand for bought_currency rises
    currencies_[bought_idx].trade_demand_milli =
        saturating_add(currencies_[bought_idx].trade_demand_milli, volume_in_bought_milli);

    // Seigniorage accrued to the sovereign of the circulating currency
    const auto seigniorage = mul_div_nonnegative(volume_in_bought_milli, kSeigniorageRatePpm, ppm_scale);
    currencies_[bought_idx].seigniorage_accrued_milli =
        saturating_add(currencies_[bought_idx].seigniorage_accrued_milli, seigniorage);

    // Supply of sold_currency rises in equivalent sold currency terms
    const auto volume_in_sold = convert(volume_in_bought_milli, bought_currency, sold_currency);
    currencies_[sold_idx].trade_supply_milli =
        saturating_add(currencies_[sold_idx].trade_supply_milli, volume_in_sold);
}

void CurrencyStore::evaluate_monetary_sovereignty(const CountryStore& countries) noexcept {
    for (auto& cur : currencies_) {
        CountryId best_leader{};
        double best_power = -1.0;

        // Leadership is derived state.  Clear it before scanning so a country
        // that changes its primary currency cannot keep controlling the old
        // currency zone (and draining its reserves on the next FX update).
        cur.sovereign_leader = CountryId{};
        cur.leader_prestige = 0.0;

        for (std::size_t ci = 0; ci < countries.size(); ++ci) {
            const CountryId id{static_cast<CountryId::rep_type>(ci)};
            if (countries.primary_currency(id) != cur.key) continue;

            const double power = countries.power_score(id);
            if (power > best_power) {
                best_power = power;
                best_leader = id;
            }
        }

        if (best_leader.valid()) {
            cur.sovereign_leader = best_leader;
            cur.leader_prestige = best_power;
        }
    }
}

void CurrencyStore::update_exchange_rates(EconomyPrice gold_price_ppm,
                                         EconomyPrice silver_price_ppm,
                                         CountryStore* countries) noexcept {
    // Base parity: 1000 mg Gold = gold_price_ppm
    // Silver parity: 15500 mg Silver at standard 15.5:1 ratio = gold_price_ppm
    // The supplied metal prices are authoritative and also pin the Metal numeraire
    // currencies (core.metal.gold / core.metal.silver), which act as the FX anchor.
    const auto safe_gold_price = std::max<EconomyPrice>(1, gold_price_ppm);
    const auto safe_silver_price = std::max<EconomyPrice>(1, silver_price_ppm);
    const EconomyPrice gold_ref = safe_gold_price;
    const EconomyPrice silver_ref = safe_silver_price;

    for (auto& cur : currencies_) {
        cur.specie_export_mg = 0;
        cur.specie_import_mg = 0;

        // Metal numeraires are the numeraire itself: their rate is the reference
        // metal price. No mint parity, no specie-flow, no trade-pressure drift.
        if (is_metal_currency(cur.key)) {
            const EconomyPrice ref = (cur.key == gold_metal_key()) ? gold_ref : silver_ref;
            cur.exchange_rate_ppm = std::clamp(ref, kMinExchangeRatePpm, kMaxExchangeRatePpm);
            cur.target_rate_ppm = cur.exchange_rate_ppm;
            cur.trade_demand_milli = 0;
            cur.trade_supply_milli = 0;
            cur.history_rates_ppm.push_back(cur.exchange_rate_ppm);
            if (cur.history_rates_ppm.size() > 52) {
                cur.history_rates_ppm.erase(cur.history_rates_ppm.begin());
            }
            continue;
        }

        // Compute metallic mint parity target rate
        switch (cur.standard) {
        case MonetaryStandard::GoldStandard: {
            // Target mint parity proportional to gold content:
            // Rate = (gold_parity_mg / 1000.0) * gold_ref (Metal numeraire rate)
            cur.target_rate_ppm = std::clamp<EconomyPrice>(
                mul_div_nonnegative(gold_ref, cur.gold_parity_mg, 1000),
                kMinExchangeRatePpm, kMaxExchangeRatePpm);
            break;
        }
        case MonetaryStandard::SilverStandard: {
            // Target mint parity proportional to silver content:
            cur.target_rate_ppm = std::clamp<EconomyPrice>(
                mul_div_nonnegative(silver_ref, cur.silver_parity_mg, 1000),
                kMinExchangeRatePpm, kMaxExchangeRatePpm);
            break;
        }
        case MonetaryStandard::Bimetallism: {
            // Gresham's Law:
            // Legal Ratio = silver_parity_mg / gold_parity_mg (e.g. 15.5)
            // Market Ratio = gold_ref / silver_ref
            // If Gold is undervalued by law (Market Ratio > Legal Ratio), Silver is overvalued -> Silver circulates
            // If Silver is undervalued by law (Market Ratio < Legal Ratio), Gold is overvalued -> Gold circulates
            const auto gold_mint_val = mul_div_nonnegative(gold_ref, cur.gold_parity_mg, 1000);
            const auto silver_mint_val = mul_div_nonnegative(silver_ref, cur.silver_parity_mg, 1000);
            // Market tracks the cheaper/circulating metal
            cur.target_rate_ppm = std::clamp<EconomyPrice>(
                std::min(gold_mint_val, silver_mint_val),
                kMinExchangeRatePpm, kMaxExchangeRatePpm);
            break;
        }
        case MonetaryStandard::FiatFloating: {
            // Pure credit currency: target follows current rate
            cur.target_rate_ppm = cur.exchange_rate_ppm;
            break;
        }
        }

        // Market adjustments based on trade pressure and Hume Specie-Flow mechanism
        const auto demand = cur.trade_demand_milli;
        const auto supply = cur.trade_supply_milli;
        const auto total_volume = saturating_add(demand, supply);

        if (total_volume > 0) {
            const auto net_balance = saturating_sub(demand, supply);
            const auto pressure_ppm = signed_ratio_ppm(net_balance, total_volume);

            const auto shift_mag = mul_div_nonnegative(
                cur.exchange_rate_ppm, std::abs(pressure_ppm), ppm_scale * 20);
            const auto clamped_shift = std::min<EconomyAmount>(shift_mag, kMaxWeeklyRateDeltaPpm);

            if (pressure_ppm > 0) {
                cur.exchange_rate_ppm = saturating_add(cur.exchange_rate_ppm, clamped_shift);
            } else if (pressure_ppm < 0) {
                cur.exchange_rate_ppm = saturating_sub(cur.exchange_rate_ppm, clamped_shift);
            }
        }

        // Apply Gold/Silver Points Specie Arbitrage & Physical Specie Transport:
        // Specie-flow prevents exchange rates from deviating beyond transport points.
        // Once buying FX on market > Cost of redeeming gold at mint + shipping,
        // banks automatically ship physical gold/silver to clear the balance.
        if (!cur.convertibility_suspended && (
            cur.standard == MonetaryStandard::GoldStandard ||
            cur.standard == MonetaryStandard::SilverStandard ||
            cur.standard == MonetaryStandard::Bimetallism)) {

            const auto export_point = std::max<EconomyPrice>(
                kMinExchangeRatePpm,
                mul_div_nonnegative(cur.target_rate_ppm, ppm_scale - kGoldTransportPointPpm, ppm_scale));
            const auto import_point = mul_div_nonnegative(
                cur.target_rate_ppm, ppm_scale + kGoldTransportPointPpm, ppm_scale);

            if (cur.exchange_rate_ppm < export_point) {
                // Specie export arbitrage triggered
                const auto fx_deficit = demand > supply ? demand - supply : 1000;
                const auto active_parity_mg = (cur.standard == MonetaryStandard::SilverStandard)
                    ? cur.silver_parity_mg
                    : cur.gold_parity_mg;
                const auto gold_exported_mg = mul_div_nonnegative(fx_deficit, active_parity_mg, 1000);

                bool defended = true;
                if (countries != nullptr && cur.sovereign_leader.valid()) {
                    const auto reserves = countries->foreign_reserves_milli(cur.sovereign_leader);
                    const auto reserve_cost = convert(fx_deficit, cur.key, default_currency_key);
                    if (reserves >= reserve_cost) {
                        countries->add_foreign_reserves_milli(cur.sovereign_leader, -reserve_cost);
                        cur.specie_export_mg = static_cast<std::uint64_t>(gold_exported_mg);
                    } else if (reserves > 0) {
                        countries->set_foreign_reserves_milli(cur.sovereign_leader, 0);
                        const auto funded_currency = convert(reserves, default_currency_key, cur.key);
                        cur.specie_export_mg = static_cast<std::uint64_t>(
                            mul_div_nonnegative(funded_currency, active_parity_mg, 1000));
                        cur.convertibility_suspended = true;
                        defended = false;
                    } else {
                        cur.convertibility_suspended = true;
                        defended = false;
                    }
                } else {
                    cur.specie_export_mg = static_cast<std::uint64_t>(gold_exported_mg);
                }

                if (defended) {
                    cur.exchange_rate_ppm = export_point;
                }
            } else if (cur.exchange_rate_ppm > import_point) {
                // Specie import arbitrage triggered
                const auto fx_surplus = supply > demand ? supply - demand : 1000;
                const auto active_parity_mg = (cur.standard == MonetaryStandard::SilverStandard)
                    ? cur.silver_parity_mg
                    : cur.gold_parity_mg;
                const auto gold_imported_mg = mul_div_nonnegative(fx_surplus, active_parity_mg, 1000);
                cur.specie_import_mg = static_cast<std::uint64_t>(gold_imported_mg);

                if (countries != nullptr && cur.sovereign_leader.valid()) {
                    countries->add_foreign_reserves_milli(cur.sovereign_leader,
                        convert(fx_surplus, cur.key, default_currency_key));
                }
                cur.exchange_rate_ppm = import_point;
            }
        }

        cur.exchange_rate_ppm = std::clamp(cur.exchange_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
        cur.trade_demand_milli = 0;
        cur.trade_supply_milli = 0;

        // Record rolling weekly exchange rate for UI charts (retain up to 52 weeks)
        cur.history_rates_ppm.push_back(cur.exchange_rate_ppm);
        if (cur.history_rates_ppm.size() > 52) {
            cur.history_rates_ppm.erase(cur.history_rates_ppm.begin());
        }
    }
}

bool CurrencyStore::validate(std::size_t country_count) const noexcept {
    constexpr std::size_t max_currencies = 1'024u;
    if (currencies_.empty() || currencies_.size() > max_currencies ||
        index_.size() != currencies_.size()) return false;

    for (std::size_t i = 0; i < currencies_.size(); ++i) {
        const auto& c = currencies_[i];
        if (c.key == 0u || c.gold_parity_mg == 0u || c.silver_parity_mg == 0u ||
            static_cast<std::uint8_t>(c.standard) >
                static_cast<std::uint8_t>(MonetaryStandard::FiatFloating) ||
            c.exchange_rate_ppm < kMinExchangeRatePpm ||
            c.exchange_rate_ppm > kMaxExchangeRatePpm ||
            c.target_rate_ppm < kMinExchangeRatePpm ||
            c.target_rate_ppm > kMaxExchangeRatePpm || c.trade_demand_milli < 0 ||
            c.trade_supply_milli < 0 || c.seigniorage_accrued_milli < 0 ||
            !std::isfinite(c.leader_prestige) || c.leader_prestige < 0.0 ||
            (c.sovereign_leader.valid() && c.sovereign_leader.value() >= country_count) ||
            c.history_rates_ppm.size() > 52u)
            return false;
        const auto indexed = index_.find(c.key);
        if (indexed == index_.end() || indexed->second != i) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (currencies_[j].key == c.key) return false;
        for (const auto history_rate : c.history_rates_ppm)
            if (history_rate < kMinExchangeRatePpm || history_rate > kMaxExchangeRatePpm)
                return false;
    }
    return true;
}

std::size_t CurrencyStore::memory_bytes() const noexcept {
    std::size_t bytes = sizeof(CurrencyStore) + currencies_.capacity() * sizeof(CurrencyRecord);
    for (const auto& c : currencies_) {
        bytes += c.name.capacity();
        bytes += c.history_rates_ppm.capacity() * sizeof(EconomyPrice);
    }
    bytes += index_.size() * (sizeof(std::pair<const CurrencyKey, std::size_t>) + 3u * sizeof(void*));
    return bytes;
}

std::uint64_t CurrencyStore::checksum() const noexcept {
    Fnv1a64 h;
    h.add(currencies_.size());
    for (const auto& c : currencies_) {
        h.add(c.key);
        h.add(c.name);
        h.add(static_cast<std::uint8_t>(c.standard));
        h.add(c.sovereign_leader.value());
        h.add(c.gold_parity_mg);
        h.add(c.silver_parity_mg);
        h.add(c.exchange_rate_ppm);
        h.add(c.target_rate_ppm);
        h.add(c.convertibility_suspended);
        h.add(c.trade_demand_milli);
        h.add(c.trade_supply_milli);
        h.add(c.seigniorage_accrued_milli);
    }
    return h.value();
}

} // namespace thunder
