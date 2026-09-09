#include "thunder/simulation/gameplay/GameplaySignalRegistry.hpp"
#include "thunder/scripting/ScriptValue.hpp"
#include <algorithm>

namespace thunder {

GameplaySignalId GameplaySignalRegistry::register_signal(GameplaySignalDefinition def) {
    if (def.stable_key == 0) def.stable_key = script_stable_key(def.key);
    if (auto existing = find_signal(def.stable_key); existing != kInvalidGameplaySignal) {
        signals_[existing] = std::move(def);
        return existing;
    }
    const auto id = static_cast<GameplaySignalId>(signals_.size());
    lookup_.emplace_back(def.stable_key, id);
    signals_.push_back(std::move(def));
    // Keep lookup sorted for deterministic binary search
    std::sort(lookup_.begin(), lookup_.end(),
              [](auto& a, auto& b){ return a.first < b.first; });
    return id;
}

GameplaySignalId GameplaySignalRegistry::find_signal(std::string_view key) const noexcept {
    return find_signal(script_stable_key(key));
}

GameplaySignalId GameplaySignalRegistry::find_signal(ScriptStableKey stable_key) const noexcept {
    auto it = std::lower_bound(lookup_.begin(), lookup_.end(), stable_key,
        [](const auto& p, ScriptStableKey k){ return p.first < k; });
    if (it == lookup_.end() || it->first != stable_key) return kInvalidGameplaySignal;
    return it->second;
}

void GameplaySignalRegistry::subscribe(GameplaySignalId signal, GameplaySignalSubscriber sub) {
    sub.signal = signal;
    subscribers_.push_back(sub);
    // Deterministic order: priority asc then subscriber_key asc
    std::stable_sort(subscribers_.begin(), subscribers_.end(),
        [](const GameplaySignalSubscriber& a, const GameplaySignalSubscriber& b){
            if (a.signal != b.signal) return a.signal < b.signal;
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.subscriber_key < b.subscriber_key;
        });
}

void GameplaySignalRegistry::unsubscribe(GameplaySignalId signal, std::uint64_t subscriber_key) {
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
            [&](const GameplaySignalSubscriber& s){
                return s.signal == signal && s.subscriber_key == subscriber_key;
            }),
        subscribers_.end());
}

std::size_t GameplaySignalRegistry::emit(World&, ScriptedGameplayRuntime&,
                                         GameplaySignalId, std::uint64_t) const {
    // Stub deterministic dispatch - content handlers are invoked via
    // ScriptProgramDatabase in the full integration. This keeps the
    // registry header-only testable without pulling gameplay deps.
    // Returns subscriber count for telemetry; no world mutation here.
    return subscribers_.size();
}

std::vector<GameplaySignalSubscriber> GameplaySignalRegistry::subscribers_for(GameplaySignalId id) const {
    // Linear scan is fine: subscriber count is content-bounded (<10k)
    // and dispatch is not in the per-POP hot path.
    //
    // Returned by value on purpose. This used to hand out a span over a
    // thread_local scratch buffer, so the next call (or a nested one in the
    // same iteration) silently invalidated the result still held by the caller.
    std::vector<GameplaySignalSubscriber> out;
    for (const auto& s : subscribers_) if (s.signal == id) out.push_back(s);
    return out;
}

} // namespace thunder
