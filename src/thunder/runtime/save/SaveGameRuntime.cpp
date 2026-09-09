#include "thunder/runtime/save/SaveGameInternal.hpp"

#include <cstdint>

namespace thunder::save_detail {

// Save schema v3 predates strategic plan definitions/state. Preserve its exact
// AI checksum algorithm for read-only migration instead of weakening validation.
std::uint64_t legacy_ai_checksum_v3(const UtilityAiEngine& ai,
                                    std::span<const AiActionState> state) noexcept {
    Fnv1a64 hash;
    std::uint64_t action_xor = 0;
    std::uint64_t action_sum = 0;
    for (const auto& action : ai.actions()) {
        Fnv1a64 one;
        one.add(std::string_view{action.key});
        one.add(static_cast<std::uint8_t>(action.scope));
        one.add(action.base_utility);
        one.add(action.cooldown_ticks);
        const auto value = one.value();
        action_xor ^= value;
        action_sum += value * 0x9e3779b97f4a7c15ull;
    }
    hash.add(ai.actions().size()); hash.add(action_xor); hash.add(action_sum);
    std::uint64_t state_xor = 0;
    std::uint64_t state_sum = 0;
    for (const auto& record : state) {
        Fnv1a64 one;
        if (record.action < ai.actions().size()) one.add(std::string_view{ai.actions()[record.action].key});
        else one.add(record.action);
        one.add(static_cast<std::uint8_t>(record.scope.type)); one.add(record.scope.raw_id); one.add(record.last_tick);
        const auto value = one.value();
        state_xor ^= value; state_sum += value * 0x517cc1b727220a95ull;
    }
    hash.add(state.size()); hash.add(state_xor); hash.add(state_sum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(gameplay_checksum);
    hash.add(ai_checksum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum,
                               std::uint64_t clock_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(runtime_checksum(gameplay_checksum, ai_checksum));
    hash.add(clock_checksum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum,
                               std::uint64_t clock_checksum,
                               std::uint64_t notification_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(runtime_checksum(gameplay_checksum, ai_checksum, clock_checksum));
    hash.add(notification_checksum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum,
                               std::uint64_t clock_checksum,
                               std::uint64_t notification_checksum,
                               std::uint64_t on_action_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(runtime_checksum(gameplay_checksum, ai_checksum, clock_checksum,
                              notification_checksum));
    hash.add(on_action_checksum);
    return hash.value();
}
} // namespace thunder::save_detail
