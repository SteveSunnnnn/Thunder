#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include "thunder/foundation/memory/SlotPool.hpp"
#include "thunder/foundation/memory/SoaCompactor.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct BuildingInit {
    MarketId market;
    BuildingTypeId type;
    std::uint16_t level = 1;
    EconomyPrice wage_offer_milli = 1000;
    EconomyAmount cash_milli = 0;
    ProvinceId province{};
    ProductionMethodId production_method{};
};

// Physically isolated SoA hot data scanned weekly during economic cycles
struct BuildingHotData {
    std::vector<MarketId> markets;
    std::vector<BuildingTypeId> types;
    std::vector<std::uint16_t> levels;
    std::vector<ProductionMethodId> production_methods;
    std::vector<PopulationCount> employees;
    std::vector<EconomyPrice> wage_offer_milli;
    std::vector<EconomyAmount> cash_milli;
    std::vector<EconomyAmount> last_profit_milli;

    void reserve(std::size_t count);
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
};

// Out-of-line spatial and ownership cold data
struct BuildingColdData {
    std::vector<ProvinceId> provinces;

    void reserve(std::size_t count);
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
};

class BuildingStore {
public:
    void reserve(std::size_t count);
    BuildingId create(BuildingInit init);
    void destroy(BuildingId id);
    CompactionMap compact();

    [[nodiscard]] std::size_t size() const noexcept { return hot_.markets.size(); }
    [[nodiscard]] MarketId market(BuildingId id) const;
    [[nodiscard]] ProvinceId province(BuildingId id) const;
    [[nodiscard]] BuildingTypeId type(BuildingId id) const;
    [[nodiscard]] std::uint16_t level(BuildingId id) const;
    [[nodiscard]] ProductionMethodId production_method(BuildingId id) const;
    [[nodiscard]] PopulationCount employees(BuildingId id) const;
    [[nodiscard]] EconomyPrice wage_offer(BuildingId id) const;
    [[nodiscard]] EconomyAmount cash(BuildingId id) const;
    [[nodiscard]] EconomyAmount last_profit(BuildingId id) const;

    void set_market(BuildingId id, MarketId value);
    void set_province(BuildingId id, ProvinceId value);
    void set_level(BuildingId id, std::uint16_t value);
    void set_production_method(BuildingId id, ProductionMethodId value);
    void set_employees(BuildingId id, PopulationCount value);
    void set_wage_offer(BuildingId id, EconomyPrice value);
    void add_cash(BuildingId id, EconomyAmount delta);
    void set_last_profit(BuildingId id, EconomyAmount value);

    [[nodiscard]] std::uint64_t market_membership_revision() const noexcept { return market_membership_revision_; }
    [[nodiscard]] std::uint64_t province_membership_revision() const noexcept { return province_membership_revision_; }
    [[nodiscard]] std::span<const MarketId> markets() const noexcept { return hot_.markets; }
    [[nodiscard]] std::span<const ProvinceId> provinces() const noexcept { return cold_.provinces; }
    [[nodiscard]] std::span<const BuildingTypeId> types() const noexcept { return hot_.types; }
    [[nodiscard]] std::span<const std::uint16_t> levels() const noexcept { return hot_.levels; }
    [[nodiscard]] std::span<const ProductionMethodId> production_methods() const noexcept { return hot_.production_methods; }
    [[nodiscard]] std::span<PopulationCount> employees_mut() noexcept { return hot_.employees; }
    [[nodiscard]] std::span<EconomyPrice> wage_offers_mut() noexcept { return hot_.wage_offer_milli; }
    [[nodiscard]] std::span<EconomyAmount> cash_mut() noexcept { return hot_.cash_milli; }
    [[nodiscard]] std::span<EconomyAmount> last_profits_mut() noexcept { return hot_.last_profit_milli; }
    [[nodiscard]] std::span<const PopulationCount> employees_all() const noexcept { return hot_.employees; }
    [[nodiscard]] std::span<const EconomyPrice> wage_offers() const noexcept { return hot_.wage_offer_milli; }
    [[nodiscard]] std::span<const EconomyAmount> cash_all() const noexcept { return hot_.cash_milli; }
    [[nodiscard]] std::span<const EconomyAmount> last_profits() const noexcept { return hot_.last_profit_milli; }

    [[nodiscard]] const BuildingHotData& hot_data() const noexcept { return hot_; }
    [[nodiscard]] const BuildingColdData& cold_data() const noexcept { return cold_; }
    [[nodiscard]] std::size_t hot_memory_bytes() const noexcept { return hot_.memory_bytes(); }
    [[nodiscard]] std::size_t cold_memory_bytes() const noexcept { return cold_.memory_bytes(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return hot_memory_bytes() + cold_memory_bytes(); }
    [[nodiscard]] std::uint64_t checksum() const noexcept;

    [[nodiscard]] SlotPool& slot_pool() noexcept { return slot_pool_; }
    [[nodiscard]] const SlotPool& slot_pool() const noexcept { return slot_pool_; }

private:
    [[nodiscard]] std::size_t index(BuildingId id) const;
    BuildingHotData hot_;
    BuildingColdData cold_;
    SlotPool slot_pool_;
    std::uint64_t market_membership_revision_ = 0u;
    std::uint64_t province_membership_revision_ = 0u;
};

} // namespace thunder
