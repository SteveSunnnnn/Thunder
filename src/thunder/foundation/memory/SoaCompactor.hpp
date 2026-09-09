#pragma once
#include "thunder/foundation/memory/SlotPool.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {

/// Mapping produced by build_compaction_map().
struct CompactionMap {
    std::vector<std::uint32_t> old_to_new;  // old index -> new dense index (0xFFFFFFFF if dead)
    std::size_t compacted_size = 0;          // alive entity count after compaction

    [[nodiscard]] bool is_identity() const noexcept {
        if (compacted_size != old_to_new.size()) return false;
        for (std::size_t i = 0; i < old_to_new.size(); ++i) {
            if (old_to_new[i] != static_cast<std::uint32_t>(i)) return false;
        }
        return true;
    }
};

/// High-performance bitwise compaction map builder.
/// Scans 64 bits per iteration using hardware popcount and bit extraction.
[[nodiscard]] inline CompactionMap build_compaction_map(const SlotPool& pool) {
    CompactionMap map;
    const std::size_t cap = pool.capacity();
    map.old_to_new.resize(cap, 0xFFFFFFFFu);

    std::uint32_t write = 0;
    const auto bm = pool.bitmap();

    for (std::size_t word_idx = 0; word_idx < bm.size(); ++word_idx) {
        std::uint64_t w = bm[word_idx];
        const std::size_t base_idx = word_idx * 64u;
        if (base_idx >= cap) break;

        // Fast path: all 64 slots alive
        if (w == ~0ULL && base_idx + 64u <= cap) {
            for (std::uint32_t k = 0; k < 64u; ++k) {
                map.old_to_new[base_idx + k] = write++;
            }
            continue;
        }

        // Fast path: all 64 slots dead
        if (w == 0ULL) continue;

        // Bitwise extraction of set bits
        while (w != 0ULL) {
            const auto bit = static_cast<unsigned>(std::countr_zero(w));
            const auto idx = base_idx + bit;
            if (idx < cap) {
                map.old_to_new[idx] = write++;
            }
            w &= w - 1ULL; // clear lowest set bit
        }
    }
    map.compacted_size = write;
    return map;
}

/// Compact a single SoA column vector in-place.
template <typename T>
void compact_column(std::vector<T>& column, const CompactionMap& map) {
    assert(column.size() >= map.old_to_new.size());
    const std::size_t old_size = map.old_to_new.size();
    for (std::size_t i = 0; i < old_size; ++i) {
        const auto dst = map.old_to_new[i];
        if (dst != 0xFFFFFFFFu && dst != static_cast<std::uint32_t>(i)) {
            column[dst] = std::move(column[i]);
        }
    }
    column.resize(map.compacted_size);
}

/// Apply compaction to the pool itself — permutes generations, resets
/// free-list, resizes arrays.
inline void apply_compaction(SlotPool& pool, const CompactionMap& map) {
    pool.remap_generations(map.old_to_new, map.compacted_size);
    pool.apply_compaction(map.compacted_size);
}

} // namespace thunder
