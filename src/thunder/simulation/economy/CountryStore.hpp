#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

class World;

struct TaxPolicy {
    std::int32_t consumption_tax_ppm = 0;
    std::int32_t land_tax_ppm = 0;
    std::int32_t per_capita_tax_ppm = 0;
    std::int32_t income_tax_ppm = 200'000;
    std::int32_t dividends_tax_ppm = 0;
};

enum class CreditRating : std::uint8_t {
    AAA = 0,
    AA  = 1,
    A   = 2,
    BBB = 3,
    BB  = 4,
    B   = 5,
    CCC = 6,
    D   = 7
};

struct CountryInit {
    std::string tag;
    double population = 0.0;
    double gdp = 0.0;
    double treasury = 0.0;
    double tax_rate = 0.20;
    CurrencyKey primary_currency = default_currency_key;
    EconomyAmount foreign_reserves_milli = 0;
    double prestige = 0.0;
    EconomyAmount national_debt_milli = 0;
};

inline constexpr EconomyAmount building_credit_limit_milli = -500'000;

class CountryStore {
public:
    CountryId create(CountryInit init);
    void reserve(std::size_t count);

    [[nodiscard]] std::size_t size() const noexcept { return tags_.size(); }
    [[nodiscard]] std::string_view tag(CountryId id) const;
    [[nodiscard]] double population(CountryId id) const;
    [[nodiscard]] double gdp(CountryId id) const;
    [[nodiscard]] double nominal_gdp(CountryId id) const;
    [[nodiscard]] EconomyAmount nominal_gdp_milli(CountryId id) const;
    [[nodiscard]] double treasury(CountryId id) const;
    [[nodiscard]] EconomyAmount treasury_milli(CountryId id) const;
    [[nodiscard]] double tax_rate(CountryId id) const;
    [[nodiscard]] const TaxPolicy& tax_policy(CountryId id) const;

    [[nodiscard]] double prestige(CountryId id) const;
    void set_prestige(CountryId id, double value);
    void add_prestige(CountryId id, double delta);
    [[nodiscard]] double power_score(CountryId id) const;

    [[nodiscard]] CurrencyKey primary_currency(CountryId id) const;
    void set_primary_currency(CountryId id, CurrencyKey key);
    [[nodiscard]] EconomyAmount foreign_reserves_milli(CountryId id) const;
    void set_foreign_reserves_milli(CountryId id, EconomyAmount amount);
    void add_foreign_reserves_milli(CountryId id, EconomyAmount delta);
    [[nodiscard]] EconomyAmount balance_of_payments_milli(CountryId id) const;
    void set_balance_of_payments_milli(CountryId id, EconomyAmount amount);
    void add_balance_of_payments_milli(CountryId id, EconomyAmount delta);

    [[nodiscard]] EconomyAmount national_debt_milli(CountryId id) const;
    void set_national_debt_milli(CountryId id, EconomyAmount debt);
    void add_national_debt_milli(CountryId id, EconomyAmount delta);
    [[nodiscard]] CreditRating credit_rating(CountryId id) const;
    void set_credit_rating(CountryId id, CreditRating rating);
    [[nodiscard]] EconomyPrice bond_yield_ppm(CountryId id) const;
    void set_bond_yield_ppm(CountryId id, EconomyPrice yield_ppm);
    [[nodiscard]] std::uint16_t default_weeks(CountryId id) const;
    void set_default_weeks(CountryId id, std::uint16_t weeks);
    [[nodiscard]] bool is_in_default(CountryId id) const;
    [[nodiscard]] EconomyAmount weekly_debt_service_milli(CountryId id) const;
    [[nodiscard]] EconomyAmount borrowing_capacity_milli(CountryId id) const;

    EconomyAmount issue_sovereign_bonds(CountryId id, EconomyAmount requested_amount, World& world);
    EconomyAmount repay_sovereign_debt(CountryId id, EconomyAmount repayment_amount, World& world);
    void evaluate_credit_rating(CountryId id);

    [[nodiscard]] std::span<const double> populations() const noexcept { return population_; }
    [[nodiscard]] std::span<const double> gdps() const noexcept { return gdp_; }
    [[nodiscard]] std::span<const EconomyAmount> nominal_gdps() const noexcept { return nominal_gdp_milli_; }
    [[nodiscard]] std::span<const double> treasuries() const noexcept { return treasury_; }
    [[nodiscard]] std::span<const EconomyAmount> treasuries_milli() const noexcept { return treasury_milli_; }
    [[nodiscard]] std::span<const double> tax_rates() const noexcept { return tax_rate_; }
    [[nodiscard]] std::span<const double> prestiges() const noexcept { return prestige_; }
    [[nodiscard]] std::span<const CurrencyKey> primary_currencies() const noexcept { return primary_currencies_; }
    [[nodiscard]] std::span<const EconomyAmount> foreign_reserves() const noexcept { return foreign_reserves_milli_; }
    [[nodiscard]] std::span<const EconomyAmount> balance_of_payments() const noexcept { return balance_of_payments_milli_; }
    [[nodiscard]] std::span<const EconomyAmount> national_debts() const noexcept { return national_debt_milli_; }
    [[nodiscard]] std::span<const CreditRating> credit_ratings() const noexcept { return credit_ratings_; }
    [[nodiscard]] std::span<const EconomyPrice> bond_yields() const noexcept { return bond_yields_ppm_; }

    void set_population(CountryId id, double value);
    void set_gdp(CountryId id, double value);
    void set_nominal_gdp_milli(CountryId id, EconomyAmount amount_milli);
    void set_treasury(CountryId id, double value);
    void set_treasury_milli(CountryId id, EconomyAmount amount_milli);
    void set_tax_rate(CountryId id, double value);
    void set_tax_policy(CountryId id, TaxPolicy policy);
    void add_treasury(CountryId id, double delta);
    void add_treasury_milli(CountryId id, EconomyAmount delta_milli);

    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] std::size_t idx(CountryId id) const;

    std::vector<std::string> tags_;
    std::vector<double> population_;
    std::vector<double> gdp_;
    std::vector<EconomyAmount> nominal_gdp_milli_;
    std::vector<double> treasury_;
    std::vector<EconomyAmount> treasury_milli_;
    std::vector<double> tax_rate_;
    std::vector<double> prestige_;
    std::vector<TaxPolicy> tax_policies_;
    std::vector<CurrencyKey> primary_currencies_;
    std::vector<EconomyAmount> foreign_reserves_milli_;
    std::vector<EconomyAmount> balance_of_payments_milli_;
    std::vector<EconomyAmount> national_debt_milli_;
    std::vector<CreditRating> credit_ratings_;
    std::vector<EconomyPrice> bond_yields_ppm_;
    std::vector<std::uint16_t> default_weeks_;
};

} // namespace thunder
