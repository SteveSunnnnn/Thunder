#pragma once
#include <cstddef>
#include <stdexcept>

namespace thunder {

struct StableChunkRange {
    std::size_t index = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
};

// Simulation partition boundaries are content/runtime constants, not hardware
// dependent. Keeping the same grain on 4-core and 32-core machines preserves
// chunk-local floating-point order, RNG stream assignment and command merge order.
class StablePartition {
public:
    StablePartition(std::size_t item_count, std::size_t grain_size)
        : item_count_(item_count), grain_size_(grain_size == 0u ? 1u : grain_size) {}

    [[nodiscard]] std::size_t item_count() const noexcept { return item_count_; }
    [[nodiscard]] std::size_t grain_size() const noexcept { return grain_size_; }
    [[nodiscard]] std::size_t chunk_count() const noexcept {
        return item_count_ == 0u ? 0u : 1u + (item_count_ - 1u) / grain_size_;
    }

    [[nodiscard]] StableChunkRange chunk(std::size_t index) const {
        if (index >= chunk_count()) throw std::out_of_range("StablePartition chunk index out of range");
        const std::size_t begin = index * grain_size_;
        const std::size_t remaining = item_count_ - begin;
        const std::size_t end = grain_size_ < remaining ? begin + grain_size_ : item_count_;
        return {index, begin, end};
    }

private:
    std::size_t item_count_ = 0;
    std::size_t grain_size_ = 1;
};

} // namespace thunder
