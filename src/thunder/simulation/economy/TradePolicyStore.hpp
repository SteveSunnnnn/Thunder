#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {

struct TradePolicy {
    std::int32_t import_tariff_ppm = 0;
    std::int32_t export_tariff_ppm = 0;
    // Weekly international logistics capacity. A negative value means
    // unlimited capacity, which is the compatibility default.
    EconomyAmount logistics_capacity_milli = -1;
};

class TradePolicyStore {
public:
    void resize(std::size_t country_count);
    [[nodiscard]] std::size_t size() const noexcept { return policies_.size(); }
    void set(CountryId country, TradePolicy policy);
    [[nodiscard]] const TradePolicy& get(CountryId country) const noexcept;
    void begin_week() noexcept;
    [[nodiscard]] EconomyAmount available_capacity(CountryId country) const noexcept;
    [[nodiscard]] EconomyAmount reserve_capacity(CountryId country, EconomyAmount requested) noexcept;
    [[nodiscard]] EconomyAmount used_capacity(CountryId country) const noexcept;
    [[nodiscard]] bool validate(std::size_t country_count) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    std::vector<TradePolicy> policies_;
    std::vector<EconomyAmount> used_capacity_milli_;
};

} // namespace thunder
