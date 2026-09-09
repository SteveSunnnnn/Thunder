#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace thunder {

ScriptedGameplayRuntime::ScriptedGameplayRuntime(const ScriptRegistry& registry,
                                                 const ScriptProgramDatabase* programs)
    : vm_(registry, programs) {}

void ScriptedGameplayRuntime::clear_content() {
    definitions_.clear();
    instances_.clear();
    log_.clear();
    next_instance_id_ = 1u;
}

std::uint32_t ScriptedGameplayRuntime::add_definition(GameplayDefinition definition) {
    if (definition.key.empty()) throw std::invalid_argument("gameplay definition key is empty");
    for (const auto& existing : definitions_) {
        if (existing.key == definition.key) throw std::invalid_argument("duplicate gameplay definition key");
    }
    if (definition.scope == ScopeType::None) throw std::invalid_argument("gameplay definition scope is none");
    const auto validate_scope = [&definition](const std::optional<ScriptProgram>& program) {
        return !program.has_value() || program->scope == definition.scope;
    };
    if (!validate_scope(definition.potential) || !validate_scope(definition.allow) ||
        !validate_scope(definition.effect) || !validate_scope(definition.completion)) {
        throw std::invalid_argument("gameplay script scope mismatch");
    }
    std::unordered_set<std::string> option_keys;
    for (const auto& option : definition.options) {
        if (option.key.empty()) throw std::invalid_argument("event option key is empty");
        if (!option_keys.insert(option.key).second) throw std::invalid_argument("duplicate event option key");
        if ((option.allow && option.allow->scope != definition.scope) ||
            (option.effect && option.effect->scope != definition.scope)) {
            throw std::invalid_argument("event option script scope mismatch");
        }
    }
    const auto id = static_cast<std::uint32_t>(definitions_.size());
    definitions_.push_back(std::move(definition));
    return id;
}

bool ScriptedGameplayRuntime::passes(const std::optional<ScriptProgram>& program, const World& world,
                                     ScopeRef scope, ScopeRef from, std::uint64_t seed) const {
    if (!program) return true;
    auto context = ScriptExecutionContext::rooted(scope, from, seed);
    return vm_.evaluate(*program, world, std::move(context));
}

bool ScriptedGameplayRuntime::passes(const std::optional<ScriptProgram>& program,
                                     const World& world,
                                     ScriptExecutionContext context) const {
    return !program || vm_.evaluate(*program, world, std::move(context));
}

bool ScriptedGameplayRuntime::apply(const std::optional<ScriptProgram>& program, World& world,
                                    ScopeRef scope, ScopeRef from, std::uint64_t seed) const {
    if (!program) return true;
    auto context = ScriptExecutionContext::rooted(scope, from, seed);
    return vm_.execute_if(*program, world, context);
}

bool ScriptedGameplayRuntime::apply(const std::optional<ScriptProgram>& program, World& world,
                                    ScriptExecutionContext& context) const {
    return !program || vm_.execute_if(*program, world, context);
}

bool ScriptedGameplayRuntime::available(std::uint32_t id, const World& world, ScopeRef scope,
                                        std::uint64_t tick, ScopeRef from) const {
    if (id >= definitions_.size() || !ScopeResolver::valid(world, scope)) return false;
    const auto& definition = definitions_[id];
    if (definition.scope != scope.type || !passes(definition.potential, world, scope, from, tick) ||
        !passes(definition.allow, world, scope, from, tick)) return false;
    for (const auto& instance : instances_) {
        if (instance.definition != id || instance.scope != scope) continue;
        if (instance.active) return false;
        if (definition.kind == GameplayItemKind::Journal && instance.completed) return false;
        if (definition.cooldown_ticks != 0u && tick < instance.last_action_tick + definition.cooldown_ticks) return false;
    }
    return true;
}

void ScriptedGameplayRuntime::append_log(GameplayLogKind kind, std::uint32_t definition,
                                         ScopeRef scope, std::uint64_t tick, std::uint32_t option) {
    log_.push_back({tick, kind, definition, scope, option});
}

bool ScriptedGameplayRuntime::fire(std::uint32_t id, World& world, ScopeRef scope,
                                   std::uint64_t tick, ScopeRef from) {
    if (!available(id, world, scope, tick, from) ||
        next_instance_id_ == GameplayInstanceId::invalid_value) return false;
    const auto& definition = definitions_[id];
    auto context = ScriptExecutionContext::rooted(scope, from, tick);

    const auto append_instance = [&](bool active, bool completed, bool awaiting_choice,
                                     std::optional<ScriptExecutionContext> retained_context) {
        GameplayInstance instance;
        instance.definition = id;
        instance.scope = scope;
        instance.opened_tick = tick;
        instance.last_action_tick = tick;
        instance.active = active;
        instance.completed = completed;
        instance.awaiting_choice = awaiting_choice;
        instance.id = GameplayInstanceId{next_instance_id_++};
        instance.context = std::move(retained_context);
        instances_.push_back(std::move(instance));
    };

    if (definition.kind == GameplayItemKind::Decision) {
        if (!apply(definition.effect, world, context)) return false;
        append_instance(false, true, false, std::nullopt);
        append_log(GameplayLogKind::DecisionTaken, id, scope, tick);
        return true;
    }

    if (definition.kind == GameplayItemKind::Journal) {
        append_instance(true, false, false, std::move(context));
        append_log(GameplayLogKind::JournalOpened, id, scope, tick);
        return true;
    }

    if (!apply(definition.effect, world, context)) return false;
    const bool awaits_choice = !definition.options.empty();
    append_instance(awaits_choice, !awaits_choice, awaits_choice,
                    awaits_choice
                        ? std::optional<ScriptExecutionContext>{std::move(context)}
                        : std::nullopt);
    append_log(GameplayLogKind::EventOpened, id, scope, tick);
    if (!awaits_choice) append_log(GameplayLogKind::EventResolved, id, scope, tick);
    return true;
}

bool ScriptedGameplayRuntime::take_decision(std::uint32_t id, World& world, ScopeRef scope,
                                            std::uint64_t tick) {
    if (id >= definitions_.size() || definitions_[id].kind != GameplayItemKind::Decision) return false;
    return fire(id, world, scope, tick);
}

bool ScriptedGameplayRuntime::choose_event_option(std::uint32_t instance_id, std::uint32_t option_id,
                                                  World& world, std::uint64_t tick) {
    if (instance_id >= instances_.size()) return false;
    return choose_event_option(instances_[instance_id].id, option_id, world, tick);
}

bool ScriptedGameplayRuntime::choose_event_option(GameplayInstanceId instance_id,
                                                  std::uint32_t option_id,
                                                  World& world, std::uint64_t tick) {
    auto found = std::find_if(instances_.begin(), instances_.end(),
        [instance_id](const GameplayInstance& instance) { return instance.id == instance_id; });
    if (found == instances_.end()) return false;
    auto& instance = *found;
    if (!instance.active || !instance.awaiting_choice || instance.definition >= definitions_.size()) return false;
    const auto& definition = definitions_[instance.definition];
    if (definition.kind != GameplayItemKind::Event || option_id >= definition.options.size()) return false;
    const auto& option = definition.options[option_id];
    if (!instance.context.has_value())
        instance.context = ScriptExecutionContext::rooted(instance.scope, {}, instance.opened_tick);
    if (!passes(option.allow, world, *instance.context)) return false;
    auto effect_context = *instance.context;
    if (!apply(option.effect, world, effect_context)) return false;
    instance.selected_option = option_id;
    instance.active = false;
    instance.completed = true;
    instance.awaiting_choice = false;
    instance.last_action_tick = tick;
    instance.context.reset();
    append_log(GameplayLogKind::EventOptionTaken, instance.definition, instance.scope, tick, option_id);
    append_log(GameplayLogKind::EventResolved, instance.definition, instance.scope, tick, option_id);
    return true;
}

void ScriptedGameplayRuntime::update_journals(World& world, std::uint64_t tick) {
    for (std::uint32_t id = 0; id < definitions_.size(); ++id) {
        const auto& definition = definitions_[id];
        if (definition.kind != GameplayItemKind::Journal) continue;

        for (auto& instance : instances_) {
            if (instance.definition != id || !instance.active) continue;
            if (!instance.context.has_value())
                instance.context = ScriptExecutionContext::rooted(instance.scope, {}, instance.opened_tick);
            if (passes(definition.completion, world, *instance.context)) {
                auto effect_context = *instance.context;
                (void)apply(definition.effect, world, effect_context);
                instance.active = false;
                instance.completed = true;
                instance.last_action_tick = tick;
                instance.context.reset();
                append_log(GameplayLogKind::JournalCompleted, id, instance.scope, tick);
            }
        }

        for (const auto scope : ScopeResolver::all(world, definition.scope)) {
            if (available(id, world, scope, tick)) (void)fire(id, world, scope, tick);
        }
    }
}

const GameplayInstance* ScriptedGameplayRuntime::find_instance(GameplayInstanceId id) const noexcept {
    if (!id.valid()) return nullptr;
    const auto found = std::find_if(instances_.begin(), instances_.end(),
        [id](const GameplayInstance& instance) { return instance.id == id; });
    return found == instances_.end() ? nullptr : &*found;
}

std::size_t ScriptedGameplayRuntime::update_auto_events(World& world, std::uint64_t tick,
                                                        std::size_t budget) {
    std::size_t fired = 0;
    for (std::uint32_t id = 0; id < definitions_.size() && fired < budget; ++id) {
        const auto& definition = definitions_[id];
        if (definition.kind != GameplayItemKind::Event || !definition.auto_trigger) continue;
        for (const auto scope : ScopeResolver::all(world, definition.scope)) {
            if (fired >= budget) break;
            if (available(id, world, scope, tick) && fire(id, world, scope, tick)) ++fired;
        }
    }
    return fired;
}

void ScriptedGameplayRuntime::update(World& world, std::uint64_t tick, std::size_t auto_event_budget) {
    update_journals(world, tick);
    (void)update_auto_events(world, tick, auto_event_budget);
}

void ScriptedGameplayRuntime::validate_state(std::span<const GameplayInstance> instances,
                                                   std::span<const GameplayLogEntry> log,
                                                   const World& world,
                                                   std::uint64_t current_tick,
                                                   std::uint64_t next_instance_id) const {
    std::unordered_set<std::uint64_t> ids;
    std::uint64_t maximum_id = 0u;
    if (next_instance_id != 0u &&
        (next_instance_id == GameplayInstanceId::invalid_value || next_instance_id == 0u))
        throw std::runtime_error("gameplay save instance sequence invalid");
    for (const auto& instance : instances) {
        if (instance.definition >= definitions_.size()) throw std::runtime_error("gameplay save references missing definition");
        const auto& definition = definitions_[instance.definition];
        if (instance.scope.type != definition.scope || !ScopeResolver::valid(world, instance.scope))
            throw std::runtime_error("gameplay save scope mismatch or out of range");
        if (instance.opened_tick > current_tick || instance.last_action_tick > current_tick ||
            instance.last_action_tick < instance.opened_tick) {
            throw std::runtime_error("gameplay save tick state invalid");
        }
        if (instance.active && instance.completed) throw std::runtime_error("gameplay save active/completed state invalid");
        if (instance.awaiting_choice && (!instance.active || instance.completed || definition.kind != GameplayItemKind::Event))
            throw std::runtime_error("gameplay save awaiting-choice state invalid");
        if (instance.selected_option != GameplayInstance::no_option) {
            if (definition.kind != GameplayItemKind::Event || instance.selected_option >= definition.options.size())
                throw std::runtime_error("gameplay save option reference invalid");
            if (instance.awaiting_choice) throw std::runtime_error("gameplay save selected option still awaiting choice");
        }
        if (definition.kind != GameplayItemKind::Event && instance.selected_option != GameplayInstance::no_option)
            throw std::runtime_error("non-event gameplay state contains option");
        if (next_instance_id != 0u) {
            if (!instance.id.valid() || instance.id.value() == 0u ||
                !ids.insert(instance.id.value()).second)
                throw std::runtime_error("gameplay save duplicate or invalid instance id");
            maximum_id = std::max(maximum_id, instance.id.value());
        }
        if (instance.context.has_value()) {
            if (!instance.active ||
                (definition.kind == GameplayItemKind::Event && !instance.awaiting_choice) ||
                (definition.kind != GameplayItemKind::Event &&
                 definition.kind != GameplayItemKind::Journal) ||
                !instance.context->validate_persistent(world, instance.scope))
                throw std::runtime_error("gameplay save persistent script context invalid");
        } else if (next_instance_id != 0u && instance.active &&
                   (definition.kind == GameplayItemKind::Journal || instance.awaiting_choice)) {
            throw std::runtime_error("gameplay save live chain has no persistent context");
        }
    }
    if (next_instance_id != 0u && maximum_id >= next_instance_id)
        throw std::runtime_error("gameplay save next instance id is not monotonic");
    for (const auto& entry : log) {
        if (entry.tick > current_tick) throw std::runtime_error("gameplay log tick is in the future");
        if (entry.definition >= definitions_.size()) throw std::runtime_error("gameplay log references missing definition");
        const auto& definition = definitions_[entry.definition];
        if (entry.scope.type != definition.scope || !ScopeResolver::valid(world, entry.scope))
            throw std::runtime_error("gameplay log scope mismatch or out of range");
        if (entry.option != GameplayInstance::no_option) {
            if (definition.kind != GameplayItemKind::Event || entry.option >= definition.options.size())
                throw std::runtime_error("gameplay log option reference invalid");
        }
        switch (entry.kind) {
            case GameplayLogKind::EventOpened:
            case GameplayLogKind::EventOptionTaken:
            case GameplayLogKind::EventResolved:
                if (definition.kind != GameplayItemKind::Event) throw std::runtime_error("gameplay log kind/definition mismatch");
                break;
            case GameplayLogKind::DecisionTaken:
                if (definition.kind != GameplayItemKind::Decision) throw std::runtime_error("gameplay log kind/definition mismatch");
                break;
            case GameplayLogKind::JournalOpened:
            case GameplayLogKind::JournalCompleted:
                if (definition.kind != GameplayItemKind::Journal) throw std::runtime_error("gameplay log kind/definition mismatch");
                break;
            default:
                throw std::runtime_error("gameplay log kind invalid");
        }
    }
}

void ScriptedGameplayRuntime::restore_state(std::vector<GameplayInstance> instances,
                                             std::vector<GameplayLogEntry> log,
                                             std::uint64_t next_instance_id) {
    const bool legacy_state = next_instance_id == 0u;
    for (const auto& instance : instances) {
        if (instance.definition >= definitions_.size()) throw std::runtime_error("gameplay save references missing definition");
        const auto& definition = definitions_[instance.definition];
        if (instance.scope.type != definition.scope) throw std::runtime_error("gameplay save scope mismatch");
        if (instance.selected_option != GameplayInstance::no_option &&
            (definition.kind != GameplayItemKind::Event || instance.selected_option >= definition.options.size())) {
            throw std::runtime_error("gameplay save option reference invalid");
        }
    }
    for (const auto& entry : log) {
        if (entry.definition >= definitions_.size()) throw std::runtime_error("gameplay log references missing definition");
        if (entry.scope.type != definitions_[entry.definition].scope) throw std::runtime_error("gameplay log scope mismatch");
        if (entry.option != GameplayInstance::no_option &&
            (definitions_[entry.definition].kind != GameplayItemKind::Event ||
             entry.option >= definitions_[entry.definition].options.size())) {
            throw std::runtime_error("gameplay log option reference invalid");
        }
    }
    bool any_valid_id = false;
    bool any_invalid_id = false;
    std::uint64_t maximum_id = 0u;
    for (const auto& instance : instances) {
        any_valid_id |= instance.id.valid();
        any_invalid_id |= !instance.id.valid();
        if (instance.id.valid()) maximum_id = std::max(maximum_id, instance.id.value());
    }
    if (any_valid_id && any_invalid_id)
        throw std::runtime_error("gameplay restore mixes stable and legacy instance ids");
    if (!any_valid_id) {
        std::uint64_t assigned = 1u;
        for (auto& instance : instances) instance.id = GameplayInstanceId{assigned++};
        maximum_id = assigned - 1u;
    }
    if (legacy_state) {
        for (auto& instance : instances) {
            if (instance.context || !instance.active || instance.definition >= definitions_.size()) continue;
            const auto kind = definitions_[instance.definition].kind;
            if (kind == GameplayItemKind::Journal ||
                (kind == GameplayItemKind::Event && instance.awaiting_choice)) {
                instance.context = ScriptExecutionContext::rooted(
                    instance.scope, {}, instance.opened_tick);
            }
        }
    }
    if (next_instance_id == 0u) next_instance_id = maximum_id + 1u;
    if (next_instance_id == 0u || next_instance_id == GameplayInstanceId::invalid_value ||
        maximum_id >= next_instance_id)
        throw std::runtime_error("gameplay restore instance sequence invalid");
    instances_ = std::move(instances);
    log_ = std::move(log);
    next_instance_id_ = next_instance_id;
}

std::uint64_t ScriptedGameplayRuntime::checksum_state(std::span<const GameplayInstance> instances,
                                                       std::span<const GameplayLogEntry> log,
                                                       std::uint64_t next_instance_id) const noexcept {
    Fnv1a64 hash;
    std::uint64_t definition_xor = 0;
    std::uint64_t definition_sum = 0;
    for (const auto& definition : definitions_) {
        Fnv1a64 one;
        one.add(std::string_view{definition.key});
        one.add(static_cast<std::uint8_t>(definition.kind));
        one.add(static_cast<std::uint8_t>(definition.scope));
        one.add(definition.cooldown_ticks);
        one.add(definition.auto_trigger);
        std::uint64_t option_xor = 0;
        std::uint64_t option_sum = 0;
        for (const auto& option : definition.options) {
            Fnv1a64 oh;
            oh.add(std::string_view{option.key});
            const auto value = oh.value();
            option_xor ^= value;
            option_sum += value * 0x9e3779b97f4a7c15ull;
        }
        one.add(definition.options.size());
        one.add(option_xor);
        one.add(option_sum);
        const auto value = one.value();
        definition_xor ^= value;
        definition_sum += value * 0x517cc1b727220a95ull;
    }
    hash.add(definitions_.size());
    hash.add(definition_xor);
    hash.add(definition_sum);
    hash.add(instances.size());
    for (const auto& instance : instances) {
        if (instance.definition < definitions_.size()) {
            const auto& definition = definitions_[instance.definition];
            hash.add(std::string_view{definition.key});
            if (instance.selected_option != GameplayInstance::no_option && instance.selected_option < definition.options.size())
                hash.add(std::string_view{definition.options[instance.selected_option].key});
            else
                hash.add(std::uint64_t{0});
        } else {
            hash.add(instance.definition);
            hash.add(instance.selected_option);
        }
        hash.add(static_cast<std::uint8_t>(instance.scope.type));
        hash.add(instance.scope.raw_id);
        hash.add(instance.opened_tick);
        hash.add(instance.last_action_tick);
        hash.add(instance.active);
        hash.add(instance.completed);
        hash.add(instance.awaiting_choice);
    }
    hash.add(log.size());
    for (const auto& entry : log) {
        hash.add(entry.tick);
        hash.add(static_cast<std::uint8_t>(entry.kind));
        if (entry.definition < definitions_.size()) {
            const auto& definition = definitions_[entry.definition];
            hash.add(std::string_view{definition.key});
            if (entry.option != GameplayInstance::no_option && entry.option < definition.options.size())
                hash.add(std::string_view{definition.options[entry.option].key});
            else
                hash.add(std::uint64_t{0});
        } else {
            hash.add(entry.definition);
            hash.add(entry.option);
        }
        hash.add(static_cast<std::uint8_t>(entry.scope.type));
        hash.add(entry.scope.raw_id);
    }
    const bool extended_state = next_instance_id != 0u &&
        (next_instance_id != 1u || std::any_of(instances.begin(), instances.end(),
            [](const GameplayInstance& instance) {
                return instance.id.valid() || instance.context.has_value();
            }));
    if (extended_state) {
        hash.add(std::uint64_t{0x475043545831ull}); // "GPCTX1" domain marker.
        hash.add(next_instance_id);
        for (const auto& instance : instances) {
            hash.add(instance.id.value());
            hash.add(instance.context.has_value());
            if (instance.context) hash.add(instance.context->checksum());
        }
    }
    return hash.value();
}

std::uint64_t ScriptedGameplayRuntime::checksum() const noexcept {
    return checksum_state(instances_, log_, next_instance_id_);
}

} // namespace thunder
