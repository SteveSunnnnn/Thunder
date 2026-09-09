#include "thunder/scripting/ScriptContext.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace thunder {
namespace {

template <typename Binding>
auto lower_binding(std::vector<Binding>& values, ScriptStableKey name) {
    return std::lower_bound(values.begin(), values.end(), name,
        [](const Binding& binding, ScriptStableKey key) { return binding.name < key; });
}

template <typename Binding>
auto lower_binding(const std::vector<Binding>& values, ScriptStableKey name) {
    return std::lower_bound(values.begin(), values.end(), name,
        [](const Binding& binding, ScriptStableKey key) { return binding.name < key; });
}

bool normalize_argument(ScriptArgument& value) noexcept {
    switch (value.kind) {
        case ScriptArgumentKind::None:
            return false;
        case ScriptArgumentKind::Number: {
            constexpr auto maximum = std::numeric_limits<double>::max();
            if (!(value.number >= -maximum && value.number <= maximum)) return false;
            if (value.number == 0.0) value.number = 0.0;
            value.symbol_hash = 0u;
            value.scope_value = {};
            return true;
        }
        case ScriptArgumentKind::SymbolHash:
            value.number = 0.0;
            value.scope_value = {};
            return true;
        case ScriptArgumentKind::Boolean:
            value.number = value.number == 0.0 ? 0.0 : 1.0;
            value.symbol_hash = 0u;
            value.scope_value = {};
            return true;
        case ScriptArgumentKind::Scope:
            if (!value.scope_value.valid()) return false;
            value.number = 0.0;
            value.symbol_hash = 0u;
            return true;
    }
    return false;
}

bool optional_scope_valid(const World& world, ScopeRef scope) noexcept {
    return scope.type == ScopeType::None ? scope.raw_id == 0u : ScopeResolver::valid(world, scope);
}

bool canonical_argument(const World& world, const ScriptArgument& value) noexcept {
    switch (value.kind) {
        case ScriptArgumentKind::None:
            return false;
        case ScriptArgumentKind::Number:
            return std::isfinite(value.number) &&
                (value.number != 0.0 || !std::signbit(value.number)) &&
                value.symbol_hash == 0u && value.scope_value == ScopeRef{};
        case ScriptArgumentKind::SymbolHash:
            return value.number == 0.0 && !std::signbit(value.number) &&
                value.scope_value == ScopeRef{};
        case ScriptArgumentKind::Boolean:
            return (value.number == 0.0 || value.number == 1.0) &&
                !std::signbit(value.number) && value.symbol_hash == 0u &&
                value.scope_value == ScopeRef{};
        case ScriptArgumentKind::Scope:
            return value.number == 0.0 && !std::signbit(value.number) &&
                value.symbol_hash == 0u && ScopeResolver::valid(world, value.scope_value);
    }
    return false;
}

template <typename Binding>
bool stable_names_valid(const std::vector<Binding>& values) noexcept {
    ScriptStableKey previous = 0u;
    for (const auto& value : values) {
        if (value.name == 0u || (previous != 0u && value.name <= previous)) return false;
        previous = value.name;
    }
    return true;
}

void set_named_value(std::vector<ScriptNamedValue>& values, ScriptStableKey name,
                     ScriptArgument value) {
    if (!normalize_argument(value)) return;
    const auto it = lower_binding(values, name);
    if (it != values.end() && it->name == name) {
        it->value = value;
        return;
    }
    values.insert(it, {name, value});
}

void hash_scope(Fnv1a64& hash, ScopeRef scope) noexcept {
    hash.add(static_cast<std::uint8_t>(scope.type));
    hash.add(scope.raw_id);
}

void hash_value(Fnv1a64& hash, const ScriptArgument& value) noexcept {
    hash.add(static_cast<std::uint8_t>(value.kind));
    switch (value.kind) {
        case ScriptArgumentKind::None: break;
        case ScriptArgumentKind::Number: hash.add(value.number); break;
        case ScriptArgumentKind::SymbolHash: hash.add(value.symbol_hash); break;
        case ScriptArgumentKind::Boolean: hash.add(value.boolean_value()); break;
        case ScriptArgumentKind::Scope: hash_scope(hash, value.scope_value); break;
    }
}

void ensure_membership_index(ScriptCollection& collection) {
    if (collection.membership_valid) return;
    collection.membership.clear();
    collection.membership.reserve(collection.values.size());
    collection.membership.insert(collection.values.begin(), collection.values.end());
    collection.membership_valid = true;
}

} // namespace

ScriptExecutionContext ScriptExecutionContext::rooted(ScopeRef scope, ScopeRef from_scope,
                                                       std::uint64_t seed) {
    ScriptExecutionContext context;
    context.root = scope;
    context.from = from_scope;
    context.current = scope;
    context.random_seed = seed;
    context.calls.emplace_back();
    return context;
}

void ScriptExecutionContext::enter(ScopeRef scope) {
    previous.push_back(current);
    current = scope;
}

void ScriptExecutionContext::leave() noexcept {
    if (previous.empty()) return;
    current = previous.back();
    previous.pop_back();
}

void ScriptExecutionContext::push_call_frame(std::span<const ScriptNamedValue> parameters) {
    ScriptCallFrame frame;
    frame.parameters.reserve(parameters.size());
    for (const auto& parameter_value : parameters) {
        if (parameter_value.name == 0 || !parameter_value.value.valid()) continue;
        set_named_value(frame.parameters, parameter_value.name, parameter_value.value);
    }
    calls.push_back(std::move(frame));
}

void ScriptExecutionContext::pop_call_frame() noexcept {
    // The root frame belongs to the invocation and is retained until the context dies.
    if (calls.size() > 1u) calls.pop_back();
}

void ScriptExecutionContext::set_parameter(ScriptStableKey name, ScriptArgument value) {
    if (name == 0 || !value.valid()) return;
    if (calls.empty()) calls.emplace_back();
    set_named_value(calls.back().parameters, name, value);
}

ScriptArgument ScriptExecutionContext::parameter(ScriptStableKey name) const noexcept {
    if (calls.empty() || name == 0) return {};
    const auto& values = calls.back().parameters;
    const auto it = lower_binding(values, name);
    return it != values.end() && it->name == name ? it->value : ScriptArgument{};
}

bool ScriptExecutionContext::has_parameter(ScriptStableKey name) const noexcept {
    return parameter(name).valid();
}

void ScriptExecutionContext::set_variable(ScriptStableKey name, ScriptArgument value) {
    if (name == 0 || !value.valid()) return;
    if (calls.empty()) calls.emplace_back();
    set_named_value(calls.back().variables, name, value);
}

ScriptArgument ScriptExecutionContext::variable(ScriptStableKey name) const noexcept {
    if (name == 0) return {};
    for (auto frame = calls.rbegin(); frame != calls.rend(); ++frame) {
        const auto it = lower_binding(frame->variables, name);
        if (it != frame->variables.end() && it->name == name) return it->value;
    }
    return {};
}

bool ScriptExecutionContext::has_variable(ScriptStableKey name) const noexcept {
    return variable(name).valid();
}

bool ScriptExecutionContext::change_variable(ScriptStableKey name, double delta) noexcept {
    if (name == 0) return false;
    for (auto frame = calls.rbegin(); frame != calls.rend(); ++frame) {
        const auto it = lower_binding(frame->variables, name);
        if (it == frame->variables.end() || it->name != name) continue;
        if (it->value.kind != ScriptArgumentKind::Number) return false;
        auto replacement = ScriptArgument::numeric(it->value.number + delta);
        if (!replacement.valid()) return false;
        it->value = replacement;
        return true;
    }
    return false;
}

bool ScriptExecutionContext::clear_variable(ScriptStableKey name) noexcept {
    if (name == 0) return false;
    for (auto frame = calls.rbegin(); frame != calls.rend(); ++frame) {
        const auto it = lower_binding(frame->variables, name);
        if (it == frame->variables.end() || it->name != name) continue;
        frame->variables.erase(it);
        return true;
    }
    return false;
}

void ScriptExecutionContext::save_event_target(ScriptStableKey name, ScopeRef scope) {
    if (name == 0 || !scope.valid()) return;
    const auto it = lower_binding(event_targets, name);
    if (it != event_targets.end() && it->name == name) {
        it->scope = scope;
        return;
    }
    event_targets.insert(it, {name, scope});
}

ScopeRef ScriptExecutionContext::event_target(ScriptStableKey name) const noexcept {
    const auto it = lower_binding(event_targets, name);
    return it != event_targets.end() && it->name == name ? it->scope : ScopeRef{};
}

bool ScriptExecutionContext::clear_event_target(ScriptStableKey name) noexcept {
    const auto it = lower_binding(event_targets, name);
    if (it == event_targets.end() || it->name != name) return false;
    event_targets.erase(it);
    return true;
}

bool ScriptExecutionContext::add_to_collection(ScriptStableKey name, ScriptArgument value,
                                               bool unique) {
    if (name == 0 || !normalize_argument(value)) return false;
    if (value.kind == ScriptArgumentKind::Scope && !value.scope_value.valid()) return false;
    auto it = lower_binding(collections, name);
    if (it == collections.end() || it->name != name) {
        const auto scope_type = value.kind == ScriptArgumentKind::Scope
            ? value.scope_value.type : ScopeType::None;
        ScriptCollection collection;
        collection.name = name;
        collection.element_kind = value.kind;
        collection.element_scope = scope_type;
        it = collections.insert(it, std::move(collection));
    }
    if (it->element_kind != value.kind) return false;
    if (value.kind == ScriptArgumentKind::Scope &&
        it->element_scope != value.scope_value.type) return false;
    if (unique) {
        ensure_membership_index(*it);
        if (it->membership.contains(value)) return true;
    }
    it->values.push_back(value);
    if (it->membership_valid) it->membership.insert(value);
    return true;
}

bool ScriptExecutionContext::remove_from_collection(ScriptStableKey name,
                                                    ScriptArgument value) noexcept {
    if (!normalize_argument(value)) return false;
    const auto it = lower_binding(collections, name);
    if (it == collections.end() || it->name != name || it->element_kind != value.kind) return false;
    if (value.kind == ScriptArgumentKind::Scope &&
        it->element_scope != value.scope_value.type) return false;
    const auto found = std::find(it->values.begin(), it->values.end(), value);
    if (found == it->values.end()) return false;
    it->values.erase(found);
    if (it->membership_valid) {
        it->membership.erase(value);
        if (std::find(it->values.begin(), it->values.end(), value) != it->values.end()) {
            it->membership.insert(value);
        }
    }
    return true;
}

bool ScriptExecutionContext::clear_collection(ScriptStableKey name) noexcept {
    const auto it = lower_binding(collections, name);
    if (it == collections.end() || it->name != name) return false;
    collections.erase(it);
    return true;
}

std::span<const ScriptArgument> ScriptExecutionContext::collection(ScriptStableKey name) const noexcept {
    const auto it = lower_binding(collections, name);
    if (it == collections.end() || it->name != name) return {};
    return it->values;
}

ScriptArgumentKind ScriptExecutionContext::collection_kind(ScriptStableKey name) const noexcept {
    const auto it = lower_binding(collections, name);
    return it == collections.end() || it->name != name ? ScriptArgumentKind::None : it->element_kind;
}

ScopeType ScriptExecutionContext::collection_scope(ScriptStableKey name) const noexcept {
    const auto it = lower_binding(collections, name);
    return it == collections.end() || it->name != name ? ScopeType::None : it->element_scope;
}

std::uint64_t ScriptExecutionContext::consume_random_draw(std::uint64_t callsite) noexcept {
    const auto it = std::lower_bound(random_draws.begin(), random_draws.end(), callsite,
        [](const ScriptRandomDraw& draw, std::uint64_t key) { return draw.callsite < key; });
    if (it != random_draws.end() && it->callsite == callsite) {
        const auto draw = it->count;
        if (it->count != std::numeric_limits<std::uint64_t>::max()) ++it->count;
        return draw;
    }
    random_draws.insert(it, {callsite, 1u});
    return 0u;
}

bool ScriptExecutionContext::validate_persistent(const World& world,
                                                 ScopeRef expected_root) const noexcept {
    constexpr std::size_t max_bindings = 65'536u;
    constexpr std::size_t max_collections = 4'096u;
    constexpr std::size_t max_collection_values = 1'000'000u;
    const bool untyped_global_root = expected_root.type == ScopeType::None;
    if (root != expected_root || current != expected_root ||
        (!untyped_global_root && !ScopeResolver::valid(world, root)) ||
        !optional_scope_valid(world, from) ||
        !previous.empty() || calls.size() != 1u ||
        event_targets.size() > max_bindings || collections.size() > max_collections ||
        random_draws.size() > max_bindings) return false;

    const auto& root_frame = calls.front();
    if (root_frame.parameters.size() > max_bindings || root_frame.variables.size() > max_bindings ||
        !stable_names_valid(root_frame.parameters) || !stable_names_valid(root_frame.variables)) return false;
    for (const auto& value : root_frame.parameters)
        if (!canonical_argument(world, value.value)) return false;
    for (const auto& value : root_frame.variables)
        if (!canonical_argument(world, value.value)) return false;

    if (!stable_names_valid(event_targets)) return false;
    for (const auto& target : event_targets)
        if (!ScopeResolver::valid(world, target.scope)) return false;

    if (!stable_names_valid(collections)) return false;
    std::size_t collection_value_count = 0u;
    for (const auto& collection_value : collections) {
        if (collection_value.element_kind == ScriptArgumentKind::None) return false;
        if (collection_value.element_kind == ScriptArgumentKind::Scope) {
            if (collection_value.element_scope == ScopeType::None) return false;
        } else if (collection_value.element_scope != ScopeType::None) return false;
        if (collection_value.values.size() > max_collection_values - collection_value_count) return false;
        collection_value_count += collection_value.values.size();
        for (const auto& value : collection_value.values) {
            if (value.kind != collection_value.element_kind || !canonical_argument(world, value)) return false;
            if (value.kind == ScriptArgumentKind::Scope &&
                value.scope_value.type != collection_value.element_scope) return false;
        }
    }

    std::uint64_t previous_callsite = 0u;
    bool first_draw = true;
    for (const auto& draw : random_draws) {
        if (draw.count == 0u || (!first_draw && draw.callsite <= previous_callsite)) return false;
        first_draw = false;
        previous_callsite = draw.callsite;
    }
    return true;
}

std::uint64_t ScriptExecutionContext::checksum() const noexcept {
    Fnv1a64 hash;
    hash_scope(hash, root);
    hash_scope(hash, from);
    hash_scope(hash, current);
    hash.add(random_seed);
    hash.add(static_cast<std::uint64_t>(previous.size()));
    for (const auto scope : previous) hash_scope(hash, scope);

    hash.add(static_cast<std::uint64_t>(calls.size()));
    for (const auto& frame : calls) {
        hash.add(static_cast<std::uint64_t>(frame.parameters.size()));
        for (const auto& binding : frame.parameters) {
            hash.add(binding.name);
            hash_value(hash, binding.value);
        }
        hash.add(static_cast<std::uint64_t>(frame.variables.size()));
        for (const auto& binding : frame.variables) {
            hash.add(binding.name);
            hash_value(hash, binding.value);
        }
    }

    hash.add(static_cast<std::uint64_t>(event_targets.size()));
    for (const auto& binding : event_targets) {
        hash.add(binding.name);
        hash_scope(hash, binding.scope);
    }
    hash.add(static_cast<std::uint64_t>(collections.size()));
    for (const auto& collection_value : collections) {
        hash.add(collection_value.name);
        hash.add(static_cast<std::uint8_t>(collection_value.element_kind));
        hash.add(static_cast<std::uint8_t>(collection_value.element_scope));
        hash.add(static_cast<std::uint64_t>(collection_value.values.size()));
        for (const auto& value : collection_value.values) hash_value(hash, value);
    }
    hash.add(static_cast<std::uint64_t>(random_draws.size()));
    for (const auto& draw : random_draws) {
        hash.add(draw.callsite);
        hash.add(draw.count);
    }
    return hash.value();
}

} // namespace thunder
