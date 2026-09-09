#include "thunder/simulation/gameplay/NotificationRuntime.hpp"

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace thunder {

NotificationRuntime::NotificationRuntime(const ScriptRegistry& registry,
                                         const ScriptProgramDatabase* programs)
    : vm_(registry, programs) {}

void NotificationRuntime::set_program_database(const ScriptProgramDatabase* programs) noexcept {
    vm_.set_program_database(programs);
}

void NotificationRuntime::clear_content() {
    definitions_.clear();
    clear_state();
}

void NotificationRuntime::clear_state() noexcept {
    instances_.clear();
    next_instance_id_ = 1u;
}

std::uint32_t NotificationRuntime::add_definition(NotificationDefinition definition) {
    if (definition.key.empty()) throw std::invalid_argument("notification definition key is empty");
    if (definition.scope == ScopeType::None) throw std::invalid_argument("notification definition scope is none");
    for (const auto& existing : definitions_) {
        if (existing.key == definition.key) throw std::invalid_argument("duplicate notification definition key");
    }
    if (definition.potential && definition.potential->scope != definition.scope)
        throw std::invalid_argument("notification potential scope mismatch");
    std::unordered_set<std::string> action_keys;
    for (const auto& action : definition.actions) {
        if (action.key.empty()) throw std::invalid_argument("notification action key is empty");
        if (!action_keys.insert(action.key).second)
            throw std::invalid_argument("duplicate notification action key");
        if ((action.allow && action.allow->scope != definition.scope) ||
            (action.effect && action.effect->scope != definition.scope))
            throw std::invalid_argument("notification action scope mismatch");
    }
    if (definitions_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::overflow_error("too many notification definitions");
    const auto id = static_cast<std::uint32_t>(definitions_.size());
    definitions_.push_back(std::move(definition));
    return id;
}

bool NotificationRuntime::active(NotificationState state) const noexcept {
    return state == NotificationState::Unread || state == NotificationState::Read;
}

bool NotificationRuntime::optional_scope_valid(const World& world, ScopeRef scope) const noexcept {
    return scope.type == ScopeType::None ? scope.raw_id == 0u : ScopeResolver::valid(world, scope);
}

NotificationInstanceId NotificationRuntime::emit(std::uint32_t definition_id, const World& world,
                                                  ScopeRef scope, std::uint64_t tick,
                                                  ScopeRef source, ScopeRef map_target,
                                                  std::uint64_t dedupe_key) {
    if (definition_id >= definitions_.size()) return {};
    const auto& definition = definitions_[definition_id];
    if (scope.type != definition.scope || !ScopeResolver::valid(world, scope) ||
        !optional_scope_valid(world, source) || !optional_scope_valid(world, map_target)) return {};
    if (definition.potential && !vm_.evaluate(*definition.potential, world, scope)) return {};

    if (definition.dedupe != NotificationDedupePolicy::Stack) {
        for (auto& instance : instances_) {
            if (!active(instance.state) || instance.definition != definition_id ||
                instance.scope != scope || instance.dedupe_key != dedupe_key) continue;
            if (instance.occurrence_count != std::numeric_limits<std::uint32_t>::max())
                ++instance.occurrence_count;
            instance.updated_tick = tick;
            if (definition.lifetime_ticks != 0u)
                instance.expires_tick = tick > std::numeric_limits<std::uint64_t>::max() - definition.lifetime_ticks
                    ? std::numeric_limits<std::uint64_t>::max()
                    : tick + definition.lifetime_ticks;
            if (definition.dedupe == NotificationDedupePolicy::Replace) {
                instance.source = source;
                instance.map_target = map_target;
                instance.state = NotificationState::Unread;
                instance.chosen_action = NotificationInstance::no_action;
            }
            return instance.id;
        }
    }

    if (next_instance_id_ == NotificationInstanceId::invalid_value) return {};
    if (instances_.size() >= max_instances) {
        // Nothing transitions out of Expired or Dismissed and nothing was ever
        // removed, so the table grew without bound until it hit max_instances
        // — after which emit() silently dropped every new notification for the
        // rest of the game. Reclaim terminal instances instead. Read and
        // Actioned are kept because the UI still queries them for history.
        // This only runs at the cap, so a just-expired notification stays
        // inspectable for the normal lifetime of a game.
        std::erase_if(instances_, [](const NotificationInstance& instance) {
            return instance.state == NotificationState::Expired ||
                   instance.state == NotificationState::Dismissed;
        });
        if (instances_.size() >= max_instances) return {};
    }
    NotificationInstance instance;
    instance.id = NotificationInstanceId{next_instance_id_++};
    instance.definition = definition_id;
    instance.scope = scope;
    instance.source = source;
    instance.map_target = map_target;
    instance.created_tick = tick;
    instance.updated_tick = tick;
    instance.expires_tick = definition.lifetime_ticks == 0u ? 0u :
        (tick > std::numeric_limits<std::uint64_t>::max() - definition.lifetime_ticks
            ? std::numeric_limits<std::uint64_t>::max() : tick + definition.lifetime_ticks);
    instance.dedupe_key = dedupe_key;
    instances_.push_back(instance);
    return instance.id;
}

NotificationInstance* NotificationRuntime::mutable_instance(NotificationInstanceId id) noexcept {
    if (!id.valid()) return nullptr;
    for (auto& instance : instances_) if (instance.id == id) return &instance;
    return nullptr;
}

const NotificationInstance* NotificationRuntime::find_instance(NotificationInstanceId id) const noexcept {
    if (!id.valid()) return nullptr;
    for (const auto& instance : instances_) if (instance.id == id) return &instance;
    return nullptr;
}

bool NotificationRuntime::mark_read(NotificationInstanceId id, std::uint64_t tick) {
    auto* instance = mutable_instance(id);
    if (instance == nullptr || instance->state != NotificationState::Unread || tick < instance->created_tick)
        return false;
    instance->state = NotificationState::Read;
    instance->updated_tick = tick;
    return true;
}

bool NotificationRuntime::dismiss(NotificationInstanceId id, std::uint64_t tick) {
    auto* instance = mutable_instance(id);
    if (instance == nullptr || !active(instance->state) || tick < instance->created_tick) return false;
    instance->state = NotificationState::Dismissed;
    instance->updated_tick = tick;
    return true;
}

bool NotificationRuntime::choose_action(NotificationInstanceId id, std::uint32_t action_id,
                                        World& world, std::uint64_t tick) {
    auto* instance = mutable_instance(id);
    if (instance == nullptr || !active(instance->state) || instance->definition >= definitions_.size() ||
        tick < instance->created_tick) return false;
    const auto& definition = definitions_[instance->definition];
    if (action_id >= definition.actions.size()) return false;
    const auto& action = definition.actions[action_id];
    if (action.allow && !vm_.evaluate(*action.allow, world, instance->scope)) return false;
    if (action.effect && !vm_.execute_if(*action.effect, world, instance->scope, instance->source, tick))
        return false;
    instance->chosen_action = action_id;
    instance->state = NotificationState::Actioned;
    instance->updated_tick = tick;
    return true;
}

std::size_t NotificationRuntime::update(std::uint64_t tick) noexcept {
    std::size_t expired = 0u;
    for (auto& instance : instances_) {
        if (active(instance.state) && instance.expires_tick != 0u && tick >= instance.expires_tick) {
            instance.state = NotificationState::Expired;
            instance.updated_tick = tick;
            ++expired;
        }
    }
    return expired;
}

std::size_t NotificationRuntime::unread_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(instances_.begin(), instances_.end(),
        [](const auto& instance) { return instance.state == NotificationState::Unread; }));
}

void NotificationRuntime::validate_state(std::span<const NotificationInstance> instances,
                                         std::uint64_t next_instance_id, const World& world,
                                         std::uint64_t current_tick) const {
    if (instances.size() > max_instances || next_instance_id == 0u ||
        next_instance_id == NotificationInstanceId::invalid_value)
        throw std::runtime_error("notification save instance sequence invalid");
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(instances.size());
    std::uint64_t maximum_id = 0u;
    for (const auto& instance : instances) {
        if (!instance.id.valid() || instance.id.value() == 0u || !ids.insert(instance.id.value()).second)
            throw std::runtime_error("notification save contains duplicate or invalid instance id");
        maximum_id = std::max(maximum_id, instance.id.value());
        if (instance.definition >= definitions_.size())
            throw std::runtime_error("notification save references missing definition");
        const auto& definition = definitions_[instance.definition];
        if (instance.scope.type != definition.scope || !ScopeResolver::valid(world, instance.scope) ||
            !optional_scope_valid(world, instance.source) || !optional_scope_valid(world, instance.map_target))
            throw std::runtime_error("notification save scope mismatch or invalid reference");
        if (instance.created_tick > current_tick || instance.updated_tick > current_tick ||
            instance.updated_tick < instance.created_tick ||
            (instance.expires_tick != 0u && instance.expires_tick < instance.created_tick) ||
            instance.occurrence_count == 0u)
            throw std::runtime_error("notification save tick/count state invalid");
        if (static_cast<std::uint8_t>(instance.state) > static_cast<std::uint8_t>(NotificationState::Expired))
            throw std::runtime_error("notification save state invalid");
        if (instance.chosen_action != NotificationInstance::no_action) {
            if (instance.chosen_action >= definition.actions.size() ||
                instance.state != NotificationState::Actioned)
                throw std::runtime_error("notification save action state invalid");
        } else if (instance.state == NotificationState::Actioned) {
            throw std::runtime_error("notification save actioned state has no action");
        }
    }
    if (maximum_id >= next_instance_id)
        throw std::runtime_error("notification save next instance id is not monotonic");
}

void NotificationRuntime::restore_state(std::vector<NotificationInstance> instances,
                                        std::uint64_t next_instance_id) {
    if (next_instance_id == 0u || next_instance_id == NotificationInstanceId::invalid_value)
        throw std::runtime_error("notification restore sequence invalid");
    instances_ = std::move(instances);
    next_instance_id_ = next_instance_id;
}

std::uint64_t NotificationRuntime::checksum_state(
    std::span<const NotificationInstance> instances, std::uint64_t next_instance_id) const noexcept {
    Fnv1a64 hash;
    std::uint64_t definition_xor = 0u;
    std::uint64_t definition_sum = 0u;
    for (const auto& definition : definitions_) {
        Fnv1a64 one;
        one.add(std::string_view{definition.key});
        one.add(static_cast<std::uint8_t>(definition.scope));
        one.add(std::string_view{definition.title_key});
        one.add(std::string_view{definition.body_key});
        one.add(std::string_view{definition.icon_key});
        one.add(std::string_view{definition.category_key});
        one.add(static_cast<std::uint8_t>(definition.priority));
        one.add(static_cast<std::uint8_t>(definition.dedupe));
        one.add(definition.lifetime_ticks);
        for (const auto& action : definition.actions) {
            one.add(std::string_view{action.key});
            one.add(std::string_view{action.label_key});
        }
        const auto value = one.value();
        definition_xor ^= value;
        definition_sum += value * 0x9e3779b97f4a7c15ull;
    }
    hash.add(definitions_.size());
    hash.add(definition_xor);
    hash.add(definition_sum);
    hash.add(next_instance_id);
    hash.add(instances.size());
    for (const auto& instance : instances) {
        hash.add(instance.id.value());
        if (instance.definition < definitions_.size()) {
            const auto& definition = definitions_[instance.definition];
            hash.add(std::string_view{definition.key});
            if (instance.chosen_action != NotificationInstance::no_action &&
                instance.chosen_action < definition.actions.size())
                hash.add(std::string_view{definition.actions[instance.chosen_action].key});
            else hash.add(std::uint64_t{0u});
        } else {
            hash.add(instance.definition);
            hash.add(instance.chosen_action);
        }
        const auto add_scope = [&hash](ScopeRef scope) {
            hash.add(static_cast<std::uint8_t>(scope.type));
            hash.add(scope.raw_id);
        };
        add_scope(instance.scope);
        add_scope(instance.source);
        add_scope(instance.map_target);
        hash.add(instance.created_tick);
        hash.add(instance.updated_tick);
        hash.add(instance.expires_tick);
        hash.add(instance.dedupe_key);
        hash.add(instance.occurrence_count);
        hash.add(static_cast<std::uint8_t>(instance.state));
    }
    return hash.value();
}

std::uint64_t NotificationRuntime::checksum() const noexcept {
    return checksum_state(instances_, next_instance_id_);
}

} // namespace thunder
