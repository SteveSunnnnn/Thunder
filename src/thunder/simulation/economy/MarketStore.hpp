#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

class MarketStore {
public:
    void resize(std::size_t market_count, const EconomyDefinitions& definitions);
    [[nodiscard]] std::size_t size() const noexcept { return owners_.size(); }
    [[nodiscard]] std::size_t good_count() const noexcept { return good_count_; }

    void set_owner(MarketId market, CountryId country);
    [[nodiscard]] CountryId owner(MarketId market) const;
    // Each market owns a stable, saved clearing account. Payments move through
    // this account instead of disappearing on the buyer side and being recreated
    // independently on the producer side.
    [[nodiscard]] SettlementAccountId settlement_account(MarketId market) const;
    [[nodiscard]] CurrencyKey currency_key(MarketId market) const;
    [[nodiscard]] EconomyAmount clearing_cash(MarketId market) const;
    void set_currency_key(MarketId market, CurrencyKey key);
    void set_clearing_cash(MarketId market, EconomyAmount value);
    void add_clearing_cash(MarketId market, EconomyAmount delta);
    [[nodiscard]] EconomyPrice price(MarketId market, GoodId good) const;
    [[nodiscard]] EconomyAmount supply(MarketId market, GoodId good) const;
    [[nodiscard]] EconomyAmount demand(MarketId market, GoodId good) const;
    // Unsold stock carried between ticks. Stock is the memory that lets prices
    // clear: gluts accumulate inventory and push prices down across ticks,
    // shortages drain it and push prices up.
    [[nodiscard]] EconomyAmount inventory(MarketId market, GoodId good) const;
    [[nodiscard]] std::span<const EconomyAmount> inventory_row(MarketId market) const;
    [[nodiscard]] std::span<EconomyAmount> inventory_row(MarketId market);
    // Unmet demand left after the last clearing. Drives inter-market trade.
    [[nodiscard]] EconomyAmount shortage(MarketId market, GoodId good) const;
    [[nodiscard]] std::span<const EconomyAmount> shortage_row(MarketId market) const;
    [[nodiscard]] std::span<EconomyAmount> shortage_row(MarketId market);
    [[nodiscard]] std::span<EconomyAmount> supply_row(MarketId market);
    [[nodiscard]] std::span<const EconomyAmount> supply_row(MarketId market) const;
    [[nodiscard]] std::span<EconomyAmount> demand_row(MarketId market);
    [[nodiscard]] std::span<const EconomyAmount> demand_row(MarketId market) const;
    [[nodiscard]] std::span<EconomyPrice> price_row(MarketId market);
    [[nodiscard]] std::span<const EconomyPrice> price_row(MarketId market) const;

    void clear_flows() noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] std::size_t market_index(MarketId market) const;
    [[nodiscard]] std::size_t flat_index(MarketId market, GoodId good) const;

    std::size_t good_count_ = 0;
    std::vector<CountryId> owners_;
    std::vector<SettlementAccountId> settlement_accounts_;
    std::vector<CurrencyKey> currency_keys_;
    std::vector<EconomyAmount> clearing_cash_;
    std::vector<EconomyPrice> prices_;
    std::vector<EconomyAmount> supply_;
    std::vector<EconomyAmount> demand_;
    std::vector<EconomyAmount> inventory_;
    std::vector<EconomyAmount> shortage_;
};

} // namespace thunder
