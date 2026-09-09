#include "thunder/simulation/world/StateRegionIndex.hpp"

#include "thunder/foundation/base/Hash.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace thunder {

void StateRegionIndex::clear() noexcept {
    keys_.clear();
    state_regions_.clear();
    state_offsets_.clear();
    states_.clear();
    province_offsets_.clear();
    provinces_.clear();
}

void StateRegionIndex::rebuild(const GeographyStore& geography) {
    clear();
    std::unordered_map<std::string, StateRegionId> lookup;
    lookup.reserve(geography.state_count() * 2u);
    state_regions_.reserve(geography.state_count());

    for (std::size_t i = 0; i < geography.state_count(); ++i) {
        const auto state = StateId{static_cast<StateId::rep_type>(i)};
        const std::string key{geography.state_key(state)};
        const auto found = lookup.find(key);
        if (found != lookup.end()) {
            state_regions_.push_back(found->second);
            continue;
        }
        const auto region = StateRegionId{static_cast<StateRegionId::rep_type>(keys_.size())};
        keys_.push_back(key);
        lookup.emplace(key, region);
        state_regions_.push_back(region);
    }

    state_offsets_.assign(keys_.size() + 1u, 0u);
    for (const auto region : state_regions_) {
        if (region.valid() && region.value() < keys_.size()) ++state_offsets_[region.value() + 1u];
    }
    for (std::size_t i = 1; i < state_offsets_.size(); ++i) state_offsets_[i] += state_offsets_[i - 1u];
    states_.assign(state_regions_.size(), StateId{});
    auto state_write = state_offsets_;
    for (std::size_t i = 0; i < state_regions_.size(); ++i) {
        const auto region = state_regions_[i];
        if (region.valid() && region.value() < keys_.size())
            states_[state_write[region.value()]++] = StateId{static_cast<StateId::rep_type>(i)};
    }

    province_offsets_.assign(keys_.size() + 1u, 0u);
    for (std::size_t i = 0; i < geography.province_count(); ++i) {
        const auto state = geography.province_state(ProvinceId{static_cast<ProvinceId::rep_type>(i)});
        if (state.valid() && state.value() < state_regions_.size()) {
            const auto region = state_regions_[state.value()];
            if (region.valid() && region.value() < keys_.size()) ++province_offsets_[region.value() + 1u];
        }
    }
    for (std::size_t i = 1; i < province_offsets_.size(); ++i) province_offsets_[i] += province_offsets_[i - 1u];
    provinces_.assign(province_offsets_.back(), ProvinceId{});
    auto province_write = province_offsets_;
    for (std::size_t i = 0; i < geography.province_count(); ++i) {
        const auto province = ProvinceId{static_cast<ProvinceId::rep_type>(i)};
        const auto state = geography.province_state(province);
        if (state.valid() && state.value() < state_regions_.size()) {
            const auto region = state_regions_[state.value()];
            if (region.valid() && region.value() < keys_.size())
                provinces_[province_write[region.value()]++] = province;
        }
    }
}

std::size_t StateRegionIndex::region_index(StateRegionId region) const {
    if (!region.valid() || region.value() >= keys_.size()) throw std::out_of_range("invalid StateRegionId");
    return static_cast<std::size_t>(region.value());
}

StateRegionId StateRegionIndex::region_for_state(StateId state) const {
    if (!state.valid() || state.value() >= state_regions_.size()) throw std::out_of_range("invalid StateId");
    return state_regions_[state.value()];
}

std::string_view StateRegionIndex::key(StateRegionId region) const {
    return keys_[region_index(region)];
}

std::span<const StateId> StateRegionIndex::states(StateRegionId region) const {
    const auto index = region_index(region);
    return {states_.data() + state_offsets_[index], state_offsets_[index + 1u] - state_offsets_[index]};
}

std::span<const ProvinceId> StateRegionIndex::provinces(StateRegionId region) const {
    const auto index = region_index(region);
    return {provinces_.data() + province_offsets_[index], province_offsets_[index + 1u] - province_offsets_[index]};
}

std::uint64_t StateRegionIndex::checksum() const noexcept {
    Fnv1a64 hash;
    for (const auto& key : keys_) hash.add(std::string_view{key});
    for (const auto region : state_regions_) hash.add(region.value());
    for (const auto state : states_) hash.add(state.value());
    for (const auto province : provinces_) hash.add(province.value());
    return hash.value();
}

std::size_t StateRegionIndex::memory_bytes() const noexcept {
    return keys_.capacity() * sizeof(std::string) + state_regions_.capacity() * sizeof(StateRegionId) +
           state_offsets_.capacity() * sizeof(std::uint32_t) + states_.capacity() * sizeof(StateId) +
           province_offsets_.capacity() * sizeof(std::uint32_t) + provinces_.capacity() * sizeof(ProvinceId);
}

} // namespace thunder
