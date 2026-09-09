#include "thunder/runtime/save/SaveGameInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace thunder::save_detail

{
std::uint32_t gameplay_definition_id(const ScriptedGameplayRuntime& gameplay, std::string_view key) {
    const auto definitions = gameplay.definitions();
    for (std::uint32_t i = 0; i < definitions.size(); ++i) {
        if (definitions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing gameplay definition: " + std::string(key));
}

std::uint32_t gameplay_option_id(const GameplayDefinition& definition, std::string_view key) {
    for (std::uint32_t i = 0; i < definition.options.size(); ++i) {
        if (definition.options[i].key == key) return i;
    }
    throw std::runtime_error("save references missing event option: " + std::string(key));
}

std::uint32_t ai_action_id(const UtilityAiEngine& ai, std::string_view key) {
    const auto actions = ai.actions();
    for (std::uint32_t i = 0; i < actions.size(); ++i) {
        if (actions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing AI action: " + std::string(key));
}

std::uint32_t ai_plan_id(const UtilityAiEngine& ai, std::string_view key) {
    const auto plans = ai.plans();
    for (std::uint32_t i = 0; i < plans.size(); ++i) {
        if (plans[i].key == key) return i;
    }
    throw std::runtime_error("save references missing AI plan: " + std::string(key));
}

ScopeType read_scope_type(Reader& r) {
    const auto raw = r.u8();
    if (raw < static_cast<std::uint8_t>(ScopeType::Country) ||
        raw > static_cast<std::uint8_t>(ScopeType::Market)) {
        throw std::runtime_error("invalid scope type in Thunder save");
    }
    return static_cast<ScopeType>(raw);
}

ScopeType read_optional_scope_type(Reader& r) {
    const auto raw = r.u8();
    if (raw > static_cast<std::uint8_t>(ScopeType::Market))
        throw std::runtime_error("invalid optional scope type in Thunder save");
    return static_cast<ScopeType>(raw);
}

GameplayLogKind read_log_kind(Reader& r) {
    const auto raw = r.u8();
    if (raw > static_cast<std::uint8_t>(GameplayLogKind::JournalCompleted))
        throw std::runtime_error("invalid gameplay log kind in Thunder save");
    return static_cast<GameplayLogKind>(raw);
}

void write_scope(Writer& w, ScopeRef scope) {
    w.u8(static_cast<std::uint8_t>(scope.type));
    w.u32(scope.raw_id);
}

ScopeRef read_any_scope(Reader& r) {
    return {read_optional_scope_type(r), r.u32()};
}

void encode_script_argument(Writer& w, const ScriptArgument& value) {
    w.u8(static_cast<std::uint8_t>(value.kind));
    switch (value.kind) {
        case ScriptArgumentKind::None:
            throw std::runtime_error("persistent script context contains unset argument");
        case ScriptArgumentKind::Number: w.f64(value.number); break;
        case ScriptArgumentKind::SymbolHash: w.u64(value.symbol_hash); break;
        case ScriptArgumentKind::Boolean: w.boolean(value.boolean_value()); break;
        case ScriptArgumentKind::Scope: write_scope(w, value.scope_value); break;
    }
}

ScriptArgument decode_script_argument(Reader& r) {
    const auto raw_kind = r.u8();
    if (raw_kind == static_cast<std::uint8_t>(ScriptArgumentKind::None) ||
        raw_kind > static_cast<std::uint8_t>(ScriptArgumentKind::Scope))
        throw std::runtime_error("invalid persistent script argument kind");
    const auto kind = static_cast<ScriptArgumentKind>(raw_kind);
    switch (kind) {
        case ScriptArgumentKind::None: break;
        case ScriptArgumentKind::Number: {
            const auto value = ScriptArgument::numeric(r.f64());
            if (!value.valid()) throw std::runtime_error("non-finite number in persistent script context");
            return value;
        }
        case ScriptArgumentKind::SymbolHash: return ScriptArgument::symbol(r.u64());
        case ScriptArgumentKind::Boolean: return ScriptArgument::boolean(r.boolean());
        case ScriptArgumentKind::Scope: return ScriptArgument::scope(read_any_scope(r));
    }
    throw std::runtime_error("invalid persistent script argument");
}

void encode_named_values(Writer& w, std::span<const ScriptNamedValue> values) {
    if (values.size() > max_context_bindings)
        throw std::runtime_error("persistent script binding count exceeds safety cap");
    w.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        w.u64(value.name);
        encode_script_argument(w, value.value);
    }
}

std::vector<ScriptNamedValue> decode_named_values(Reader& r) {
    const auto count = r.count(max_context_bindings);
    std::vector<ScriptNamedValue> values;
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        values.push_back({r.u64(), decode_script_argument(r)});
    return values;
}

void encode_script_context(Writer& w, const ScriptExecutionContext& context) {
    write_scope(w, context.root);
    write_scope(w, context.from);
    write_scope(w, context.current);
    w.u64(context.random_seed);
    if (context.previous.size() > 64u || context.calls.size() > 64u)
        throw std::runtime_error("persistent script context depth exceeds safety cap");
    w.u32(static_cast<std::uint32_t>(context.previous.size()));
    for (const auto scope : context.previous) write_scope(w, scope);
    w.u32(static_cast<std::uint32_t>(context.calls.size()));
    for (const auto& frame : context.calls) {
        encode_named_values(w, frame.parameters);
        encode_named_values(w, frame.variables);
    }
    if (context.event_targets.size() > max_context_bindings ||
        context.collections.size() > max_context_collections ||
        context.random_draws.size() > max_context_bindings)
        throw std::runtime_error("persistent script context exceeds safety cap");
    w.u32(static_cast<std::uint32_t>(context.event_targets.size()));
    for (const auto& target : context.event_targets) {
        w.u64(target.name);
        write_scope(w, target.scope);
    }
    w.u32(static_cast<std::uint32_t>(context.collections.size()));
    std::size_t total_values = 0u;
    for (const auto& collection : context.collections) {
        if (collection.values.size() > max_context_values - total_values)
            throw std::runtime_error("persistent script collection values exceed safety cap");
        total_values += collection.values.size();
        w.u64(collection.name);
        w.u8(static_cast<std::uint8_t>(collection.element_kind));
        w.u8(static_cast<std::uint8_t>(collection.element_scope));
        w.u32(static_cast<std::uint32_t>(collection.values.size()));
        for (const auto& value : collection.values) encode_script_argument(w, value);
    }
    w.u32(static_cast<std::uint32_t>(context.random_draws.size()));
    for (const auto& draw : context.random_draws) {
        w.u64(draw.callsite);
        w.u64(draw.count);
    }
}

ScriptExecutionContext decode_script_context(Reader& r) {
    ScriptExecutionContext context;
    context.root = read_any_scope(r);
    context.from = read_any_scope(r);
    context.current = read_any_scope(r);
    context.random_seed = r.u64();
    const auto previous_count = r.count(64u);
    context.previous.reserve(previous_count);
    for (std::uint32_t i = 0; i < previous_count; ++i)
        context.previous.push_back(read_any_scope(r));
    const auto call_count = r.count(64u);
    context.calls.reserve(call_count);
    for (std::uint32_t i = 0; i < call_count; ++i) {
        ScriptCallFrame frame;
        frame.parameters = decode_named_values(r);
        frame.variables = decode_named_values(r);
        context.calls.push_back(std::move(frame));
    }
    const auto target_count = r.count(max_context_bindings);
    context.event_targets.reserve(target_count);
    for (std::uint32_t i = 0; i < target_count; ++i)
        context.event_targets.push_back({r.u64(), read_any_scope(r)});
    const auto collection_count = r.count(max_context_collections);
    context.collections.reserve(collection_count);
    std::size_t total_values = 0u;
    for (std::uint32_t i = 0; i < collection_count; ++i) {
        ScriptCollection collection;
        collection.name = r.u64();
        const auto raw_kind = r.u8();
        if (raw_kind == static_cast<std::uint8_t>(ScriptArgumentKind::None) ||
            raw_kind > static_cast<std::uint8_t>(ScriptArgumentKind::Scope))
            throw std::runtime_error("invalid persistent collection argument kind");
        collection.element_kind = static_cast<ScriptArgumentKind>(raw_kind);
        collection.element_scope = read_optional_scope_type(r);
        const auto value_count = r.count(max_context_values);
        if (value_count > max_context_values - total_values)
            throw std::runtime_error("persistent collection values exceed safety cap");
        total_values += value_count;
        collection.values.reserve(value_count);
        for (std::uint32_t value = 0; value < value_count; ++value)
            collection.values.push_back(decode_script_argument(r));
        context.collections.push_back(std::move(collection));
    }
    const auto draw_count = r.count(max_context_bindings);
    context.random_draws.reserve(draw_count);
    for (std::uint32_t i = 0; i < draw_count; ++i)
        context.random_draws.push_back({r.u64(), r.u64()});
    return context;
}

void encode_gameplay_context_section(Writer& w, const ScriptedGameplayRuntime& gameplay,
                                     const World& world) {
    w.u32(gameplay_context_section_tag);
    w.u64(gameplay.next_instance_id());
    w.u32(static_cast<std::uint32_t>(gameplay.instances().size()));
    std::unordered_set<std::uint64_t> ids;
    std::uint64_t maximum_id = 0u;
    for (const auto& instance : gameplay.instances()) {
        if (!instance.id.valid() || instance.id.value() == 0u ||
            !ids.insert(instance.id.value()).second)
            throw std::runtime_error("gameplay runtime contains invalid stable instance id");
        maximum_id = std::max(maximum_id, instance.id.value());
        if (instance.context && !instance.context->validate_persistent(world, instance.scope))
            throw std::runtime_error("gameplay runtime contains invalid persistent script context");
        w.u64(instance.id.value());
        w.boolean(instance.context.has_value());
        if (instance.context) encode_script_context(w, *instance.context);
    }
    if (gameplay.next_instance_id() == 0u ||
        gameplay.next_instance_id() == GameplayInstanceId::invalid_value ||
        maximum_id >= gameplay.next_instance_id())
        throw std::runtime_error("gameplay runtime stable instance sequence invalid");
}

void decode_gameplay_context_section(Reader& r, DecodedGameplayState& gameplay) {
    if (r.u32() != gameplay_context_section_tag)
        throw std::runtime_error("invalid gameplay context extension tag");
    if (gameplay.context_section_present)
        throw std::runtime_error("duplicate gameplay context extension");
    gameplay.context_section_present = true;
    gameplay.next_instance_id = r.u64();
    const auto instance_count = r.count(max_records);
    if (instance_count != gameplay.instances.size())
        throw std::runtime_error("gameplay context extension instance count mismatch");
    for (std::uint32_t i = 0; i < instance_count; ++i) {
        gameplay.instances[i].id = GameplayInstanceId{r.u64()};
        if (r.boolean()) gameplay.instances[i].context = decode_script_context(r);
    }
}

void encode_global_script_section(Writer& w, const GlobalScriptStore& global_scripts,
                                  const World& world) {
    if (global_scripts.empty()) return;
    if (!global_scripts.validate(world))
        throw std::runtime_error("global script store contains invalid persistent state");
    w.u32(global_script_section_tag);
    encode_script_context(w, global_scripts.context());
}

void decode_global_script_section(Reader& r, World& world,
                                  DecodedGlobalScriptState& decoded) {
    if (r.u32() != global_script_section_tag)
        throw std::runtime_error("invalid global script extension tag");
    if (decoded.present)
        throw std::runtime_error("duplicate global script extension");
    decoded.present = true;
    world.global_scripts.context() = decode_script_context(r);
}

void encode_gameplay(Writer& w, const ScriptedGameplayRuntime& gameplay) {
    const auto definitions = gameplay.definitions();
    w.u32(static_cast<std::uint32_t>(gameplay.instances().size()));
    for (const auto& instance : gameplay.instances()) {
        if (instance.definition >= definitions.size()) throw std::runtime_error("runtime contains invalid gameplay definition");
        const auto& definition = definitions[instance.definition];
        w.string(definition.key);
        w.u8(static_cast<std::uint8_t>(instance.scope.type)); w.u32(instance.scope.raw_id);
        w.u64(instance.opened_tick); w.u64(instance.last_action_tick);
        const bool has_option = instance.selected_option != GameplayInstance::no_option;
        w.boolean(has_option);
        if (has_option) {
            if (instance.selected_option >= definition.options.size()) throw std::runtime_error("runtime contains invalid gameplay option");
            w.string(definition.options[instance.selected_option].key);
        }
        w.boolean(instance.active); w.boolean(instance.completed); w.boolean(instance.awaiting_choice);
    }
    w.u32(static_cast<std::uint32_t>(gameplay.log().size()));
    for (const auto& entry : gameplay.log()) {
        if (entry.definition >= definitions.size()) throw std::runtime_error("runtime log contains invalid gameplay definition");
        const auto& definition = definitions[entry.definition];
        w.u64(entry.tick); w.u8(static_cast<std::uint8_t>(entry.kind)); w.string(definition.key);
        w.u8(static_cast<std::uint8_t>(entry.scope.type)); w.u32(entry.scope.raw_id);
        const bool has_option = entry.option != GameplayInstance::no_option;
        w.boolean(has_option);
        if (has_option) {
            if (entry.option >= definition.options.size()) throw std::runtime_error("runtime log contains invalid gameplay option");
            w.string(definition.options[entry.option].key);
        }
    }
}

DecodedGameplayState decode_gameplay(Reader& r, const ScriptedGameplayRuntime& gameplay) {
    DecodedGameplayState decoded;
    const auto instance_count = r.count(max_records); decoded.instances.reserve(instance_count);
    for (std::uint32_t i = 0; i < instance_count; ++i) {
        GameplayInstance instance;
        const auto definition_key = r.string();
        instance.definition = gameplay_definition_id(gameplay, definition_key);
        instance.scope = {read_scope_type(r), r.u32()};
        instance.opened_tick = r.u64(); instance.last_action_tick = r.u64();
        if (r.boolean()) {
            const auto option_key = r.string();
            instance.selected_option = gameplay_option_id(gameplay.definitions()[instance.definition], option_key);
        } else {
            instance.selected_option = GameplayInstance::no_option;
        }
        instance.active = r.boolean(); instance.completed = r.boolean(); instance.awaiting_choice = r.boolean();
        decoded.instances.push_back(instance);
    }
    const auto log_count = r.count(max_records); decoded.log.reserve(log_count);
    for (std::uint32_t i = 0; i < log_count; ++i) {
        GameplayLogEntry entry;
        entry.tick = r.u64(); entry.kind = read_log_kind(r);
        const auto definition_key = r.string();
        entry.definition = gameplay_definition_id(gameplay, definition_key);
        entry.scope = {read_scope_type(r), r.u32()};
        if (r.boolean()) {
            const auto option_key = r.string();
            entry.option = gameplay_option_id(gameplay.definitions()[entry.definition], option_key);
        } else {
            entry.option = GameplayInstance::no_option;
        }
        decoded.log.push_back(entry);
    }
    return decoded;
}

void encode_ai(Writer& w, const UtilityAiEngine& ai) {
    const auto actions = ai.actions();
    w.u32(static_cast<std::uint32_t>(ai.state().size()));
    for (const auto& state : ai.state()) {
        if (state.action >= actions.size()) throw std::runtime_error("runtime contains invalid AI action");
        w.string(actions[state.action].key);
        w.u8(static_cast<std::uint8_t>(state.scope.type)); w.u32(state.scope.raw_id); w.u64(state.last_tick);
    }
    const auto plans = ai.plans();
    w.u32(static_cast<std::uint32_t>(ai.plan_state().size()));
    for (const auto& state : ai.plan_state()) {
        if (state.plan >= plans.size()) throw std::runtime_error("runtime contains invalid AI plan");
        w.string(plans[state.plan].key);
        w.u8(static_cast<std::uint8_t>(state.scope.type)); w.u32(state.scope.raw_id);
        w.u64(state.started_tick); w.u64(state.last_tick);
    }
}

DecodedAiState decode_ai(Reader& r, const UtilityAiEngine& ai, bool has_plans) {
    DecodedAiState decoded;
    const auto count = r.count(max_records); decoded.actions.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        AiActionState record;
        record.action = ai_action_id(ai, r.string());
        record.scope = {read_scope_type(r), r.u32()}; record.last_tick = r.u64();
        decoded.actions.push_back(record);
    }
    if (has_plans) {
        const auto plan_count = r.count(max_records); decoded.plans.reserve(plan_count);
        for (std::uint32_t i = 0; i < plan_count; ++i) {
            AiPlanState record;
            record.plan = ai_plan_id(ai, r.string());
            record.scope = {read_scope_type(r), r.u32()};
            record.started_tick = r.u64(); record.last_tick = r.u64();
            decoded.plans.push_back(record);
        }
    }
    return decoded;
}

std::uint32_t notification_definition_id(const NotificationRuntime& notifications,
                                         std::string_view key) {
    const auto definitions = notifications.definitions();
    for (std::uint32_t i = 0; i < definitions.size(); ++i) {
        if (definitions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing notification definition: " +
                             std::string{key});
}

std::uint32_t notification_action_id(const NotificationDefinition& definition,
                                     std::string_view key) {
    for (std::uint32_t i = 0; i < definition.actions.size(); ++i) {
        if (definition.actions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing notification action: " +
                             std::string{key});
}

void encode_notification_section(Writer& w, const NotificationRuntime& notifications) {
    w.u32(notification_section_tag);
    w.u64(notifications.next_instance_id());
    w.u32(static_cast<std::uint32_t>(notifications.instances().size()));
    const auto definitions = notifications.definitions();
    for (const auto& instance : notifications.instances()) {
        if (instance.definition >= definitions.size())
            throw std::runtime_error("runtime contains invalid notification definition");
        const auto& definition = definitions[instance.definition];
        w.u64(instance.id.value());
        w.string(definition.key);
        const auto write_scope = [&w](ScopeRef scope) {
            w.u8(static_cast<std::uint8_t>(scope.type));
            w.u32(scope.raw_id);
        };
        write_scope(instance.scope);
        write_scope(instance.source);
        write_scope(instance.map_target);
        w.u64(instance.created_tick);
        w.u64(instance.updated_tick);
        w.u64(instance.expires_tick);
        w.u64(instance.dedupe_key);
        w.u32(instance.occurrence_count);
        w.u8(static_cast<std::uint8_t>(instance.state));
        const bool has_action = instance.chosen_action != NotificationInstance::no_action;
        w.boolean(has_action);
        if (has_action) {
            if (instance.chosen_action >= definition.actions.size())
                throw std::runtime_error("runtime contains invalid notification action");
            w.string(definition.actions[instance.chosen_action].key);
        }
    }
}

DecodedNotificationState decode_notification_section(
    Reader& r, const NotificationRuntime& notifications) {
    DecodedNotificationState decoded;
    if (r.u32() != notification_section_tag)
        throw std::runtime_error("unknown extension section in Thunder save");
    decoded.present = true;
    decoded.next_instance_id = r.u64();
    const auto count = r.count(max_notifications);
    decoded.instances.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        NotificationInstance instance;
        instance.id = NotificationInstanceId{r.u64()};
        instance.definition = notification_definition_id(notifications, r.string());
        instance.scope = {read_scope_type(r), r.u32()};
        instance.source = {read_optional_scope_type(r), r.u32()};
        instance.map_target = {read_optional_scope_type(r), r.u32()};
        instance.created_tick = r.u64();
        instance.updated_tick = r.u64();
        instance.expires_tick = r.u64();
        instance.dedupe_key = r.u64();
        instance.occurrence_count = r.u32();
        const auto state = r.u8();
        if (state > static_cast<std::uint8_t>(NotificationState::Expired))
            throw std::runtime_error("invalid notification state in Thunder save");
        instance.state = static_cast<NotificationState>(state);
        if (r.boolean()) {
            const auto action_key = r.string();
            instance.chosen_action = notification_action_id(
                notifications.definitions()[instance.definition], action_key);
        } else {
            instance.chosen_action = NotificationInstance::no_action;
        }
        decoded.instances.push_back(instance);
    }
    return decoded;
}
} // namespace thunder::save_detail
