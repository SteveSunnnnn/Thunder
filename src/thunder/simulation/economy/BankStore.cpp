#include "thunder/simulation/economy/BankStore.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace thunder {

namespace {
constexpr EconomyAmount weeks_per_year = 52;
constexpr std::uint16_t npl_after_weeks = 4;
constexpr std::uint16_t default_after_weeks = 12;
}

BankLoanKey building_loan_stable_key(BankKey bank, BuildingId building) noexcept {
    Fnv1a64 h;
    h.add(bank);
    h.add(static_cast<std::uint8_t>(BankBorrowerKind::Building));
    h.add(building.value());
    const auto value = h.value();
    return value == 0u ? 1u : value;
}

void BankStore::clear() noexcept { *this = BankStore{}; }

BankId BankStore::create(BankInit init) {
    if (init.key == 0u || find(init.key).valid()) throw std::invalid_argument("bank key must be unique and non-zero");
    if (!init.country.valid() || init.currency == 0u) throw std::invalid_argument("bank owner and currency must be valid");
    if (init.reserves_milli < 0 || init.deposits_milli < 0 || init.reserves_milli < init.deposits_milli)
        throw std::invalid_argument("new bank requires non-negative, fully backed opening liabilities");
    BankSnapshot snapshot;
    snapshot.key = init.key;
    snapshot.country = init.country;
    snapshot.currency = init.currency;
    snapshot.reserves_milli = init.reserves_milli;
    snapshot.deposits_milli = init.deposits_milli;
    snapshot.equity_milli = init.reserves_milli - init.deposits_milli;
    snapshot.reserve_requirement_ppm = std::clamp(init.reserve_requirement_ppm, 0, 1'000'000);
    snapshot.capital_requirement_ppm = std::clamp(init.capital_requirement_ppm, 1, 1'000'000);
    snapshot.deposit_rate_ppm = std::clamp<EconomyPrice>(init.deposit_rate_ppm, 0, 1'000'000);
    snapshot.loan_rate_ppm = std::clamp<EconomyPrice>(init.loan_rate_ppm, 0, 2'000'000);
    return restore_bank(snapshot);
}

BankId BankStore::restore_bank(const BankSnapshot& s) {
    if (s.key == 0u || find(s.key).valid()) throw std::invalid_argument("restored bank key must be unique and non-zero");
    if (!s.country.valid() || s.currency == 0u || s.reserves_milli < 0 || s.deposits_milli < 0 ||
        s.loan_assets_milli < 0 || s.sovereign_bonds_milli < 0 || s.nonperforming_milli < 0 ||
        s.nonperforming_milli > s.loan_assets_milli || s.reserve_requirement_ppm < 0 ||
        s.reserve_requirement_ppm > 1'000'000 || s.capital_requirement_ppm <= 0 ||
        s.capital_requirement_ppm > 1'000'000 || static_cast<std::uint8_t>(s.status) > 2u)
        throw std::invalid_argument("invalid restored bank state");
    const auto assets = saturating_add(s.reserves_milli, saturating_add(s.loan_assets_milli, s.sovereign_bonds_milli));
    const auto claims = saturating_add(s.deposits_milli, s.equity_milli);
    if (assets != claims) throw std::invalid_argument("restored bank balance sheet does not balance");
    const auto expected_status = s.equity_milli < 0
        ? BankStatus::Insolvent
        : s.nonperforming_milli > s.equity_milli ? BankStatus::Restricted : BankStatus::Active;
    if (s.status != expected_status)
        throw std::invalid_argument("restored bank status does not match balance sheet");
    const BankId id{static_cast<BankId::rep_type>(keys_.size())};
    keys_.push_back(s.key); countries_.push_back(s.country); currencies_.push_back(s.currency);
    reserves_milli_.push_back(s.reserves_milli); deposits_milli_.push_back(s.deposits_milli);
    equity_milli_.push_back(s.equity_milli); loan_assets_milli_.push_back(s.loan_assets_milli);
    sovereign_bonds_milli_.push_back(s.sovereign_bonds_milli); nonperforming_milli_.push_back(s.nonperforming_milli);
    retained_earnings_milli_.push_back(s.retained_earnings_milli);
    reserve_requirement_ppm_.push_back(s.reserve_requirement_ppm);
    capital_requirement_ppm_.push_back(s.capital_requirement_ppm);
    deposit_rate_ppm_.push_back(s.deposit_rate_ppm); loan_rate_ppm_.push_back(s.loan_rate_ppm);
    statuses_.push_back(s.status);
    key_index_.emplace(s.key, id);
    const auto zone = std::pair{s.country.value(), s.currency};
    const auto existing = primary_index_.find(zone);
    if (existing == primary_index_.end() || keys_[existing->second.value()] > s.key)
        primary_index_[zone] = id;
    return id;
}

BankLoanId BankStore::restore_loan(const BankLoanSnapshot& s) {
    if (s.key == 0u || !s.bank.valid() || static_cast<std::size_t>(s.bank.value()) >= size() ||
        s.principal_milli < 0 || s.annual_rate_ppm < 0 || s.annual_rate_ppm > 2'000'000 ||
        static_cast<std::uint8_t>(s.borrower_kind) > 0u || static_cast<std::uint8_t>(s.status) > 3u ||
        ((s.status == BankLoanStatus::Defaulted || s.status == BankLoanStatus::Repaid) &&
         s.principal_milli != 0))
        throw std::invalid_argument("invalid restored bank loan");
    if (std::find(loan_keys_.begin(), loan_keys_.end(), s.key) != loan_keys_.end())
        throw std::invalid_argument("bank loan key must be unique");
    const BankLoanId id{static_cast<BankLoanId::rep_type>(loan_keys_.size())};
    loan_keys_.push_back(s.key); loan_banks_.push_back(s.bank); borrower_kinds_.push_back(s.borrower_kind);
    borrower_ids_.push_back(s.borrower_id); principal_milli_.push_back(s.principal_milli);
    annual_rate_ppm_.push_back(s.annual_rate_ppm); remaining_weeks_.push_back(s.remaining_weeks);
    arrears_weeks_.push_back(s.arrears_weeks); loan_statuses_.push_back(s.status);
    return id;
}

std::size_t BankStore::bank_index(BankId id) const {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= size()) throw std::out_of_range("invalid BankId");
    return i;
}
std::size_t BankStore::loan_index(BankLoanId id) const {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= loan_count()) throw std::out_of_range("invalid BankLoanId");
    return i;
}

BankSnapshot BankStore::bank(BankId id) const {
    const auto i = bank_index(id);
    return {keys_[i], countries_[i], currencies_[i], reserves_milli_[i], deposits_milli_[i],
            equity_milli_[i], loan_assets_milli_[i], sovereign_bonds_milli_[i], nonperforming_milli_[i],
            retained_earnings_milli_[i], reserve_requirement_ppm_[i], capital_requirement_ppm_[i],
            deposit_rate_ppm_[i], loan_rate_ppm_[i], statuses_[i]};
}
BankLoanSnapshot BankStore::loan(BankLoanId id) const {
    const auto i = loan_index(id);
    return {loan_keys_[i], loan_banks_[i], borrower_kinds_[i], borrower_ids_[i], principal_milli_[i],
            annual_rate_ppm_[i], remaining_weeks_[i], arrears_weeks_[i], loan_statuses_[i]};
}

BankId BankStore::find(BankKey key) const noexcept {
    const auto it = key_index_.find(key);
    return it == key_index_.end() ? BankId{} : it->second;
}

BankId BankStore::primary_bank(CountryId country, CurrencyKey currency) const noexcept {
    const auto indexed = primary_index_.find({country.value(), currency});
    if (indexed != primary_index_.end() && statuses_[indexed->second.value()] == BankStatus::Active)
        return indexed->second;
    BankId best{};
    BankKey best_key = std::numeric_limits<BankKey>::max();
    for (std::size_t i = 0; i < size(); ++i) {
        if (countries_[i] == country && currencies_[i] == currency && statuses_[i] == BankStatus::Active &&
            keys_[i] < best_key) { best = BankId{static_cast<BankId::rep_type>(i)}; best_key = keys_[i]; }
    }
    return best;
}

EconomyAmount BankStore::lendable_capacity(BankId id) const noexcept {
    if (!id.valid() || static_cast<std::size_t>(id.value()) >= size()) return 0;
    const auto i = static_cast<std::size_t>(id.value());
    if (statuses_[i] != BankStatus::Active || equity_milli_[i] <= 0) return 0;
    EconomyAmount liquidity_capacity = std::numeric_limits<EconomyAmount>::max();
    if (reserve_requirement_ppm_[i] > 0) {
        const auto max_deposits = mul_div_nonnegative(reserves_milli_[i], ppm_scale, reserve_requirement_ppm_[i]);
        liquidity_capacity = std::max<EconomyAmount>(0, saturating_sub(max_deposits, deposits_milli_[i]));
    }
    const auto max_risk_assets = mul_div_nonnegative(equity_milli_[i], ppm_scale, capital_requirement_ppm_[i]);
    const auto risk_assets = saturating_add(loan_assets_milli_[i], sovereign_bonds_milli_[i]);
    const auto capital_capacity = std::max<EconomyAmount>(0, saturating_sub(max_risk_assets, risk_assets));
    return std::min(liquidity_capacity, capital_capacity);
}

bool BankStore::balance_sheet_balanced(BankId id) const noexcept {
    if (!id.valid() || static_cast<std::size_t>(id.value()) >= size()) return false;
    const auto i = static_cast<std::size_t>(id.value());
    return saturating_add(reserves_milli_[i], saturating_add(loan_assets_milli_[i], sovereign_bonds_milli_[i])) ==
           saturating_add(deposits_milli_[i], equity_milli_[i]);
}

EconomyAmount BankStore::fund_building(BankId id, BuildingId borrower, EconomyAmount requested,
                                       EconomyPrice rate, std::uint16_t term) {
    if (!borrower.valid() || requested <= 0) return 0;
    const auto bi = bank_index(id);
    const auto funded = std::min(requested, lendable_capacity(id));
    if (funded <= 0) return 0;
    const auto key = building_loan_stable_key(keys_[bi], borrower);
    std::size_t loan_i = loan_count();
    for (std::size_t i = 0; i < loan_count(); ++i) {
        if (loan_keys_[i] == key && loan_statuses_[i] != BankLoanStatus::Defaulted &&
            loan_statuses_[i] != BankLoanStatus::Repaid) { loan_i = i; break; }
    }
    if (loan_i == loan_count()) {
        // A stable key identifies the bank/building contract, not each draw.
        // Reuse a closed row when a previous contract was repaid/defaulted;
        // appending a second row with the same key would make the next draw
        // fail validation and break long-running construction cycles.
        for (std::size_t i = 0; i < loan_count(); ++i) {
            if (loan_keys_[i] == key &&
                (loan_statuses_[i] == BankLoanStatus::Defaulted ||
                 loan_statuses_[i] == BankLoanStatus::Repaid)) {
                loan_i = i;
                loan_banks_[i] = id;
                borrower_kinds_[i] = BankBorrowerKind::Building;
                borrower_ids_[i] = borrower.value();
                principal_milli_[i] = 0;
                annual_rate_ppm_[i] = rate > 0 ? rate : loan_rate_ppm_[bi];
                remaining_weeks_[i] = std::max<std::uint16_t>(1, term);
                arrears_weeks_[i] = 0;
                loan_statuses_[i] = BankLoanStatus::Performing;
                break;
            }
        }
        if (loan_i == loan_count()) {
            restore_loan({key, id, BankBorrowerKind::Building, borrower.value(), 0,
                          rate > 0 ? rate : loan_rate_ppm_[bi], std::max<std::uint16_t>(1, term), 0,
                          BankLoanStatus::Performing});
            loan_i = loan_count() - 1u;
        }
    }
    principal_milli_[loan_i] = saturating_add(principal_milli_[loan_i], funded);
    remaining_weeks_[loan_i] = std::max(remaining_weeks_[loan_i], std::max<std::uint16_t>(1, term));
    loan_assets_milli_[bi] = saturating_add(loan_assets_milli_[bi], funded);
    deposits_milli_[bi] = saturating_add(deposits_milli_[bi], funded);
    // Additional draws on an already non-performing contract remain
    // non-performing. Keeping the NPL bucket in lockstep with principal is
    // required for balance-sheet validation and for capital-ratio gating.
    if (loan_statuses_[loan_i] == BankLoanStatus::NonPerforming)
        nonperforming_milli_[bi] = saturating_add(nonperforming_milli_[bi], funded);
    update_status(bi);
    return funded;
}

EconomyAmount BankStore::fund_sovereign_bonds(CountryId borrower, CurrencyKey currency,
                                               EconomyAmount requested, EconomyPrice) {
    if (!borrower.valid() || requested <= 0) return 0;
    const auto id = primary_bank(borrower, currency);
    if (!id.valid()) return 0;
    const auto i = static_cast<std::size_t>(id.value());
    const auto funded = std::min(requested, lendable_capacity(id));
    if (funded <= 0) return 0;
    sovereign_bonds_milli_[i] = saturating_add(sovereign_bonds_milli_[i], funded);
    deposits_milli_[i] = saturating_add(deposits_milli_[i], funded);
    return funded;
}

EconomyAmount BankStore::receive_sovereign_interest(CountryId borrower, EconomyAmount payment) noexcept {
    EconomyAmount remaining = std::max<EconomyAmount>(0, payment), received = 0;
    // Pre-scan: compute total sovereign bond holdings for this borrower to
    // distribute interest proportionally.
    EconomyAmount total_bonds = 0;
    for (std::size_t i = 0; i < size(); ++i)
        if (countries_[i] == borrower && sovereign_bonds_milli_[i] > 0)
            total_bonds = saturating_add(total_bonds, sovereign_bonds_milli_[i]);
    if (total_bonds <= 0) return 0;
    for (std::size_t i = 0; i < size() && remaining > 0; ++i) {
        if (countries_[i] != borrower || sovereign_bonds_milli_[i] <= 0) continue;
        // Proportional share of interest, capped by remaining payment.
        const auto share = mul_div_nonnegative(payment, sovereign_bonds_milli_[i], total_bonds);
        const auto leg = std::min(remaining, share);
        // Bank receives interest as asset increase (reserves) + equity,
        // matching the treasury's debit.  Deposits are unchanged — the
        // interest is new income, not a liability transfer.
        reserves_milli_[i] = saturating_add(reserves_milli_[i], leg);
        equity_milli_[i] = saturating_add(equity_milli_[i], leg);
        retained_earnings_milli_[i] = saturating_add(retained_earnings_milli_[i], leg);
        remaining = saturating_sub(remaining, leg); received = saturating_add(received, leg);
        update_status(i);
    }
    return received;
}

EconomyAmount BankStore::redeem_sovereign_bonds(CountryId borrower, EconomyAmount principal) noexcept {
    EconomyAmount remaining = std::max<EconomyAmount>(0, principal), redeemed = 0;
    for (std::size_t i = 0; i < size() && remaining > 0; ++i) {
        if (countries_[i] != borrower || sovereign_bonds_milli_[i] <= 0) continue;
        const auto leg = std::min({remaining, sovereign_bonds_milli_[i], deposits_milli_[i]});
        sovereign_bonds_milli_[i] = saturating_sub(sovereign_bonds_milli_[i], leg);
        deposits_milli_[i] = saturating_sub(deposits_milli_[i], leg);
        remaining = saturating_sub(remaining, leg); redeemed = saturating_add(redeemed, leg);
    }
    return redeemed;
}

EconomyAmount BankStore::write_down_sovereign_bonds(CountryId borrower, EconomyAmount amount) noexcept {
    const auto target = std::max<EconomyAmount>(0, amount);
    if (!borrower.valid() || target <= 0) return 0;
    EconomyAmount total_bonds = 0;
    for (std::size_t i = 0; i < size(); ++i)
        if (countries_[i] == borrower && sovereign_bonds_milli_[i] > 0)
            total_bonds = saturating_add(total_bonds, sovereign_bonds_milli_[i]);
    if (total_bonds <= 0) return 0;
    EconomyAmount written = 0;
    for (std::size_t i = 0; i < size(); ++i) {
        if (countries_[i] != borrower || sovereign_bonds_milli_[i] <= 0) continue;
        // Pro-rata leg over the pre-write-down holdings (deterministic order).
        const auto leg = std::min(sovereign_bonds_milli_[i],
            mul_div_nonnegative(target, sovereign_bonds_milli_[i], total_bonds));
        if (leg <= 0) continue;
        sovereign_bonds_milli_[i] = saturating_sub(sovereign_bonds_milli_[i], leg);
        equity_milli_[i] = saturating_sub(equity_milli_[i], leg);
        retained_earnings_milli_[i] = saturating_sub(retained_earnings_milli_[i], leg);
        written = saturating_add(written, leg);
        update_status(i);
    }
    return written;
}

BankLoanStatus BankStore::building_loan_status(BuildingId building) const noexcept {
    if (!building.valid()) return BankLoanStatus::Performing;
    BankLoanStatus worst = BankLoanStatus::Performing;
    for (std::size_t i = 0; i < loan_count(); ++i) {
        if (borrower_kinds_[i] != BankBorrowerKind::Building) continue;
        if (borrower_ids_[i] != building.value()) continue;
        if (loan_statuses_[i] == BankLoanStatus::Repaid) continue;
        const auto status = loan_statuses_[i];
        if (static_cast<std::uint8_t>(status) > static_cast<std::uint8_t>(worst)) worst = status;
        if (worst == BankLoanStatus::Defaulted) break;
    }
    return worst;
}

void BankStore::charge_off(std::size_t li) noexcept {
    if (li >= loan_count()) return;
    const auto bi = static_cast<std::size_t>(loan_banks_[li].value());
    const auto loss = principal_milli_[li];
    if (bi >= size()) {
        principal_milli_[li] = 0;
        loan_statuses_[li] = BankLoanStatus::Defaulted;
        return;
    }
    loan_assets_milli_[bi] = saturating_sub(loan_assets_milli_[bi], loss);
    nonperforming_milli_[bi] = saturating_sub(nonperforming_milli_[bi], std::min(nonperforming_milli_[bi], loss));
    equity_milli_[bi] = saturating_sub(equity_milli_[bi], loss);
    principal_milli_[li] = 0;
    loan_statuses_[li] = BankLoanStatus::Defaulted;
    update_status(bi);
}

void BankStore::update_status(std::size_t i) noexcept {
    if (equity_milli_[i] < 0) statuses_[i] = BankStatus::Insolvent;
    else if (nonperforming_milli_[i] > equity_milli_[i]) statuses_[i] = BankStatus::Restricted;
    else statuses_[i] = BankStatus::Active;
}

void BankStore::run_weekly(World& world) {
    for (std::size_t li = 0; li < loan_count(); ++li) {
        if (loan_statuses_[li] == BankLoanStatus::Defaulted || loan_statuses_[li] == BankLoanStatus::Repaid ||
            principal_milli_[li] <= 0) continue;
        const auto bi = static_cast<std::size_t>(loan_banks_[li].value());
        if (bi >= size() || borrower_kinds_[li] != BankBorrowerKind::Building ||
            borrower_ids_[li] >= world.buildings.size() ||
            !world.buildings.slot_pool().is_index_alive(borrower_ids_[li])) {
            charge_off(li); continue;
        }
        const BuildingId building{borrower_ids_[li]};
        const auto interest_due = mul_div_nonnegative(principal_milli_[li], annual_rate_ppm_[li], weeks_per_year * ppm_scale);
        const auto principal_due = remaining_weeks_[li] > 0
            ? std::max<EconomyAmount>(1, principal_milli_[li] / remaining_weeks_[li]) : principal_milli_[li];
        const auto due = saturating_add(interest_due, principal_due);
        const auto available = std::max<EconomyAmount>(0, world.buildings.cash(building));
        const auto payment = std::min({due, available, deposits_milli_[bi]});
        if (payment > 0) world.buildings.cash_mut()[building.value()] =
            saturating_sub(world.buildings.cash(building), payment);
        const auto interest_paid = std::min(payment, interest_due);
        const auto principal_paid = std::min(saturating_sub(payment, interest_paid), principal_milli_[li]);
        deposits_milli_[bi] = saturating_sub(deposits_milli_[bi], payment);
        principal_milli_[li] = saturating_sub(principal_milli_[li], principal_paid);
        loan_assets_milli_[bi] = saturating_sub(loan_assets_milli_[bi], principal_paid);
        if (loan_statuses_[li] == BankLoanStatus::NonPerforming)
            nonperforming_milli_[bi] = saturating_sub(
                nonperforming_milli_[bi], std::min(nonperforming_milli_[bi], principal_paid));
        equity_milli_[bi] = saturating_add(equity_milli_[bi], interest_paid);
        retained_earnings_milli_[bi] = saturating_add(retained_earnings_milli_[bi], interest_paid);
        if (payment < due) {
            arrears_weeks_[li] = static_cast<std::uint16_t>(std::min<unsigned>(65535u, arrears_weeks_[li] + 1u));
            if (arrears_weeks_[li] == npl_after_weeks) {
                loan_statuses_[li] = BankLoanStatus::NonPerforming;
                nonperforming_milli_[bi] = saturating_add(nonperforming_milli_[bi], principal_milli_[li]);
            }
            if (arrears_weeks_[li] >= default_after_weeks) { charge_off(li); continue; }
        } else if (arrears_weeks_[li] > 0) {
            // Curing the arrears returns the loan to Performing, so it stops
            // contributing to the NPL bucket. The bucket was seeded with the
            // full outstanding principal at write-down time and the repayments
            // above only debited the portions actually paid, so the remaining
            // principal is still booked as non-performing. Clear it now:
            // validation requires the bucket to equal the sum of principal
            // over loans still marked NonPerforming, and leaving the residue
            // behind also inflates the NPL ratio enough to gate lending for an
            // otherwise healthy bank.
            if (loan_statuses_[li] == BankLoanStatus::NonPerforming)
                nonperforming_milli_[bi] = saturating_sub(
                    nonperforming_milli_[bi],
                    std::min(nonperforming_milli_[bi], principal_milli_[li]));
            arrears_weeks_[li] = 0; loan_statuses_[li] = BankLoanStatus::Performing;
        }
        if (remaining_weeks_[li] > 0) --remaining_weeks_[li];
        if (principal_milli_[li] == 0) loan_statuses_[li] = BankLoanStatus::Repaid;
        update_status(bi);
    }
    // Deposit interest is credited to the domestic investment pool. The bank
    // books an equal liability increase and equity expense: no money appears
    // without a matching bank claim.
    for (std::size_t bi = 0; bi < size(); ++bi) {
        if (statuses_[bi] == BankStatus::Insolvent || deposits_milli_[bi] <= 0 || equity_milli_[bi] <= 0) continue;
        const auto interest = std::min(equity_milli_[bi], mul_div_nonnegative(
            deposits_milli_[bi], deposit_rate_ppm_[bi], weeks_per_year * ppm_scale));
        if (interest <= 0) continue;
        deposits_milli_[bi] = saturating_add(deposits_milli_[bi], interest);
        equity_milli_[bi] = saturating_sub(equity_milli_[bi], interest);
        retained_earnings_milli_[bi] = saturating_sub(retained_earnings_milli_[bi], interest);
        world.grand_strategy.add_investment_pool_funds(countries_[bi], interest);
        update_status(bi);
    }
}

bool BankStore::validate(std::size_t countries, std::size_t buildings) const noexcept {
    const auto n = size();
    if (countries_.size()!=n || currencies_.size()!=n || reserves_milli_.size()!=n || deposits_milli_.size()!=n ||
        equity_milli_.size()!=n || loan_assets_milli_.size()!=n || sovereign_bonds_milli_.size()!=n ||
        nonperforming_milli_.size()!=n || retained_earnings_milli_.size()!=n || reserve_requirement_ppm_.size()!=n ||
        capital_requirement_ppm_.size()!=n || deposit_rate_ppm_.size()!=n || loan_rate_ppm_.size()!=n || statuses_.size()!=n) return false;
    for (std::size_t i=0;i<n;++i) {
        if (keys_[i]==0u || countries_[i].value()>=countries || currencies_[i]==0u ||
            static_cast<std::uint8_t>(statuses_[i]) > 2u ||
            reserve_requirement_ppm_[i] < 0 || reserve_requirement_ppm_[i] > 1'000'000 ||
            capital_requirement_ppm_[i] <= 0 || capital_requirement_ppm_[i] > 1'000'000 ||
            deposit_rate_ppm_[i] < 0 || deposit_rate_ppm_[i] > 1'000'000 ||
            loan_rate_ppm_[i] < 0 || loan_rate_ppm_[i] > 2'000'000 || reserves_milli_[i]<0 ||
            deposits_milli_[i]<0 || loan_assets_milli_[i]<0 || sovereign_bonds_milli_[i]<0 ||
            nonperforming_milli_[i]<0 || nonperforming_milli_[i]>loan_assets_milli_[i] || !balance_sheet_balanced(BankId{static_cast<BankId::rep_type>(i)})) return false;
        const auto expected_status = equity_milli_[i] < 0
            ? BankStatus::Insolvent
            : nonperforming_milli_[i] > equity_milli_[i] ? BankStatus::Restricted : BankStatus::Active;
        if (statuses_[i] != expected_status) return false;
        for (std::size_t j=0;j<i;++j) if (keys_[i]==keys_[j]) return false;
    }
    const auto l=loan_count();
    if (loan_banks_.size()!=l || borrower_kinds_.size()!=l || borrower_ids_.size()!=l || principal_milli_.size()!=l ||
        annual_rate_ppm_.size()!=l || remaining_weeks_.size()!=l || arrears_weeks_.size()!=l || loan_statuses_.size()!=l) return false;
    std::vector<EconomyAmount> loan_sums(n,0), npl_sums(n,0);
    for(std::size_t i=0;i<l;++i){
        if(loan_keys_[i]==0u||loan_banks_[i].value()>=n||
           static_cast<std::uint8_t>(borrower_kinds_[i]) > 0u ||
           static_cast<std::uint8_t>(loan_statuses_[i]) > 3u ||
           annual_rate_ppm_[i] < 0 || annual_rate_ppm_[i] > 2'000'000 ||
           borrower_ids_[i]>=buildings||principal_milli_[i]<0 ||
           ((loan_statuses_[i] == BankLoanStatus::Defaulted ||
             loan_statuses_[i] == BankLoanStatus::Repaid) && principal_milli_[i] != 0))return false;
        for(std::size_t j=0;j<i;++j)if(loan_keys_[i]==loan_keys_[j])return false;
        const auto bi=loan_banks_[i].value(); loan_sums[bi]=saturating_add(loan_sums[bi],principal_milli_[i]);
        if(loan_statuses_[i]==BankLoanStatus::NonPerforming)npl_sums[bi]=saturating_add(npl_sums[bi],principal_milli_[i]);
    }
    for(std::size_t i=0;i<n;++i)if(loan_sums[i]!=loan_assets_milli_[i]||npl_sums[i]!=nonperforming_milli_[i])return false;
    return true;
}

std::uint64_t BankStore::checksum() const noexcept {
    Fnv1a64 h; h.add(size());
    for(std::size_t i=0;i<size();++i){h.add(keys_[i]);h.add(countries_[i].value());h.add(currencies_[i]);h.add(reserves_milli_[i]);h.add(deposits_milli_[i]);h.add(equity_milli_[i]);h.add(loan_assets_milli_[i]);h.add(sovereign_bonds_milli_[i]);h.add(nonperforming_milli_[i]);h.add(retained_earnings_milli_[i]);h.add(reserve_requirement_ppm_[i]);h.add(capital_requirement_ppm_[i]);h.add(deposit_rate_ppm_[i]);h.add(loan_rate_ppm_[i]);h.add(static_cast<std::uint8_t>(statuses_[i]));}
    h.add(loan_count());
    for(std::size_t i=0;i<loan_count();++i){h.add(loan_keys_[i]);h.add(loan_banks_[i].value());h.add(static_cast<std::uint8_t>(borrower_kinds_[i]));h.add(borrower_ids_[i]);h.add(principal_milli_[i]);h.add(annual_rate_ppm_[i]);h.add(remaining_weeks_[i]);h.add(arrears_weeks_[i]);h.add(static_cast<std::uint8_t>(loan_statuses_[i]));}
    return h.value();
}

std::size_t BankStore::memory_bytes() const noexcept {
    return sizeof(*this)+keys_.capacity()*sizeof(BankKey)+countries_.capacity()*sizeof(CountryId)+currencies_.capacity()*sizeof(CurrencyKey)+
        (reserves_milli_.capacity()+deposits_milli_.capacity()+equity_milli_.capacity()+loan_assets_milli_.capacity()+sovereign_bonds_milli_.capacity()+nonperforming_milli_.capacity()+retained_earnings_milli_.capacity())*sizeof(EconomyAmount)+
        (reserve_requirement_ppm_.capacity()+capital_requirement_ppm_.capacity())*sizeof(std::int32_t)+(deposit_rate_ppm_.capacity()+loan_rate_ppm_.capacity())*sizeof(EconomyPrice)+statuses_.capacity()*sizeof(BankStatus)+
        loan_keys_.capacity()*sizeof(BankLoanKey)+loan_banks_.capacity()*sizeof(BankId)+borrower_kinds_.capacity()*sizeof(BankBorrowerKind)+borrower_ids_.capacity()*sizeof(std::uint32_t)+principal_milli_.capacity()*sizeof(EconomyAmount)+annual_rate_ppm_.capacity()*sizeof(EconomyPrice)+remaining_weeks_.capacity()*sizeof(std::uint16_t)+arrears_weeks_.capacity()*sizeof(std::uint16_t)+loan_statuses_.capacity()*sizeof(BankLoanStatus)+
        key_index_.size()*(sizeof(std::pair<const BankKey,BankId>)+3u*sizeof(void*))+primary_index_.size()*(sizeof(std::pair<const std::pair<std::uint32_t,CurrencyKey>,BankId>)+3u*sizeof(void*));
}

} // namespace thunder
