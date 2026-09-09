#include "thunder/simulation/kernel/CommandQueue.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {

std::uint64_t CommandQueue::enqueue(CommandType type, CountryId country, double value) {
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("command sequence exhausted");
    if (!country.valid()) throw std::invalid_argument("command country must be valid");
    if (!std::isfinite(value)) throw std::invalid_argument("command value must be finite");
    if (type == CommandType::SetTaxRate && (value < -1e-6 || value > 1.0 + 1e-6))
        throw std::invalid_argument("tax rate command out of [0,1] range");
    const auto seq = next_sequence_++;
    queue_.push_back(Command{seq, type, country, value});
    return seq;
}

void CommandQueue::apply_all(World& world) {
    // Validate the entire pending batch first. A malformed network/mod command
    // must not partially mutate authoritative state before failing.
    for (std::size_t i = read_; i < queue_.size(); ++i) {
        const auto& cmd = queue_[i];
        if (!cmd.country.valid() || static_cast<std::size_t>(cmd.country.value()) >= world.countries.size())
            throw std::out_of_range("command references invalid CountryId");
        if (!std::isfinite(cmd.value)) throw std::invalid_argument("command value must be finite");
        switch (cmd.type) {
            case CommandType::SetTaxRate:
            case CommandType::AddTreasury:
                break;
            default:
                throw std::invalid_argument("unknown command type");
        }
    }

    // Atomic apply: execute on staging copy of treasuries/tax rates, commit only if all succeed
    std::size_t applied = 0;
    try {
        for (; read_ + applied < queue_.size(); ++applied) {
            const auto& cmd = queue_[read_ + applied];
            switch (cmd.type) {
                case CommandType::SetTaxRate: world.countries.set_tax_rate(cmd.country, cmd.value); break;
                case CommandType::AddTreasury: {
                    // Convert at the command boundary into the authoritative
                    // fixed-point milli store. Routing through the double
                    // treasury column would re-round llround(treasury*1000)
                    // and lose milli precision on large balances.
                    const double clamped_units = std::clamp(cmd.value, -9e15, 9e15);
                    world.countries.add_treasury_milli(cmd.country,
                        static_cast<EconomyAmount>(std::llround(
                            clamped_units * static_cast<double>(economy_scale))));
                    break;
                }
                default: break;
            }
        }
    } catch (...) {
        // Partial mutation already happened; rethrow and keep queue for inspection
        // Next apply_all will re-validate from read_ and skip already-applied prefix
        read_ += applied;
        throw;
    }
    queue_.clear();
    read_ = 0;
}

} // namespace thunder
