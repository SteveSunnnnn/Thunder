#pragma once
#include "thunder/scripting/ScriptValue.hpp"
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace thunder {

class World;

struct ScriptCallFrame {
    // Both vectors are maintained in ascending stable-key order. Parameters are
    // immutable to script operations; variables are lexical invocation locals.
    std::vector<ScriptNamedValue> parameters;
    std::vector<ScriptNamedValue> variables;
};

struct ScriptEventTargetBinding {
    ScriptStableKey name = 0;
    ScopeRef scope{};
};

struct ScriptCollection {
    ScriptStableKey name = 0;
    ScriptArgumentKind element_kind = ScriptArgumentKind::None;
    // Scope is a tagged argument kind. Lock its concrete subtype on first insert
    // so a collection cannot alias country/pop/state ids under one name.
    ScopeType element_scope = ScopeType::None;
    // Collection order is semantic and therefore participates in deterministic
    // iteration and checksums. Script effects use unique insertion by default.
    std::vector<ScriptArgument> values;
    // Non-authoritative lazy membership index. It is never iterated, saved or
    // checksummed; authoritative ordering remains exclusively in values.
    std::unordered_set<ScriptArgument, ScriptArgumentHash> membership;
    bool membership_valid = false;
};

struct ScriptRandomDraw {
    std::uint64_t callsite = 0u;
    std::uint64_t count = 0u;
};

// ThunderScript execution state. Ordinary calls keep it transient; long-lived
// gameplay instances may explicitly retain the canonical root form across ticks
// and persist it through their authoritative store. Stable keys and a
// deterministic checksum keep replay/desync identity independent of SymbolId
// ordering, while VM-only caches and work counters remain derived state.
struct ScriptExecutionContext {
    static constexpr std::uint64_t default_work_budget = 1'000'000u;

    ScopeRef root{};
    ScopeRef from{};
    ScopeRef current{};
    std::uint64_t random_seed = 0;
    std::vector<ScopeRef> previous;
    std::vector<ScriptCallFrame> calls;
    std::vector<ScriptEventTargetBinding> event_targets;
    std::vector<ScriptCollection> collections;
    // Sorted by stable callsite key. Random iterators consume a counter so two
    // executions at one callsite form a deterministic sequence instead of repeating.
    std::vector<ScriptRandomDraw> random_draws;
    // VM-local guardrail. It is reset for every public invocation and deliberately
    // excluded from save/checksum because it is not simulation state.
    std::uint64_t transient_work_remaining = 0u;

    [[nodiscard]] static ScriptExecutionContext rooted(ScopeRef scope, ScopeRef from_scope = {},
                                                       std::uint64_t seed = 0);

    [[nodiscard]] ScopeRef prev() const noexcept {
        return previous.empty() ? ScopeRef{} : previous.back();
    }

    void enter(ScopeRef scope);
    void leave() noexcept;

    void push_call_frame(std::span<const ScriptNamedValue> parameters = {});
    void pop_call_frame() noexcept;
    [[nodiscard]] std::size_t call_depth() const noexcept { return calls.size(); }

    void set_parameter(ScriptStableKey name, ScriptArgument value);
    [[nodiscard]] ScriptArgument parameter(ScriptStableKey name) const noexcept;
    [[nodiscard]] bool has_parameter(ScriptStableKey name) const noexcept;

    // set_variable writes the current invocation frame. Reads and changes use
    // lexical lookup from the innermost frame outward.
    void set_variable(ScriptStableKey name, ScriptArgument value);
    [[nodiscard]] ScriptArgument variable(ScriptStableKey name) const noexcept;
    [[nodiscard]] bool has_variable(ScriptStableKey name) const noexcept;
    [[nodiscard]] bool change_variable(ScriptStableKey name, double delta) noexcept;
    [[nodiscard]] bool clear_variable(ScriptStableKey name) noexcept;

    void save_event_target(ScriptStableKey name, ScopeRef scope);
    [[nodiscard]] ScopeRef event_target(ScriptStableKey name) const noexcept;
    [[nodiscard]] bool clear_event_target(ScriptStableKey name) noexcept;
    // Compatibility aliases for existing save_scope_as / saved:name content.
    void save(ScriptStableKey name, ScopeRef scope) { save_event_target(name, scope); }
    [[nodiscard]] ScopeRef saved(ScriptStableKey name) const noexcept { return event_target(name); }

    [[nodiscard]] bool add_to_collection(ScriptStableKey name, ScriptArgument value,
                                         bool unique = true);
    [[nodiscard]] bool remove_from_collection(ScriptStableKey name, ScriptArgument value) noexcept;
    [[nodiscard]] bool clear_collection(ScriptStableKey name) noexcept;
    [[nodiscard]] std::span<const ScriptArgument> collection(ScriptStableKey name) const noexcept;
    [[nodiscard]] ScriptArgumentKind collection_kind(ScriptStableKey name) const noexcept;
    [[nodiscard]] ScopeType collection_scope(ScriptStableKey name) const noexcept;
    [[nodiscard]] std::uint64_t consume_random_draw(std::uint64_t callsite) noexcept;

    [[nodiscard]] bool validate_persistent(const World& world,
                                           ScopeRef expected_root) const noexcept;
    void begin_execution(std::uint64_t budget = default_work_budget) noexcept {
        transient_work_remaining = budget;
    }
    [[nodiscard]] bool consume_work(std::uint64_t amount = 1u) noexcept {
        if (amount > transient_work_remaining) return false;
        transient_work_remaining -= amount;
        return true;
    }

    [[nodiscard]] std::uint64_t checksum() const noexcept;
};

} // namespace thunder
