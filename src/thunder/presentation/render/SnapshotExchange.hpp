#pragma once
#include "thunder/presentation/render/RenderSnapshotData.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace thunder {

// Single-producer / single-consumer triple buffer between simulation and render.
// The producer never waits for the consumer: if render falls behind, stale READY
// snapshots are replaced. A snapshot being actively READ is never touched.
class SnapshotExchange {
public:
    static constexpr std::uint32_t invalid = std::numeric_limits<std::uint32_t>::max();

    explicit SnapshotExchange(std::size_t country_capacity = 256) {
        for (auto& slot : slots_) slot.snapshot.reserve(country_capacity);
    }

    struct WriteHandle {
        RenderSnapshot* snapshot = nullptr;
        std::uint32_t slot = invalid;
        explicit operator bool() const noexcept { return snapshot != nullptr; }
    };

    struct ReadHandle {
        const RenderSnapshot* snapshot = nullptr;
        std::uint32_t slot = invalid;
        explicit operator bool() const noexcept { return snapshot != nullptr; }
    };

    [[nodiscard]] WriteHandle try_begin_write() noexcept {
        for (std::uint32_t i = 0; i < slots_.size(); ++i) {
            std::uint8_t expected = free;
            if (slots_[i].state.compare_exchange_strong(expected, writing,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return {&slots_[i].snapshot, i};
            }
        }
        return {};
    }

    void publish(WriteHandle handle) noexcept {
        if (!handle || handle.slot >= slots_.size()) return;
        auto& current = slots_[handle.slot];
        current.state.store(ready, std::memory_order_release);
        const auto previous = latest_.exchange(handle.slot, std::memory_order_acq_rel);
        if (previous != invalid && previous != handle.slot) {
            std::uint8_t expected = ready;
            (void)slots_[previous].state.compare_exchange_strong(expected, free,
                std::memory_order_release, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] ReadHandle try_acquire_latest() noexcept {
        const auto index = latest_.exchange(invalid, std::memory_order_acq_rel);
        if (index == invalid) return {};
        std::uint8_t expected = ready;
        if (!slots_[index].state.compare_exchange_strong(expected, reading,
                std::memory_order_acquire, std::memory_order_relaxed)) {
            return {};
        }
        return {&slots_[index].snapshot, index};
    }

    void release(ReadHandle handle) noexcept {
        if (!handle || handle.slot >= slots_.size()) return;
        slots_[handle.slot].state.store(free, std::memory_order_release);
    }

private:
    static constexpr std::uint8_t free = 0;
    static constexpr std::uint8_t writing = 1;
    static constexpr std::uint8_t ready = 2;
    static constexpr std::uint8_t reading = 3;

    struct Slot {
        RenderSnapshot snapshot;
        std::atomic<std::uint8_t> state{free};
    };

    std::array<Slot, 3> slots_{};
    std::atomic<std::uint32_t> latest_{invalid};
};

} // namespace thunder
