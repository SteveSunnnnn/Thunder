#pragma once
#include "thunder/simulation/kernel/CommandQueue.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {

struct StagedCommand {
    CommandType type{};
    CountryId country{};
    double value = 0.0;
};

// Parallel simulation kernels emit into deterministic chunk-owned lanes. Each
// chunk is written by exactly one job; flush then walks chunk IDs in ascending
// order and assigns global command sequence numbers. Scheduling/stealing order
// therefore cannot change command order or the resulting world checksum.
class DeterministicCommandStage {
public:
    void resize(std::size_t chunks, std::size_t reserve_per_chunk = 0u) {
        lanes_.resize(chunks);
        for (auto& lane : lanes_) {
            lane.commands.clear();
            if (reserve_per_chunk > lane.commands.capacity()) lane.commands.reserve(reserve_per_chunk);
        }
    }

    void clear() noexcept {
        for (auto& lane : lanes_) lane.commands.clear();
    }

    void emit(std::size_t chunk, CommandType type, CountryId country, double value) {
        lanes_[chunk].commands.push_back({type, country, value});
    }

    [[nodiscard]] std::size_t chunk_count() const noexcept { return lanes_.size(); }
    [[nodiscard]] std::size_t pending() const noexcept {
        std::size_t count = 0;
        for (const auto& lane : lanes_) count += lane.commands.size();
        return count;
    }

    std::size_t flush(CommandQueue& queue) {
        std::size_t count = 0;
        for (auto& lane : lanes_) {
            for (const auto& command : lane.commands) {
                (void)queue.enqueue(command.type, command.country, command.value);
                ++count;
            }
            lane.commands.clear();
        }
        return count;
    }

private:
    struct alignas(64) Lane {
        std::vector<StagedCommand> commands;
    };
    std::vector<Lane> lanes_;
};

} // namespace thunder
