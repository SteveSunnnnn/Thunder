#include "thunder/simulation/economy/CountryStore.hpp"

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {

// Weekly simulation cadence: annual figures (bond yields, debt ratios) are
// derived from weekly flows with this factor. Kept in sync with BankStore.
inline constexpr EconomyAmount weeks_per_year = 52;

double finite_value(double value, const char* name) {
    if (!std::isfinite(value)) throw std::invalid_argument(std::string{name} + " must be finite");
    return value;
}

double nonnegative_finite(double value, const char* name) {
    return std::max(0.0, finite_value(value, name));
}

double tax_finite(double value) {
    return std::clamp(finite_value(value, "tax rate"), 0.0, 1.0);
}

double saturating_double_add(double a, double b) {
    finite_value(a, "country value");
    finite_value(b, "country delta");
    if (b > 0.0 && a > std::numeric_limits<double>::max() - b) return std::numeric_limits<double>::max();
    if (b < 0.0 && a < -std::numeric_limits<double>::max() - b) return -std::numeric_limits<double>::max();
    return a + b;
}

// Authoritative money is integer milli-units. Doubles entering through the
// compatibility API are quantized once at the boundary, so the legacy double
// column is always exactly treasury_milli_/economy_scale and never
// accumulates floating-point error of its own (previously add_treasury
// accumulated in double and re-derived milli from the noisy sum).
// The clamp keeps llround far from int64 overflow while remaining ~10^12x
// larger than any plausible treasury.
EconomyAmount double_to_milli_saturating(double value) {
    constexpr double max_convertible = 1.0e15;
    if (value >= max_convertible) return std::numeric_limits<EconomyAmount>::max();
    if (value <= -max_convertible) return std::numeric_limits<EconomyAmount>::min();
    return static_cast<EconomyAmount>(std::llround(value * static_cast<double>(economy_scale)));
}

double milli_to_double(EconomyAmount milli) {
    return static_cast<double>(milli) / static_cast<double>(economy_scale);
}

} // namespace

void CountryStore::reserve(std::size_t count) {
    tags_.reserve(count);
    population_.reserve(count);
    gdp_.reserve(count);
    nominal_gdp_milli_.reserve(count);
    treasury_.reserve(count);
    treasury_milli_.reserve(count);
    tax_rate_.reserve(count);
    prestige_.reserve(count);
    tax_policies_.reserve(count);
    primary_currencies_.reserve(count);
    foreign_reserves_milli_.reserve(count);
    balance_of_payments_milli_.reserve(count);
    national_debt_milli_.reserve(count);
    credit_ratings_.reserve(count);
    bond_yields_ppm_.reserve(count);
    default_weeks_.reserve(count);
}

CountryId CountryStore::create(CountryInit init) {
    const auto raw = static_cast<CountryId::rep_type>(tags_.size());
    tags_.push_back(std::move(init.tag));
    population_.push_back(nonnegative_finite(init.population, "population"));
    gdp_.push_back(nonnegative_finite(init.gdp, "gdp"));
    nominal_gdp_milli_.push_back(0);
    const double t_val = finite_value(init.treasury, "treasury");
    const EconomyAmount t_milli = double_to_milli_saturating(t_val);
    treasury_milli_.push_back(t_milli);
    treasury_.push_back(milli_to_double(t_milli));
    tax_rate_.push_back(tax_finite(init.tax_rate));
    prestige_.push_back(nonnegative_finite(init.prestige, "prestige"));
    primary_currencies_.push_back(init.primary_currency != 0u ? init.primary_currency : default_currency_key);
    foreign_reserves_milli_.push_back(std::max<EconomyAmount>(0, init.foreign_reserves_milli));
    balance_of_payments_milli_.push_back(0);
    national_debt_milli_.push_back(std::max<EconomyAmount>(0, init.national_debt_milli));
    credit_ratings_.push_back(CreditRating::AAA);
    bond_yields_ppm_.push_back(30'000);
    default_weeks_.push_back(0);
    TaxPolicy default_policy;
    default_policy.income_tax_ppm = static_cast<std::int32_t>(
        std::clamp(init.tax_rate, 0.0, 1.0) * static_cast<double>(ppm_scale) + 0.5);
    tax_policies_.push_back(default_policy);
    return CountryId{raw};
}

std::size_t CountryStore::idx(CountryId id) const {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= size()) throw std::out_of_range("invalid CountryId");
    return i;
}

std::string_view CountryStore::tag(CountryId id) const { return tags_[idx(id)]; }
double CountryStore::population(CountryId id) const { return population_[idx(id)]; }
double CountryStore::gdp(CountryId id) const { return gdp_[idx(id)]; }
double CountryStore::nominal_gdp(CountryId id) const {
    const auto i = idx(id);
    return i < nominal_gdp_milli_.size()
        ? static_cast<double>(nominal_gdp_milli_[i]) / static_cast<double>(economy_scale)
        : 0.0;
}
EconomyAmount CountryStore::nominal_gdp_milli(CountryId id) const {
    const auto i = idx(id);
    return i < nominal_gdp_milli_.size() ? nominal_gdp_milli_[i] : 0;
}
double CountryStore::treasury(CountryId id) const { return treasury_[idx(id)]; }
EconomyAmount CountryStore::treasury_milli(CountryId id) const {
    const auto i = idx(id);
    return i < treasury_milli_.size() ? treasury_milli_[i] : 0;
}
double CountryStore::tax_rate(CountryId id) const { return tax_rate_[idx(id)]; }
const TaxPolicy& CountryStore::tax_policy(CountryId id) const { return tax_policies_[idx(id)]; }

CurrencyKey CountryStore::primary_currency(CountryId id) const {
    const auto i = idx(id);
    return i < primary_currencies_.size() ? primary_currencies_[i] : default_currency_key;
}
void CountryStore::set_primary_currency(CountryId id, CurrencyKey key) {
    primary_currencies_[idx(id)] = key != 0u ? key : default_currency_key;
}
EconomyAmount CountryStore::foreign_reserves_milli(CountryId id) const {
    const auto i = idx(id);
    return i < foreign_reserves_milli_.size() ? foreign_reserves_milli_[i] : 0;
}
void CountryStore::set_foreign_reserves_milli(CountryId id, EconomyAmount amount) {
    foreign_reserves_milli_[idx(id)] = std::max<EconomyAmount>(0, amount);
}
void CountryStore::add_foreign_reserves_milli(CountryId id, EconomyAmount delta) {
    const auto i = idx(id);
    foreign_reserves_milli_[i] = std::max<EconomyAmount>(0,
        saturating_add(foreign_reserves_milli_[i], delta));
}
EconomyAmount CountryStore::balance_of_payments_milli(CountryId id) const {
    const auto i = idx(id);
    return i < balance_of_payments_milli_.size() ? balance_of_payments_milli_[i] : 0;
}
void CountryStore::set_balance_of_payments_milli(CountryId id, EconomyAmount amount) {
    balance_of_payments_milli_[idx(id)] = amount;
}
void CountryStore::add_balance_of_payments_milli(CountryId id, EconomyAmount delta) {
    const auto i = idx(id);
    balance_of_payments_milli_[i] = saturating_add(balance_of_payments_milli_[i], delta);
}

double CountryStore::prestige(CountryId id) const {
    const auto i = idx(id);
    return i < prestige_.size() ? prestige_[i] : 0.0;
}
void CountryStore::set_prestige(CountryId id, double value) {
    prestige_[idx(id)] = nonnegative_finite(value, "prestige");
}
void CountryStore::add_prestige(CountryId id, double delta) {
    const auto i = idx(id);
    prestige_[i] = std::max(0.0, saturating_double_add(prestige_[i], delta));
}
double CountryStore::power_score(CountryId id) const {
    const auto i = idx(id);
    const double p = i < prestige_.size() ? prestige_[i] : 0.0;
    const double g = i < gdp_.size() ? gdp_[i] : 0.0;
    const double t = i < treasury_.size() ? std::max(0.0, treasury_[i]) : 0.0;
    return p + (g * 0.001) + (t * 0.0001);
}

void CountryStore::set_population(CountryId id, double value) {
    population_[idx(id)] = nonnegative_finite(value, "population");
}
void CountryStore::set_gdp(CountryId id, double value) {
    gdp_[idx(id)] = nonnegative_finite(value, "gdp");
}
void CountryStore::set_nominal_gdp_milli(CountryId id, EconomyAmount amount_milli) {
    const auto i = idx(id);
    if (i < nominal_gdp_milli_.size()) nominal_gdp_milli_[i] = std::max<EconomyAmount>(0, amount_milli);
}
void CountryStore::set_treasury(CountryId id, double value) {
    const auto i = idx(id);
    const double val = finite_value(value, "treasury");
    const auto milli = double_to_milli_saturating(val);
    treasury_milli_[i] = milli;
    treasury_[i] = milli_to_double(milli);
}
void CountryStore::set_treasury_milli(CountryId id, EconomyAmount amount_milli) {
    const auto i = idx(id);
    treasury_milli_[i] = amount_milli;
    treasury_[i] = milli_to_double(amount_milli);
}
void CountryStore::set_tax_rate(CountryId id, double value) {
    const auto i = idx(id);
    const double normalized = tax_finite(value);
    tax_rate_[i] = normalized;
    tax_policies_[i].income_tax_ppm = static_cast<std::int32_t>(
        normalized * static_cast<double>(ppm_scale) + 0.5);
}
void CountryStore::set_tax_policy(CountryId id, TaxPolicy policy) {
    const auto normalize = [](std::int32_t value) {
        return std::clamp<std::int32_t>(value, 0, static_cast<std::int32_t>(ppm_scale));
    };
    policy.income_tax_ppm = normalize(policy.income_tax_ppm);
    policy.consumption_tax_ppm = normalize(policy.consumption_tax_ppm);
    policy.land_tax_ppm = normalize(policy.land_tax_ppm);
    policy.per_capita_tax_ppm = normalize(policy.per_capita_tax_ppm);
    policy.dividends_tax_ppm = normalize(policy.dividends_tax_ppm);
    tax_policies_[idx(id)] = policy;
}
void CountryStore::add_treasury(CountryId id, double delta) {
    const auto i = idx(id);
    // Quantize the delta, then accumulate in integer milli: the double column
    // stays an exact view of the authoritative integer and repeated scripted
    // adds cannot drift at sub-milli precision.
    const auto delta_milli = double_to_milli_saturating(finite_value(delta, "treasury delta"));
    treasury_milli_[i] = saturating_add(treasury_milli_[i], delta_milli);
    treasury_[i] = milli_to_double(treasury_milli_[i]);
}
void CountryStore::add_treasury_milli(CountryId id, EconomyAmount delta_milli) {
    const auto i = idx(id);
    treasury_milli_[i] = saturating_add(treasury_milli_[i], delta_milli);
    treasury_[i] = milli_to_double(treasury_milli_[i]);
}

EconomyAmount CountryStore::national_debt_milli(CountryId id) const {
    const auto i = idx(id);
    return i < national_debt_milli_.size() ? national_debt_milli_[i] : 0;
}
void CountryStore::set_national_debt_milli(CountryId id, EconomyAmount debt) {
    const auto i = idx(id);
    if (i < national_debt_milli_.size()) national_debt_milli_[i] = std::max<EconomyAmount>(0, debt);
}
void CountryStore::add_national_debt_milli(CountryId id, EconomyAmount delta) {
    const auto i = idx(id);
    if (i < national_debt_milli_.size())
        national_debt_milli_[i] = std::max<EconomyAmount>(0, saturating_add(national_debt_milli_[i], delta));
}
CreditRating CountryStore::credit_rating(CountryId id) const {
    const auto i = idx(id);
    return i < credit_ratings_.size() ? credit_ratings_[i] : CreditRating::AAA;
}
void CountryStore::set_credit_rating(CountryId id, CreditRating rating) {
    const auto i = idx(id);
    if (i < credit_ratings_.size()) credit_ratings_[i] = rating;
}
EconomyPrice CountryStore::bond_yield_ppm(CountryId id) const {
    const auto i = idx(id);
    return i < bond_yields_ppm_.size() ? bond_yields_ppm_[i] : 30'000;
}
void CountryStore::set_bond_yield_ppm(CountryId id, EconomyPrice yield_ppm) {
    const auto i = idx(id);
    if (i < bond_yields_ppm_.size())
        bond_yields_ppm_[i] = std::clamp<EconomyPrice>(yield_ppm, 10'000, 500'000);
}
std::uint16_t CountryStore::default_weeks(CountryId id) const {
    const auto i = idx(id);
    return i < default_weeks_.size() ? default_weeks_[i] : 0;
}
void CountryStore::set_default_weeks(CountryId id, std::uint16_t weeks) {
    const auto i = idx(id);
    if (i < default_weeks_.size()) default_weeks_[i] = weeks;
}
bool CountryStore::is_in_default(CountryId id) const {
    return credit_rating(id) == CreditRating::D || default_weeks(id) > 0;
}
EconomyAmount CountryStore::weekly_debt_service_milli(CountryId id) const {
    const auto i = idx(id);
    if (i >= national_debt_milli_.size() || national_debt_milli_[i] <= 0) return 0;
    const auto debt = national_debt_milli_[i];
    const auto yield_ppm = i < bond_yields_ppm_.size() ? bond_yields_ppm_[i] : 30'000;
    return mul_div_nonnegative(debt, yield_ppm, 52LL * ppm_scale);
}
EconomyAmount CountryStore::borrowing_capacity_milli(CountryId id) const {
    const auto i = idx(id);
    if (is_in_default(id)) return 0;
    const double gdp_val = i < gdp_.size() ? gdp_[i] : 0.0;
    const double gdp_clamped = std::clamp(gdp_val, 0.0, 9e15);
    // gdp_ is the WEEKLY value-added flow; debt is a stock. Annualize the flow
    // so the ceiling is a debt-to-annual-GDP ratio, matching the rating bands.
    const auto weekly_gdp_milli = static_cast<EconomyAmount>(std::llround(gdp_clamped * 1000.0));
    const auto gdp_milli = mul_div_nonnegative(weekly_gdp_milli, weeks_per_year, 1);
    const auto rating = credit_rating(id);
    std::int64_t max_mult_ppm = 2'500'000;
    switch (rating) {
    case CreditRating::AAA: max_mult_ppm = 2'500'000; break;
    case CreditRating::AA:  max_mult_ppm = 2'000'000; break;
    case CreditRating::A:   max_mult_ppm = 1'500'000; break;
    case CreditRating::BBB: max_mult_ppm = 1'000'000; break;
    case CreditRating::BB:  max_mult_ppm = 600'000; break;
    case CreditRating::B:   max_mult_ppm = 300'000; break;
    case CreditRating::CCC: max_mult_ppm = 100'000; break;
    case CreditRating::D:   return 0;
    }
    const auto ceiling = mul_div_nonnegative(std::max<EconomyAmount>(1000, gdp_milli), max_mult_ppm, ppm_scale);
    return saturating_sub(ceiling, national_debt_milli(id));
}

EconomyAmount CountryStore::issue_sovereign_bonds(CountryId id, EconomyAmount requested_amount, World& world) {
    if (requested_amount <= 0 || is_in_default(id)) return 0;
    const auto actual_borrow = std::min(requested_amount, borrowing_capacity_milli(id));
    if (actual_borrow <= 0) return 0;
    const auto from_banks = world.banks.fund_sovereign_bonds(
        id, primary_currency(id), actual_borrow, bond_yield_ppm(id));
    const auto from_pool = world.grand_strategy.withdraw_investment_pool_funds(
        id, saturating_sub(actual_borrow, from_banks));
    const auto funded = saturating_add(from_banks, from_pool);
    if (funded <= 0) return 0;
    add_treasury_milli(id, funded);
    add_national_debt_milli(id, funded);
    evaluate_credit_rating(id);
    return funded;
}
EconomyAmount CountryStore::repay_sovereign_debt(CountryId id, EconomyAmount repayment_amount, World& world) {
    if (repayment_amount <= 0) return 0;
    const auto current_debt = national_debt_milli(id);
    const auto treasury_avail = std::max<EconomyAmount>(0, treasury_milli(id));
    const auto actual_repay = std::min({repayment_amount, current_debt, treasury_avail});
    if (actual_repay <= 0) return 0;
    add_treasury_milli(id, -actual_repay);
    const auto bank_repayment = world.banks.redeem_sovereign_bonds(id, actual_repay);
    const auto saver_repayment = saturating_sub(actual_repay, bank_repayment);
    if (saver_repayment > 0) world.grand_strategy.add_investment_pool_funds(id, saver_repayment);
    add_national_debt_milli(id, -actual_repay);
    evaluate_credit_rating(id);
    return actual_repay;
}
void CountryStore::evaluate_credit_rating(CountryId id) {
    const auto i = idx(id);
    if (i >= credit_ratings_.size()) return;
    if (default_weeks_[i] > 0) {
        credit_ratings_[i] = CreditRating::D;
        bond_yields_ppm_[i] = 300'000;
        return;
    }
    const auto debt = national_debt_milli_[i];
    if (debt == 0) {
        credit_ratings_[i] = CreditRating::AAA;
        bond_yields_ppm_[i] = 25'000;
        return;
    }
    const double gdp_clamped = std::clamp(i < gdp_.size() ? gdp_[i] : 0.0, 0.0, 9e15);
    // gdp_ is the WEEKLY value-added flow; annualize it so the debt stock is
    // compared against annual GDP (the thresholds below are annual ratios:
    // AAA < 30%, BBB < 150%, CCC at >= 250% of annual GDP).
    const auto weekly_gdp_milli = static_cast<EconomyAmount>(std::llround(gdp_clamped * 1000.0));
    const auto gdp_milli = std::max<EconomyAmount>(1000,
        mul_div_nonnegative(weekly_gdp_milli, weeks_per_year, 1));
    const auto debt_ratio_ppm = mul_div_nonnegative(debt, ppm_scale, gdp_milli);
    if (debt_ratio_ppm < 300'000) {
        credit_ratings_[i] = CreditRating::AAA; bond_yields_ppm_[i] = 30'000;
    } else if (debt_ratio_ppm < 600'000) {
        credit_ratings_[i] = CreditRating::AA; bond_yields_ppm_[i] = 38'000;
    } else if (debt_ratio_ppm < 1'000'000) {
        credit_ratings_[i] = CreditRating::A; bond_yields_ppm_[i] = 48'000;
    } else if (debt_ratio_ppm < 1'500'000) {
        credit_ratings_[i] = CreditRating::BBB; bond_yields_ppm_[i] = 60'000;
    } else if (debt_ratio_ppm < 2'000'000) {
        credit_ratings_[i] = CreditRating::BB; bond_yields_ppm_[i] = 85'000;
    } else if (debt_ratio_ppm < 2'500'000) {
        credit_ratings_[i] = CreditRating::B; bond_yields_ppm_[i] = 120'000;
    } else {
        credit_ratings_[i] = CreditRating::CCC; bond_yields_ppm_[i] = 180'000;
    }
}

std::uint64_t CountryStore::checksum() const noexcept {
    Fnv1a64 h;
    const auto n = tags_.size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        h.add(tags_[i]);
        h.add(population_[i]);
        h.add(gdp_[i]);
        h.add(treasury_[i]);
        h.add(i < treasury_milli_.size() ? treasury_milli_[i] : 0);
        h.add(tax_rate_[i]);
        h.add(i < prestige_.size() ? prestige_[i] : 0.0);
        h.add(tax_policies_[i].income_tax_ppm);
        h.add(tax_policies_[i].consumption_tax_ppm);
        h.add(tax_policies_[i].land_tax_ppm);
        h.add(tax_policies_[i].per_capita_tax_ppm);
        h.add(tax_policies_[i].dividends_tax_ppm);
        h.add(i < primary_currencies_.size() ? primary_currencies_[i] : default_currency_key);
        h.add(i < foreign_reserves_milli_.size() ? foreign_reserves_milli_[i] : 0);
        h.add(i < balance_of_payments_milli_.size() ? balance_of_payments_milli_[i] : 0);
        h.add(i < national_debt_milli_.size() ? national_debt_milli_[i] : 0);
        h.add(i < credit_ratings_.size() ? static_cast<std::uint8_t>(credit_ratings_[i]) : static_cast<std::uint8_t>(0));
        h.add(i < bond_yields_ppm_.size() ? bond_yields_ppm_[i] : 0);
        h.add(i < default_weeks_.size() ? default_weeks_[i] : 0);
    }
    return h.value();
}

} // namespace thunder
