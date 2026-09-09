#pragma once
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <span>
#include <vector>

namespace thunder {

/// Packed handle: 32-bit index + 32-bit generation.
/// A stale handle whose generation no longer matches the slot is considered dead.
struct SlotHandle {
    std::uint32_t index = 0xFFFFFFFFu;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != 0xFFFFFFFFu; }
    explicit constexpr operator bool() const noexcept { return valid(); }
    friend constexpr bool operator==(SlotHandle, SlotHandle) = default;

    static constexpr SlotHandle invalid() noexcept { return {}; }
};

/// High-Performance Generational Slot Pool with Word-Level Bitmap & LIFO Free-List Recycling.
///
/// Features:
/// - 64-bit word bitmap for cache-line efficient bitwise scanning and zero-proxy overhead.
/// - Generational handle validation to completely eliminate dangling references.
/// - LIFO free-list recycling for optimal L1/L2 cache locality.
/// - Robust boundary safety across all allocate/release/query operations.
class SlotPool {
public:
    SlotPool() = default;
    explicit SlotPool(std::size_t initial_capacity) {
        reserve(initial_capacity);
    }

    /// Allocate a slot — reuses from free-list head, or appends new.
    [[nodiscard]] SlotHandle allocate() {
        std::uint32_t idx;
        if (!free_list_.empty()) {
            idx = free_list_.back();
            free_list_.pop_back();
            set_alive_bit(idx, true);
        } else {
            idx = static_cast<std::uint32_t>(generations_.size());
            // Start generations at 1 to reserve 0 as never-valid and avoid wrap collision with initial 0 handle
            generations_.push_back(1);
            ensure_bitmap_capacity(idx + 1);
            set_alive_bit(idx, true);
        }
        ++alive_count_;
        return {idx, generations_[idx]};
    }

    /// Release a slot — validates generation, pushes to free-list, bumps generation.
    void release(SlotHandle handle) noexcept {
        if (!handle.valid() || handle.index >= generations_.size()) return;
        if (!is_index_alive(handle.index)) return;
        if (generations_[handle.index] != handle.generation) return;

        set_alive_bit(handle.index, false);
        // Bump generation, skip 0 and 0xFFFFFFFF (invalid sentinel) to avoid ABA after wrap
        auto& gen = generations_[handle.index];
        std::uint32_t next = gen + 1u;
        if (next == 0u || next == 0xFFFFFFFFu) next = 1u;
        if (next == 0xFFFFFFFEu) next = 1u; // keep below sentinel range, wrap to 1
        gen = next;
        free_list_.push_back(handle.index);
        if (alive_count_ > 0) --alive_count_;
    }

    /// Bulk release.
    void release_batch(std::span<const SlotHandle> handles) noexcept {
        for (auto h : handles) release(h);
    }

    /// Check if a handle refers to a living slot.
    [[nodiscard]] bool is_alive(SlotHandle handle) const noexcept {
        if (!handle.valid() || handle.index >= generations_.size()) return false;
        return is_index_alive(handle.index) && generations_[handle.index] == handle.generation;
    }

    /// Extract raw index for SoA column lookup (asserts alive).
    [[nodiscard]] std::uint32_t index(SlotHandle handle) const noexcept {
        assert(is_alive(handle));
        return handle.index;
    }

    /// Check if a raw index slot is currently alive via bitwise test.
    [[nodiscard]] bool is_index_alive(std::uint32_t idx) const noexcept {
        const std::size_t word_idx = idx / 64u;
        if (word_idx >= bitmap_.size()) return false;
        const std::uint64_t mask = 1ULL << (idx % 64u);
        return (bitmap_[word_idx] & mask) != 0ULL;
    }

    // ── Queries ──
    [[nodiscard]] std::size_t alive_count() const noexcept { return alive_count_; }
    [[nodiscard]] std::size_t capacity()    const noexcept { return generations_.size(); }
    [[nodiscard]] std::size_t free_count()  const noexcept { return free_list_.size(); }

    [[nodiscard]] std::span<const std::uint32_t> generations() const noexcept { return generations_; }
    [[nodiscard]] std::span<const std::uint64_t> bitmap() const noexcept { return bitmap_; }
    [[nodiscard]] std::span<const std::uint32_t> free_list() const noexcept { return free_list_; }

    /// Restore the complete allocator state from an authoritative snapshot.
    /// Entity IDs in Thunder are dense indices, so preserving generations and the
    /// free-list order is required for deterministic recycling after a save.
    void restore_state(std::span<const std::uint32_t> generations,
                       std::span<const std::uint64_t> bitmap,
                       std::span<const std::uint32_t> free_list) {
        const auto expected_words = (generations.size() + 63u) / 64u;
        if (bitmap.size() != expected_words)
            throw std::invalid_argument("slot pool bitmap size mismatch");
        if (!bitmap.empty() && (generations.size() % 64u) != 0u) {
            const auto tail_bits = static_cast<unsigned>(generations.size() % 64u);
            const auto valid_mask = (std::uint64_t{1} << tail_bits) - 1u;
            if ((bitmap.back() & ~valid_mask) != 0u)
                throw std::invalid_argument("slot pool bitmap has invalid tail bits");
        }
        for (const auto generation : generations) {
            // Zero and the two sentinel values are deliberately never emitted
            // by allocate/release and would invalidate stale-handle checks.
            if (generation == 0u || generation == 0xFFFFFFFEu || generation == 0xFFFFFFFFu)
                throw std::invalid_argument("slot pool generation is invalid");
        }

        std::size_t alive = 0u;
        for (const auto word : bitmap) alive += static_cast<std::size_t>(std::popcount(word));
        if (free_list.size() != generations.size() - alive)
            throw std::invalid_argument("slot pool free-list count mismatch");
        std::vector<bool> listed(generations.size(), false);
        for (const auto index : free_list) {
            if (index >= generations.size() || is_index_alive_from(bitmap, index) || listed[index])
                throw std::invalid_argument("slot pool free-list is invalid");
            listed[index] = true;
        }
        for (std::size_t index = 0; index < generations.size(); ++index) {
            if (!is_index_alive_from(bitmap, static_cast<std::uint32_t>(index)) && !listed[index])
                throw std::invalid_argument("slot pool free-list is incomplete");
        }
        generations_.assign(generations.begin(), generations.end());
        bitmap_.assign(bitmap.begin(), bitmap.end());
        free_list_.assign(free_list.begin(), free_list.end());
        alive_count_ = alive;
    }

    /// Should we compact? Heuristic: free > 25 % of alive AND free > 1024.
    [[nodiscard]] bool should_compact() const noexcept {
        if (free_list_.size() <= 1024) return false;
        return free_list_.size() > (alive_count_ / 4);
    }

    /// Permute generation counters so each survivor keeps its identity after
    /// external compaction. Must be called BEFORE apply_compaction(), while
    /// generations_ still holds the pre-compaction layout.
    ///
    /// Without this, `generations_` is merely truncated: slot N keeps the
    /// generation of whatever used to live at index N. A handle minted after
    /// compaction then carries a foreign generation, so stale handles issued
    /// before compaction are revived (ABA) while live handles are rejected.
    void remap_generations(std::span<const std::uint32_t> old_to_new, std::size_t new_size) {
        if (new_size > generations_.size()) return;
        std::vector<std::uint32_t> remapped(new_size, 1u);
        const std::size_t old_size = std::min(old_to_new.size(), generations_.size());
        for (std::size_t old_index = 0; old_index < old_size; ++old_index) {
            const auto dst = old_to_new[old_index];
            if (dst == 0xFFFFFFFFu || dst >= new_size) continue;
            remapped[dst] = generations_[old_index];
        }
        generations_.swap(remapped);
    }

    /// Reset pool state after external compaction shrinks the arrays.
    void apply_compaction(std::size_t new_size) {
        generations_.resize(new_size);
        const std::size_t needed_words = (new_size + 63u) / 64u;
        bitmap_.assign(needed_words, ~0ULL);
        // Clear tail bits in the last word if not full multiple of 64
        if (new_size > 0 && (new_size % 64u) != 0u) {
            const unsigned tail_bits = static_cast<unsigned>(new_size % 64u);
            bitmap_.back() = (1ULL << tail_bits) - 1ULL;
        }
        free_list_.clear();
        alive_count_ = new_size;
    }

    void reserve(std::size_t count) {
        generations_.reserve(count);
        free_list_.reserve(count / 4);
        const std::size_t needed_words = (count + 63u) / 64u;
        bitmap_.reserve(needed_words);
    }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return generations_.capacity() * sizeof(std::uint32_t)
             + bitmap_.capacity() * sizeof(std::uint64_t)
             + free_list_.capacity() * sizeof(std::uint32_t);
    }

private:
    [[nodiscard]] static bool is_index_alive_from(
        std::span<const std::uint64_t> bitmap, std::uint32_t idx) noexcept {
        const auto word_idx = static_cast<std::size_t>(idx / 64u);
        return word_idx < bitmap.size() &&
            (bitmap[word_idx] & (std::uint64_t{1} << (idx % 64u))) != 0u;
    }

    void ensure_bitmap_capacity(std::size_t slot_count) {
        const std::size_t needed_words = (slot_count + 63u) / 64u;
        if (needed_words > bitmap_.size()) {
            bitmap_.resize(needed_words, 0ULL);
        }
    }

    void set_alive_bit(std::uint32_t idx, bool alive) noexcept {
        const std::size_t word_idx = idx / 64u;
        if (word_idx >= bitmap_.size()) return;
        const std::uint64_t mask = 1ULL << (idx % 64u);
        if (alive) {
            bitmap_[word_idx] |= mask;
        } else {
            bitmap_[word_idx] &= ~mask;
        }
    }

    std::vector<std::uint32_t> generations_;
    std::vector<std::uint64_t> bitmap_;      // 64-bit word bitmap for dense bit tests
    std::vector<std::uint32_t> free_list_;   // LIFO stack of free indices
    std::size_t                alive_count_ = 0;
};

} // namespace thunder
