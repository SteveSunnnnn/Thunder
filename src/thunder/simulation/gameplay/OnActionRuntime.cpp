#include "thunder/simulation/gameplay/OnActionRuntime.hpp"

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace thunder {

OnActionRuntime::OnActionRuntime(const ScriptRegistry& registry,
                                 const ScriptProgramDatabase* programs)
    : vm_(registry, programs) {}

void OnActionRuntime::set_program_database(const ScriptProgramDatabase* programs) noexcept {
    vm_.set_program_database(programs);
}

void OnActionRuntime::clear_content() {
    definitions_.clear();
    clear_state();
}

void OnActionRuntime::clear_state() noexcept {
    queue_.clear();
    next_invocation_id_ = 1u;
}

std::uint32_t OnActionRuntime::add_definition(OnActionDefinition definition) {
    if (definition.key.empty()) throw std::invalid_argument("on_action definition key is empty");
    if (definition.scope == ScopeType::None)
        throw std::invalid_argument("on_action definition scope is none");
    if (definition.steps.empty())
        throw std::invalid_argument("on_action definition has no actions");
    if (find_definition(definition.key) != std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("duplicate on_action definition key");

    for (const auto& step : definition.steps) {
        if (step.target_key.empty())
            throw std::invalid_argument("on_action action target key is empty");
        if (step.kind == OnActionStepKind::Script) {
            if (!step.script || step.script->scope != definition.scope ||
                step.gameplay_definition != OnActionStepDefinition::invalid_gameplay_definition)
                throw std::invalid_argument("on_action script action is invalid or has wrong scope");
        } else if (step.kind == OnActionStepKind::Event) {
            if (step.script ||
                step.gameplay_definition == OnActionStepDefinition::invalid_gameplay_definition)
                throw std::invalid_argument("on_action event action is unresolved");
        } else {
            throw std::invalid_argument("on_action action kind is invalid");
        }
    }

    const auto id = static_cast<std::uint32_t>(definitions_.size());
    definitions_.push_back(std::move(definition));
    return id;
}

std::uint32_t OnActionRuntime::find_definition(std::string_view key) const noexcept {
    for (std::uint32_t index = 0u; index < definitions_.size(); ++index) {
        if (definitions_[index].key == key) return index;
    }
    return std::numeric_limits<std::uint32_t>::max();
}

bool OnActionRuntime::optional_scope_valid(const World& world, ScopeRef scope) const noexcept {
    return !scope.valid() || ScopeResolver::valid(world, scope);
}

bool OnActionRuntime::ordered_before(const ScheduledOnActionInvocation& left,
                                     const ScheduledOnActionInvocation& right) noexcept {
    if (left.due_tick != right.due_tick) return left.due_tick < right.due_tick;
    return left.id.value() < right.id.value();
}

OnActionInvocationId OnActionRuntime::schedule_at(std::uint32_t definition,
                                                  const World& world, ScopeRef scope,
                                                  std::uint64_t due_tick, ScopeRef from) {
    if (definition >= definitions_.size())
        throw std::invalid_argument("on_action schedule references missing definition");
    if (scope.type != definitions_[definition].scope || !ScopeResolver::valid(world, scope))
        throw std::invalid_argument("on_action schedule scope mismatch or out of range");
    if (!optional_scope_valid(world, from))
        throw std::invalid_argument("on_action schedule FROM scope is out of range");
    if (queue_.size() >= max_invocations)
        throw std::length_error("on_action schedule exceeds invocation cap");
    if (next_invocation_id_ == 0u ||
        next_invocation_id_ == OnActionInvocationId::invalid_value)
        throw std::overflow_error("on_action invocation id space exhausted");

    ScheduledOnActionInvocation invocation;
    invocation.id = OnActionInvocationId{next_invocation_id_++};
    invocation.definition = definition;
    invocation.scope = scope;
    invocation.from = from;
    invocation.due_tick = due_tick;
    const auto position = std::upper_bound(
        queue_.begin(), queue_.end(), invocation,
        [](const ScheduledOnActionInvocation& value,
           const ScheduledOnActionInvocation& existing) {
            return ordered_before(value, existing);
        });
    queue_.insert(position, invocation);
    return invocation.id;
}

OnActionInvocationId OnActionRuntime::schedule_after(std::uint32_t definition,
                                                     const World& world, ScopeRef scope,
                                                     std::uint64_t current_tick,
                                                     std::uint64_t delay_ticks,
                                                     ScopeRef from) {
    if (delay_ticks > std::numeric_limits<std::uint64_t>::max() - current_tick)
        throw std::overflow_error("on_action delayed schedule tick overflow");
    return schedule_at(definition, world, scope, current_tick + delay_ticks, from);
}

bool OnActionRuntime::cancel(OnActionInvocationId id) noexcept {
    if (!id.valid()) return false;
    const auto found = std::find_if(queue_.begin(), queue_.end(),
        [id](const ScheduledOnActionInvocation& invocation) { return invocation.id == id; });
    if (found == queue_.end()) return false;
    queue_.erase(found);
    return true;
}

std::size_t OnActionRuntime::dispatch_due(World& world, ScriptedGameplayRuntime& gameplay,
                                          std::uint64_t tick, std::size_t budget) {
    std::size_t dispatched = 0u;
    while (dispatched < budget && !queue_.empty() && queue_.front().due_tick <= tick) {
        const auto invocation = queue_.front();
        queue_.erase(queue_.begin());
        if (invocation.definition >= definitions_.size())
            throw std::runtime_error("on_action queue references missing definition");
        if (!ScopeResolver::valid(world, invocation.scope) ||
            !optional_scope_valid(world, invocation.from))
            throw std::runtime_error("on_action queue contains stale scope reference");

        const auto& definition = definitions_[invocation.definition];
        if (invocation.scope.type != definition.scope)
            throw std::runtime_error("on_action queue scope no longer matches definition");
        for (const auto& step : definition.steps) {
            if (step.kind == OnActionStepKind::Script) {
                if (!step.script)
                    throw std::runtime_error("on_action contains unresolved script action");
                auto context = ScriptExecutionContext::rooted(
                    invocation.scope, invocation.from,
                    invocation.id.value() ^ invocation.due_tick);
                (void)vm_.execute_if(*step.script, world, context);
                continue;
            }

            if (step.gameplay_definition >= gameplay.definitions().size())
                throw std::runtime_error("on_action contains unresolved event action");
            const auto& event = gameplay.definitions()[step.gameplay_definition];
            if (event.key != step.target_key || event.kind != GameplayItemKind::Event ||
                event.scope != definition.scope)
                throw std::runtime_error("on_action event action no longer matches bound content");
            (void)gameplay.fire(step.gameplay_definition, world, invocation.scope, tick,
                                invocation.from);
        }
        ++dispatched;
    }
    return dispatched;
}

void OnActionRuntime::validate_state(std::span<const ScheduledOnActionInvocation> queue,
                                     std::uint64_t next_invocation_id,
                                     const World& world) const {
    if (queue.size() > max_invocations)
        throw std::runtime_error("on_action save invocation count exceeds cap");
    if (next_invocation_id == 0u ||
        next_invocation_id == OnActionInvocationId::invalid_value)
        throw std::runtime_error("on_action save invocation sequence invalid");

    std::unordered_set<std::uint64_t> ids;
    ids.reserve(queue.size());
    std::uint64_t maximum_id = 0u;
    const ScheduledOnActionInvocation* previous = nullptr;
    for (const auto& invocation : queue) {
        if (!invocation.id.valid() || invocation.id.value() == 0u ||
            !ids.insert(invocation.id.value()).second)
            throw std::runtime_error("on_action save contains duplicate or invalid invocation id");
        if (invocation.definition >= definitions_.size())
            throw std::runtime_error("on_action save references missing definition");
        const auto& definition = definitions_[invocation.definition];
        if (invocation.scope.type != definition.scope ||
            !ScopeResolver::valid(world, invocation.scope))
            throw std::runtime_error("on_action save scope mismatch or out of range");
        if (!optional_scope_valid(world, invocation.from))
            throw std::runtime_error("on_action save FROM scope is out of range");
        if (previous != nullptr && !ordered_before(*previous, invocation))
            throw std::runtime_error("on_action save queue order is not canonical");
        previous = &invocation;
        maximum_id = std::max(maximum_id, invocation.id.value());
    }
    if (maximum_id >= next_invocation_id)
        throw std::runtime_error("on_action save next invocation id is not monotonic");
}

void OnActionRuntime::restore_state(std::vector<ScheduledOnActionInvocation> queue,
                                    std::uint64_t next_invocation_id) {
    if (next_invocation_id == 0u ||
        next_invocation_id == OnActionInvocationId::invalid_value)
        throw std::runtime_error("on_action restore invocation sequence invalid");
    queue_ = std::move(queue);
    next_invocation_id_ = next_invocation_id;
}

std::uint64_t OnActionRuntime::checksum_state(
    std::span<const ScheduledOnActionInvocation> queue,
    std::uint64_t next_invocation_id) const noexcept {
    Fnv1a64 hash;
    hash.add(std::string_view{"Thunder.OnActionRuntime.v1"});

    std::uint64_t definition_xor = 0u;
    std::uint64_t definition_sum = 0u;
    for (const auto& definition : definitions_) {
        Fnv1a64 one;
        one.add(std::string_view{definition.key});
        one.add(static_cast<std::uint8_t>(definition.scope));
        one.add(definition.steps.size());
        for (const auto& step : definition.steps) {
            one.add(static_cast<std::uint8_t>(step.kind));
            one.add(std::string_view{step.target_key});
        }
        const auto value = one.value();
        definition_xor ^= value;
        definition_sum += value * 0x9e3779b97f4a7c15ull;
    }
    hash.add(definitions_.size());
    hash.add(definition_xor);
    hash.add(definition_sum);
    hash.add(next_invocation_id);
    hash.add(queue.size());
    for (const auto& invocation : queue) {
        hash.add(invocation.id.value());
        if (invocation.definition < definitions_.size())
            hash.add(std::string_view{definitions_[invocation.definition].key});
        else
            hash.add(invocation.definition);
        hash.add(static_cast<std::uint8_t>(invocation.scope.type));
        hash.add(invocation.scope.raw_id);
        hash.add(static_cast<std::uint8_t>(invocation.from.type));
        hash.add(invocation.from.raw_id);
        hash.add(invocation.due_tick);
    }
    return hash.value();
}

std::uint64_t OnActionRuntime::checksum() const noexcept {
    return checksum_state(queue_, next_invocation_id_);
}

} // namespace thunder
