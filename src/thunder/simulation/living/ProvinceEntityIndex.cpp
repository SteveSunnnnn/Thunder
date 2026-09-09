#include "thunder/simulation/living/ProvinceEntityIndex.hpp"
#include "thunder/simulation/economy/BuildingStore.hpp"
#include "thunder/simulation/economy/PopStore.hpp"
#include <stdexcept>

namespace thunder {
std::size_t ProvinceEntityIndex::checked(ProvinceId province) const {
    const auto p = static_cast<std::size_t>(province.value());
    if (!province.valid() || p >= province_count_) throw std::out_of_range("invalid ProvinceId in ProvinceEntityIndex");
    return p;
}

void ProvinceEntityIndex::rebuild(std::size_t province_count, const PopStore& pops, const BuildingStore& buildings) {
    province_count_ = province_count;
    pop_offsets_.assign(province_count + 1u, 0u);
    building_offsets_.assign(province_count + 1u, 0u);
    const auto pop_provinces = pops.provinces();
    const auto building_provinces = buildings.provinces();
    for (std::size_t i = 0; i < pop_provinces.size(); ++i) {
        if (!pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const auto province = pop_provinces[i];
        if (!province.valid()) continue;
        const auto p = static_cast<std::size_t>(province.value());
        if (p >= province_count) throw std::out_of_range("PopStore contains province outside Living Map definition range");
        ++pop_offsets_[p + 1u];
    }
    for (std::size_t i = 0; i < building_provinces.size(); ++i) {
        if (!buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const auto province = building_provinces[i];
        if (!province.valid()) continue;
        const auto p = static_cast<std::size_t>(province.value());
        if (p >= province_count) throw std::out_of_range("BuildingStore contains province outside Living Map definition range");
        ++building_offsets_[p + 1u];
    }
    for (std::size_t i = 1; i < pop_offsets_.size(); ++i) pop_offsets_[i] += pop_offsets_[i - 1u];
    for (std::size_t i = 1; i < building_offsets_.size(); ++i) building_offsets_[i] += building_offsets_[i - 1u];
    pop_ids_.resize(pop_offsets_.back());
    building_ids_.resize(building_offsets_.back());
    auto pop_cursor = pop_offsets_;
    auto building_cursor = building_offsets_;
    for (std::size_t i = 0; i < pop_provinces.size(); ++i) {
        if (!pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const auto province = pop_provinces[i];
        if (!province.valid()) continue;
        const auto p = static_cast<std::size_t>(province.value());
        pop_ids_[pop_cursor[p]++] = PopId{static_cast<PopId::rep_type>(i)};
    }
    for (std::size_t i = 0; i < building_provinces.size(); ++i) {
        if (!buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const auto province = building_provinces[i];
        if (!province.valid()) continue;
        const auto p = static_cast<std::size_t>(province.value());
        building_ids_[building_cursor[p]++] = BuildingId{static_cast<BuildingId::rep_type>(i)};
    }
    pop_revision_ = pops.province_membership_revision();
    building_revision_ = buildings.province_membership_revision();
}
bool ProvinceEntityIndex::current_for(const PopStore& pops, const BuildingStore& buildings) const noexcept {
    return pop_revision_ == pops.province_membership_revision()
        && building_revision_ == buildings.province_membership_revision();
}

std::span<const PopId> ProvinceEntityIndex::pops(ProvinceId province) const {
    const auto p = checked(province);
    return std::span<const PopId>{pop_ids_}.subspan(pop_offsets_[p], pop_offsets_[p + 1u] - pop_offsets_[p]);
}
std::span<const BuildingId> ProvinceEntityIndex::buildings(ProvinceId province) const {
    const auto p = checked(province);
    return std::span<const BuildingId>{building_ids_}.subspan(building_offsets_[p], building_offsets_[p + 1u] - building_offsets_[p]);
}
std::size_t ProvinceEntityIndex::memory_bytes() const noexcept {
    return pop_offsets_.capacity() * sizeof(std::uint32_t) + pop_ids_.capacity() * sizeof(PopId)
        + building_offsets_.capacity() * sizeof(std::uint32_t) + building_ids_.capacity() * sizeof(BuildingId);
}
} // namespace thunder
