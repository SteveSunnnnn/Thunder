#include "thunder/content/definition/GameplayContent.hpp"
#include <cmath>
#include <limits>
#include <string>

namespace thunder {
namespace {

bool symbol_field(const ScriptNode& node, SymbolId& out) noexcept {
    if (node.kind != ScriptValueKind::Symbol) return false;
    out = node.symbol;
    return true;
}

} // namespace

GameplayContentDatabase::GameplayContentDatabase(SymbolTable& symbols) : symbols_(symbols) {
    sym_event_ = symbols_.intern("event");
    sym_decision_ = symbols_.intern("decision");
    sym_journal_ = symbols_.intern("journal");
    sym_ai_action_ = symbols_.intern("ai_action");
    sym_ai_plan_ = symbols_.intern("ai_plan");
    sym_scope_ = symbols_.intern("scope");
    sym_potential_ = symbols_.intern("potential");
    sym_allow_ = symbols_.intern("allow");
    sym_effect_ = symbols_.intern("effect");
    sym_completion_ = symbols_.intern("completion");
    sym_cooldown_ticks_ = symbols_.intern("cooldown_ticks");
    sym_auto_trigger_ = symbols_.intern("auto_trigger");
    sym_option_ = symbols_.intern("option");
    sym_key_ = symbols_.intern("key");
    sym_valid_ = symbols_.intern("valid");
    sym_utility_ = symbols_.intern("utility");
    sym_base_utility_ = symbols_.intern("base_utility");
    sym_priority_ = symbols_.intern("priority");
    sym_base_priority_ = symbols_.intern("base_priority");
    sym_commitment_ticks_ = symbols_.intern("commitment_ticks");
    sym_action_ = symbols_.intern("action");
    gameplay_lookup_.reserve(512u);
    ai_lookup_.reserve(256u);
    ai_plan_lookup_.reserve(128u);
}

ScopeType GameplayContentDatabase::parse_scope(const ScriptNode& node) const noexcept {
    if (node.kind != ScriptValueKind::Symbol) return ScopeType::None;
    const auto value = symbols_.text(node.symbol);
    if (value == "country") return ScopeType::Country;
    if (value == "state") return ScopeType::State;
    if (value == "province") return ScopeType::Province;
    if (value == "pop") return ScopeType::Pop;
    if (value == "market") return ScopeType::Market;
    return ScopeType::None;
}

bool GameplayContentDatabase::parse_bool(const ScriptNode& node, bool& value) const {
    if (node.kind == ScriptValueKind::Number) {
        value = node.number != 0.0;
        return true;
    }
    if (node.kind != ScriptValueKind::Symbol) return false;
    const auto text = symbols_.text(node.symbol);
    if (text == "yes" || text == "true" || text == "on") { value = true; return true; }
    if (text == "no" || text == "false" || text == "off") { value = false; return true; }
    return false;
}

bool GameplayContentDatabase::parse_u32(const ScriptNode& node, std::uint32_t& value) const noexcept {
    if (node.kind != ScriptValueKind::Number || !std::isfinite(node.number) || node.number < 0.0 ||
        node.number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) return false;
    value = static_cast<std::uint32_t>(node.number);
    return true;
}

bool GameplayContentDatabase::parse_gameplay_object(const ScriptObject& object, GameplayItemKind kind,
                                                    GameplayDefinitionSpec& out,
                                                    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    out.key = object.name;
    out.kind = kind;
    out.line = object.line;
    bool ok = true;
    for (const auto& field : object.fields) {
        if (field.key == sym_scope_) {
            out.scope = parse_scope(field);
            if (out.scope == ScopeType::None) { diagnostics.push_back({"gameplay object requires valid scope", field.line}); ok = false; }
        } else if (field.key == sym_potential_) {
            if (!symbol_field(field, out.potential)) { diagnostics.push_back({"potential requires script name", field.line}); ok = false; }
        } else if (field.key == sym_allow_) {
            if (!symbol_field(field, out.allow)) { diagnostics.push_back({"allow requires script name", field.line}); ok = false; }
        } else if (field.key == sym_effect_) {
            if (!symbol_field(field, out.effect)) { diagnostics.push_back({"effect requires script name", field.line}); ok = false; }
        } else if (field.key == sym_completion_) {
            if (!symbol_field(field, out.completion)) { diagnostics.push_back({"completion requires script name", field.line}); ok = false; }
        } else if (field.key == sym_cooldown_ticks_) {
            if (!parse_u32(field, out.cooldown_ticks)) { diagnostics.push_back({"cooldown_ticks requires non-negative uint32", field.line}); ok = false; }
        } else if (field.key == sym_auto_trigger_) {
            if (!parse_bool(field, out.auto_trigger)) { diagnostics.push_back({"auto_trigger requires yes/no or numeric boolean", field.line}); ok = false; }
        } else if (field.key == sym_option_) {
            if (kind != GameplayItemKind::Event) {
                diagnostics.push_back({"option is only valid on event objects", field.line});
                ok = false;
                continue;
            }
            if (field.kind != ScriptValueKind::Block) {
                diagnostics.push_back({"event option requires block", field.line});
                ok = false;
                continue;
            }
            GameplayOptionSpec option;
            option.line = field.line;
            for (const auto& child : field.children) {
                if (child.key == sym_key_) {
                    if (!symbol_field(child, option.key)) { diagnostics.push_back({"event option key requires symbol", child.line}); ok = false; }
                } else if (child.key == sym_allow_) {
                    if (!symbol_field(child, option.allow)) { diagnostics.push_back({"event option allow requires script name", child.line}); ok = false; }
                } else if (child.key == sym_effect_) {
                    if (!symbol_field(child, option.effect)) { diagnostics.push_back({"event option effect requires script name", child.line}); ok = false; }
                }
            }
            if (!option.key.valid()) { diagnostics.push_back({"event option requires key", field.line}); ok = false; }
            else out.options.push_back(option);
        }
    }
    if (out.scope == ScopeType::None) {
        diagnostics.push_back({"gameplay object requires scope", object.line});
        ok = false;
    }
    if (kind == GameplayItemKind::Journal && !out.completion.valid()) {
        diagnostics.push_back({"journal requires completion script", object.line});
        ok = false;
    }
    return ok;
}

bool GameplayContentDatabase::parse_ai_object(const ScriptObject& object, AiActionSpec& out,
                                              std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    out.key = object.name;
    out.line = object.line;
    bool ok = true;
    for (const auto& field : object.fields) {
        if (field.key == sym_scope_) {
            out.scope = parse_scope(field);
            if (out.scope == ScopeType::None) { diagnostics.push_back({"ai_action requires valid scope", field.line}); ok = false; }
        } else if (field.key == sym_valid_) {
            if (!symbol_field(field, out.valid)) { diagnostics.push_back({"ai_action valid requires script name", field.line}); ok = false; }
        } else if (field.key == sym_utility_) {
            if (!symbol_field(field, out.utility)) { diagnostics.push_back({"ai_action utility requires scripted_value name", field.line}); ok = false; }
        } else if (field.key == sym_effect_) {
            if (!symbol_field(field, out.effect)) { diagnostics.push_back({"ai_action effect requires script name", field.line}); ok = false; }
        } else if (field.key == sym_base_utility_) {
            if (field.kind != ScriptValueKind::Number || !std::isfinite(field.number)) {
                diagnostics.push_back({"base_utility requires finite number", field.line});
                ok = false;
            } else out.base_utility = field.number;
        } else if (field.key == sym_cooldown_ticks_) {
            if (!parse_u32(field, out.cooldown_ticks)) { diagnostics.push_back({"cooldown_ticks requires non-negative uint32", field.line}); ok = false; }
        }
    }
    if (out.scope == ScopeType::None) { diagnostics.push_back({"ai_action requires scope", object.line}); ok = false; }
    return ok;
}

bool GameplayContentDatabase::parse_ai_plan_object(const ScriptObject& object, AiPlanSpec& out,
                                                   std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    out.key = object.name;
    out.line = object.line;
    bool ok = true;
    for (const auto& field : object.fields) {
        if (field.key == sym_scope_) {
            out.scope = parse_scope(field);
            if (out.scope == ScopeType::None) { diagnostics.push_back({"ai_plan requires valid scope", field.line}); ok = false; }
        } else if (field.key == sym_valid_) {
            if (!symbol_field(field, out.valid)) { diagnostics.push_back({"ai_plan valid requires script name", field.line}); ok = false; }
        } else if (field.key == sym_priority_) {
            if (!symbol_field(field, out.priority)) { diagnostics.push_back({"ai_plan priority requires scripted_value name", field.line}); ok = false; }
        } else if (field.key == sym_completion_) {
            if (!symbol_field(field, out.completion)) { diagnostics.push_back({"ai_plan completion requires script name", field.line}); ok = false; }
        } else if (field.key == sym_action_) {
            SymbolId action{};
            if (!symbol_field(field, action)) { diagnostics.push_back({"ai_plan action requires ai_action name", field.line}); ok = false; }
            else out.actions.push_back(action);
        } else if (field.key == sym_base_priority_) {
            if (field.kind != ScriptValueKind::Number || !std::isfinite(field.number)) {
                diagnostics.push_back({"base_priority requires finite number", field.line}); ok = false;
            } else out.base_priority = field.number;
        } else if (field.key == sym_commitment_ticks_) {
            if (!parse_u32(field, out.commitment_ticks)) { diagnostics.push_back({"commitment_ticks requires non-negative uint32", field.line}); ok = false; }
        }
    }
    if (out.scope == ScopeType::None) { diagnostics.push_back({"ai_plan requires scope", object.line}); ok = false; }
    if (out.actions.empty()) { diagnostics.push_back({"ai_plan requires at least one action", object.line}); ok = false; }
    return ok;
}

bool GameplayContentDatabase::ingest(const ScriptParseResult& parsed,
                                     std::vector<ScriptCompileDiagnostic>& diagnostics) {
    bool ok = parsed.ok();
    for (const auto& object : parsed.objects) {
        if (object.type == sym_event_ || object.type == sym_decision_ || object.type == sym_journal_) {
            GameplayDefinitionSpec spec;
            const auto kind = object.type == sym_event_ ? GameplayItemKind::Event :
                              (object.type == sym_decision_ ? GameplayItemKind::Decision : GameplayItemKind::Journal);
            if (!parse_gameplay_object(object, kind, spec, diagnostics)) { ok = false; continue; }
            const auto found = gameplay_lookup_.find(spec.key.value());
            if (found == gameplay_lookup_.end()) {
                const auto index = static_cast<std::uint32_t>(gameplay_.size());
                gameplay_lookup_.emplace(spec.key.value(), index);
                gameplay_.push_back(std::move(spec));
            } else gameplay_[found->second] = std::move(spec);
        } else if (object.type == sym_ai_action_) {
            AiActionSpec spec;
            if (!parse_ai_object(object, spec, diagnostics)) { ok = false; continue; }
            const auto found = ai_lookup_.find(spec.key.value());
            if (found == ai_lookup_.end()) {
                const auto index = static_cast<std::uint32_t>(ai_actions_.size());
                ai_lookup_.emplace(spec.key.value(), index);
                ai_actions_.push_back(std::move(spec));
            } else ai_actions_[found->second] = std::move(spec);
        } else if (object.type == sym_ai_plan_) {
            AiPlanSpec spec;
            if (!parse_ai_plan_object(object, spec, diagnostics)) { ok = false; continue; }
            const auto found = ai_plan_lookup_.find(spec.key.value());
            if (found == ai_plan_lookup_.end()) {
                const auto index = static_cast<std::uint32_t>(ai_plans_.size());
                ai_plan_lookup_.emplace(spec.key.value(), index);
                ai_plans_.push_back(std::move(spec));
            } else ai_plans_[found->second] = std::move(spec);
        }
    }
    return ok;
}

bool GameplayContentDatabase::bind(const ScriptProgramDatabase& programs,
                                   ScriptedGameplayRuntime& gameplay, UtilityAiEngine& ai,
                                   std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    bool ok = true;
    gameplay.clear_content();
    ai.clear_content();
    gameplay.set_program_database(&programs);
    ai.set_program_database(&programs);

    auto script = [&](SymbolId name, ScopeType scope, std::uint32_t line,
                      std::string_view role) -> std::optional<ScriptProgram> {
        if (!name.valid()) return std::nullopt;
        const auto* program = programs.find_script(name);
        if (program == nullptr) {
            diagnostics.push_back({std::string{role} + " references unknown script: " + std::string{symbols_.text(name)}, line});
            ok = false;
            return std::nullopt;
        }
        if (program->scope != scope) {
            diagnostics.push_back({std::string{role} + " script scope mismatch: " + std::string{symbols_.text(name)}, line});
            ok = false;
            return std::nullopt;
        }
        return *program;
    };

    for (const auto& spec : gameplay_) {
        const auto before = diagnostics.size();
        GameplayDefinition definition;
        definition.key = std::string{symbols_.text(spec.key)};
        definition.kind = spec.kind;
        definition.scope = spec.scope;
        definition.potential = script(spec.potential, spec.scope, spec.line, "potential");
        definition.allow = script(spec.allow, spec.scope, spec.line, "allow");
        definition.effect = script(spec.effect, spec.scope, spec.line, "effect");
        definition.completion = script(spec.completion, spec.scope, spec.line, "completion");
        definition.cooldown_ticks = spec.cooldown_ticks;
        definition.auto_trigger = spec.auto_trigger;
        for (const auto& option_spec : spec.options) {
            GameplayOptionDefinition option;
            option.key = std::string{symbols_.text(option_spec.key)};
            option.allow = script(option_spec.allow, spec.scope, option_spec.line, "option allow");
            option.effect = script(option_spec.effect, spec.scope, option_spec.line, "option effect");
            definition.options.push_back(std::move(option));
        }
        if (diagnostics.size() == before) (void)gameplay.add_definition(std::move(definition));
    }

    for (const auto& spec : ai_actions_) {
        const auto before = diagnostics.size();
        AiActionDefinition action;
        action.key = std::string{symbols_.text(spec.key)};
        action.scope = spec.scope;
        action.valid = script(spec.valid, spec.scope, spec.line, "ai valid");
        if (spec.utility.valid()) {
            const auto* value = programs.find_value(spec.utility);
            if (value == nullptr) {
                diagnostics.push_back({"ai utility references unknown scripted_value: " + std::string{symbols_.text(spec.utility)}, spec.line});
                ok = false;
            } else if (value->scope != spec.scope) {
                diagnostics.push_back({"ai utility scripted_value scope mismatch: " + std::string{symbols_.text(spec.utility)}, spec.line});
                ok = false;
            } else action.utility = *value;
        }
        action.effect = script(spec.effect, spec.scope, spec.line, "ai effect");
        action.base_utility = spec.base_utility;
        action.cooldown_ticks = spec.cooldown_ticks;
        if (diagnostics.size() == before) (void)ai.add_action(std::move(action));
    }
    for (const auto& spec : ai_plans_) {
        const auto before = diagnostics.size();
        AiPlanDefinition plan;
        plan.key = std::string{symbols_.text(spec.key)};
        plan.scope = spec.scope;
        plan.valid = script(spec.valid, spec.scope, spec.line, "ai plan valid");
        if (spec.priority.valid()) {
            const auto* value = programs.find_value(spec.priority);
            if (value == nullptr) {
                diagnostics.push_back({"ai plan priority references unknown scripted_value: " + std::string{symbols_.text(spec.priority)}, spec.line});
                ok = false;
            } else if (value->scope != spec.scope) {
                diagnostics.push_back({"ai plan priority scripted_value scope mismatch: " + std::string{symbols_.text(spec.priority)}, spec.line});
                ok = false;
            } else plan.priority = *value;
        }
        plan.completion = script(spec.completion, spec.scope, spec.line, "ai plan completion");
        for (const auto action : spec.actions) plan.action_keys.emplace_back(symbols_.text(action));
        plan.base_priority = spec.base_priority;
        plan.commitment_ticks = spec.commitment_ticks;
        if (diagnostics.size() == before) {
            try { (void)ai.add_plan(std::move(plan)); }
            catch (const std::exception& e) { diagnostics.push_back({e.what(), spec.line}); ok = false; }
        }
    }

    return ok && diagnostics.empty();
}

std::size_t GameplayContentDatabase::memory_bytes() const noexcept {
    std::size_t bytes = gameplay_.capacity() * sizeof(GameplayDefinitionSpec) +
                        ai_actions_.capacity() * sizeof(AiActionSpec) +
                        ai_plans_.capacity() * sizeof(AiPlanSpec);
    for (const auto& item : gameplay_) bytes += item.options.capacity() * sizeof(GameplayOptionSpec);
    for (const auto& plan : ai_plans_) bytes += plan.actions.capacity() * sizeof(SymbolId);
    bytes += gameplay_lookup_.size() * (sizeof(std::uint32_t) * 2u);
    bytes += ai_lookup_.size() * (sizeof(std::uint32_t) * 2u);
    return bytes;
}

} // namespace thunder
