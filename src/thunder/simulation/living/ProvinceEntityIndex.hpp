#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {
class PopStore;
class BuildingStore;

class ProvinceEntityIndex {
public:
    void rebuild(std::size_t province_count, const PopStore& pops, const BuildingStore& buildings);
    [[nodiscard]] std::span<const PopId> pops(ProvinceId province) const;
    [[nodiscard]] std::span<const BuildingId> buildings(ProvinceId province) const;
    [[nodiscard]] std::size_t province_count() const noexcept { return province_count_; }
    [[nodiscard]] bool current_for(const PopStore& pops, const BuildingStore& buildings) const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] std::size_t checked(ProvinceId province) const;
    std::size_t province_count_ = 0;
    std::vector<std::uint32_t> pop_offsets_;
    std::vector<PopId> pop_ids_;
    std::vector<std::uint32_t> building_offsets_;
    std::vector<BuildingId> building_ids_;
    std::uint64_t pop_revision_ = 0u;
    std::uint64_t building_revision_ = 0u;
};
} // namespace thunder
