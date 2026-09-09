#include "thunder/scripting/GlobalScriptStore.hpp"
#include "thunder/simulation/kernel/World.hpp"

namespace thunder {

bool GlobalScriptStore::validate(const World& world) const noexcept {
    if (empty()) return true;
    // A global context may deliberately use the untyped root (ScopeType::None)
    // for world-level variables. The context validator still checks every
    // binding, collection value and event target against the restored World.
    const auto expected_root = context_.root.valid() ? context_.root : ScopeRef{};
    return context_.validate_persistent(world, expected_root);
}

bool GlobalScriptStore::empty() const noexcept {
    return context_.root == ScopeRef{} && context_.from == ScopeRef{} &&
        context_.current == ScopeRef{} && context_.random_seed == 0u &&
        context_.previous.empty() && context_.calls.empty() &&
        context_.event_targets.empty() && context_.collections.empty() &&
        context_.random_draws.empty();
}

} // namespace thunder
