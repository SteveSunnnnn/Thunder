#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <cstdint>
#include <vector>

namespace thunder {

enum class CommandType : std::uint8_t { SetTaxRate, AddTreasury };

struct Command {
    std::uint64_t sequence = 0;
    CommandType type{};
    CountryId country{};
    double value = 0.0;
};

class CommandQueue {
public:
    explicit CommandQueue(std::size_t initial_capacity = 256) { queue_.reserve(initial_capacity); }

    std::uint64_t enqueue(CommandType type, CountryId country, double value);
    void apply_all(World& world);
    // Drops uncommitted input after a successful authoritative-state restore.
    // Sequence ids remain monotonic for the lifetime of the engine instance.
    void clear() noexcept { queue_.clear(); read_ = 0; }
    [[nodiscard]] bool empty() const noexcept { return read_ == queue_.size(); }
    [[nodiscard]] std::size_t pending() const noexcept { return queue_.size() - read_; }

private:
    std::uint64_t next_sequence_ = 1;
    std::vector<Command> queue_;
    std::size_t read_ = 0;
};

} // namespace thunder
