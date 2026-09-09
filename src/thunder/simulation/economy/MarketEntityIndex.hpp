#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include <cstddef>
#include <span>
#include <vector>

namespace thunder {

class PopStore;
class BuildingStore;

class MarketEntityIndex {
public:
    void rebuild(std::size_t market_count, const PopStore& pops, const BuildingStore& buildings);
    [[nodiscard]] std::span<const PopId> pops(MarketId market) const;
    [[nodiscard]] std::span<const BuildingId> buildings(MarketId market) const;
    [[nodiscard]] std::size_t market_count() const noexcept { return market_count_; }
    [[nodiscard]] bool current_for(const PopStore& pops, const BuildingStore& buildings) const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] static std::size_t checked_market(MarketId market, std::size_t market_count);
    std::size_t market_count_ = 0;
    std::vector<std::uint32_t> pop_offsets_;
    std::vector<PopId> pop_ids_;
    std::vector<std::uint32_t> building_offsets_;
    std::vector<BuildingId> building_ids_;
    std::uint64_t pop_revision_ = 0u;
    std::uint64_t building_revision_ = 0u;
};

} // namespace thunder
