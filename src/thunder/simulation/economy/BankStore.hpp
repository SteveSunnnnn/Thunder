#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string_view>
#include <vector>

namespace thunder {

class World;

using BankKey = std::uint64_t;
using BankLoanKey = std::uint64_t;

enum class BankStatus : std::uint8_t { Active = 0, Restricted = 1, Insolvent = 2 };
enum class BankBorrowerKind : std::uint8_t { Building = 0 };
enum class BankLoanStatus : std::uint8_t { Performing = 0, NonPerforming = 1, Defaulted = 2, Repaid = 3 };

struct BankInit {
    BankKey key = 0;
    CountryId country{};
    CurrencyKey currency = default_currency_key;
    EconomyAmount reserves_milli = 0;
    EconomyAmount deposits_milli = 0;
    std::int32_t reserve_requirement_ppm = 100'000;
    std::int32_t capital_requirement_ppm = 80'000;
    EconomyPrice deposit_rate_ppm = 10'000;
    EconomyPrice loan_rate_ppm = 50'000;
};

struct BankSnapshot {
    BankKey key = 0;
    CountryId country{};
    CurrencyKey currency = default_currency_key;
    EconomyAmount reserves_milli = 0;
    EconomyAmount deposits_milli = 0;
    EconomyAmount equity_milli = 0;
    EconomyAmount loan_assets_milli = 0;
    EconomyAmount sovereign_bonds_milli = 0;
    EconomyAmount nonperforming_milli = 0;
    EconomyAmount retained_earnings_milli = 0;
    std::int32_t reserve_requirement_ppm = 100'000;
    std::int32_t capital_requirement_ppm = 80'000;
    EconomyPrice deposit_rate_ppm = 10'000;
    EconomyPrice loan_rate_ppm = 50'000;
    BankStatus status = BankStatus::Active;
};

struct BankLoanSnapshot {
    BankLoanKey key = 0;
    BankId bank{};
    BankBorrowerKind borrower_kind = BankBorrowerKind::Building;
    std::uint32_t borrower_id = 0;
    EconomyAmount principal_milli = 0;
    EconomyPrice annual_rate_ppm = 50'000;
    std::uint16_t remaining_weeks = 260;
    std::uint16_t arrears_weeks = 0;
    BankLoanStatus status = BankLoanStatus::Performing;
};

[[nodiscard]] constexpr BankKey bank_stable_key(std::string_view text) noexcept {
    return economy_stable_key(text);
}

[[nodiscard]] BankLoanKey building_loan_stable_key(BankKey bank, BuildingId building) noexcept;

class BankStore {
public:
    BankId create(BankInit init);
    BankId restore_bank(const BankSnapshot& snapshot);
    BankLoanId restore_loan(const BankLoanSnapshot& snapshot);
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return keys_.size(); }
    [[nodiscard]] std::size_t loan_count() const noexcept { return loan_keys_.size(); }
    [[nodiscard]] BankSnapshot bank(BankId id) const;
    [[nodiscard]] BankLoanSnapshot loan(BankLoanId id) const;
    [[nodiscard]] BankId find(BankKey key) const noexcept;
    [[nodiscard]] BankId primary_bank(CountryId country, CurrencyKey currency) const noexcept;
    [[nodiscard]] EconomyAmount lendable_capacity(BankId bank) const noexcept;
    [[nodiscard]] bool balance_sheet_balanced(BankId bank) const noexcept;

    // Book-entry credit creates a matching customer deposit and loan asset;
    // reserve and capital constraints cap the funded amount.
    [[nodiscard]] EconomyAmount fund_building(BankId bank, BuildingId borrower,
                                              EconomyAmount requested_milli,
                                              EconomyPrice annual_rate_ppm = 0,
                                              std::uint16_t term_weeks = 260);
    [[nodiscard]] EconomyAmount fund_sovereign_bonds(CountryId borrower, CurrencyKey currency,
                                                     EconomyAmount requested_milli,
                                                     EconomyPrice annual_rate_ppm);
    [[nodiscard]] EconomyAmount receive_sovereign_interest(CountryId borrower,
                                                           EconomyAmount payment_milli) noexcept;
    [[nodiscard]] EconomyAmount redeem_sovereign_bonds(CountryId borrower,
                                                        EconomyAmount principal_milli) noexcept;
    // R8: pro-rata write-down of this country's sovereign bond holdings across
    // its domestic banks (assets and equity fall together; the balance sheet
    // stays balanced). Returns the amount actually written down.
    [[nodiscard]] EconomyAmount write_down_sovereign_bonds(CountryId borrower,
                                                          EconomyAmount amount_milli) noexcept;
    // R10: worst outstanding loan status for a building borrower across all
    // banks (Performing when it has no live contract). Bankruptcy only
    // destroys a one-level building whose debt has demonstrably failed.
    [[nodiscard]] BankLoanStatus building_loan_status(BuildingId building) const noexcept;

    // Services building loans and deposit interest in stable LoanId/BankId
    // order. This is deliberately outside the high-cardinality market jobs.
    void run_weekly(World& world);

    [[nodiscard]] bool validate(std::size_t countries, std::size_t buildings) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] std::size_t bank_index(BankId id) const;
    [[nodiscard]] std::size_t loan_index(BankLoanId id) const;
    void update_status(std::size_t bank) noexcept;
    void charge_off(std::size_t loan) noexcept;

    std::vector<BankKey> keys_;
    std::vector<CountryId> countries_;
    std::vector<CurrencyKey> currencies_;
    std::vector<EconomyAmount> reserves_milli_;
    std::vector<EconomyAmount> deposits_milli_;
    std::vector<EconomyAmount> equity_milli_;
    std::vector<EconomyAmount> loan_assets_milli_;
    std::vector<EconomyAmount> sovereign_bonds_milli_;
    std::vector<EconomyAmount> nonperforming_milli_;
    std::vector<EconomyAmount> retained_earnings_milli_;
    std::vector<std::int32_t> reserve_requirement_ppm_;
    std::vector<std::int32_t> capital_requirement_ppm_;
    std::vector<EconomyPrice> deposit_rate_ppm_;
    std::vector<EconomyPrice> loan_rate_ppm_;
    std::vector<BankStatus> statuses_;

    std::vector<BankLoanKey> loan_keys_;
    std::vector<BankId> loan_banks_;
    std::vector<BankBorrowerKind> borrower_kinds_;
    std::vector<std::uint32_t> borrower_ids_;
    std::vector<EconomyAmount> principal_milli_;
    std::vector<EconomyPrice> annual_rate_ppm_;
    std::vector<std::uint16_t> remaining_weeks_;
    std::vector<std::uint16_t> arrears_weeks_;
    std::vector<BankLoanStatus> loan_statuses_;
    // Non-authoritative lookup accelerators. Iteration/checksum/save always
    // use the stable SoA order above.
    std::map<BankKey, BankId> key_index_;
    std::map<std::pair<std::uint32_t, CurrencyKey>, BankId> primary_index_;
};

} // namespace thunder
