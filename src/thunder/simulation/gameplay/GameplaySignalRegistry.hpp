#pragma once
// Generic gameplay signal bus - engine-level, content-agnostic.
// Replaces the previous polling-only OnActionRuntime with a typed,
// content-driven signal registry. Signals are stable-key identified so
// mods can add new signals without engine changes; subscribers are
// deterministically ordered (stable key + registration order) and
// dispatch is via deterministic command staging, never direct world
// mutation inside the signal emit path.
//
// See docs/ARCHITECTURE.md law 2: Player/UI actions become deterministic
// Commands. Signals follow the same rule.

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScriptValue.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

class World;
class ScriptedGameplayRuntime;

using GameplaySignalId = std::uint32_t;
inline constexpr GameplaySignalId kInvalidGameplaySignal = 0xFFFFFFFFu;

struct GameplaySignalDefinition {
    std::string key;                 // stable key e.g. "on_monthly_pulse"
    ScriptStableKey stable_key = 0;  // FNV-1a of key
    // Payload scope type: content may attach e.g. Country, State, Province
    // as signal subject. Engine does not hard-code domain payloads.
    std::uint32_t payload_scope_hash = 0;
};

struct GameplaySignalSubscriber {
    GameplaySignalId signal{};
    std::uint64_t subscriber_key = 0; // stable key of handler (event id etc.)
    std::uint32_t priority = 100;     // lower = earlier, stable tie-break by key
};

class GameplaySignalRegistry {
public:
    GameplaySignalId register_signal(GameplaySignalDefinition def);
    [[nodiscard]] GameplaySignalId find_signal(std::string_view key) const noexcept;
    [[nodiscard]] GameplaySignalId find_signal(ScriptStableKey stable_key) const noexcept;

    void subscribe(GameplaySignalId signal, GameplaySignalSubscriber sub);
    void unsubscribe(GameplaySignalId signal, std::uint64_t subscriber_key);

    // Deterministic dispatch: collects subscribers in priority+stable_key
    // order and invokes handler via command staging. Returns dispatched count.
    std::size_t emit(World& world, ScriptedGameplayRuntime& gameplay,
                     GameplaySignalId signal, std::uint64_t tick) const;

    [[nodiscard]] std::span<const GameplaySignalDefinition> signals() const noexcept { return signals_; }
    [[nodiscard]] std::vector<GameplaySignalSubscriber> subscribers_for(GameplaySignalId id) const;

    void clear() noexcept { signals_.clear(); subscribers_.clear(); lookup_.clear(); }

private:
    std::vector<GameplaySignalDefinition> signals_;
    std::vector<GameplaySignalSubscriber> subscribers_;
    // stable_key -> index in signals_
    std::vector<std::pair<ScriptStableKey, GameplaySignalId>> lookup_;
};

} // namespace thunder
