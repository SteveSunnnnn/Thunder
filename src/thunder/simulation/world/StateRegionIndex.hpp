#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/world/GeographyStore.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

// Derived read-only grouping above political StateId. The current compiler
// emits one state per region, but keeping this index separate allows future
// packs to place several political states inside one geographic region
// without changing province ownership or simulation IDs.
class StateRegionIndex {
public:
    void clear() noexcept;
    void rebuild(const GeographyStore& geography);

    [[nodiscard]] std::size_t region_count() const noexcept { return keys_.size(); }
    [[nodiscard]] std::size_t state_count() const noexcept { return state_regions_.size(); }
    [[nodiscard]] StateRegionId region_for_state(StateId state) const;
    [[nodiscard]] std::string_view key(StateRegionId region) const;
    [[nodiscard]] std::span<const StateId> states(StateRegionId region) const;
    [[nodiscard]] std::span<const ProvinceId> provinces(StateRegionId region) const;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] std::size_t region_index(StateRegionId region) const;

    std::vector<std::string> keys_;
    std::vector<StateRegionId> state_regions_;
    std::vector<std::uint32_t> state_offsets_;
    std::vector<StateId> states_;
    std::vector<std::uint32_t> province_offsets_;
    std::vector<ProvinceId> provinces_;
};

} // namespace thunder
