#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/foundation/base/Hash.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace thunder {
namespace {


std::string ascii_lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

ScopeType parse_scope_text(std::string_view text) noexcept {
    const auto lower = ascii_lower(text);
    if (lower == "country" || lower == "countries") return ScopeType::Country;
    if (lower == "state" || lower == "states") return ScopeType::State;
    if (lower == "province" || lower == "provinces") return ScopeType::Province;
    if (lower == "pop" || lower == "pops") return ScopeType::Pop;
    if (lower == "market" || lower == "markets") return ScopeType::Market;
    if (lower == "building" || lower == "buildings") return ScopeType::Building;
    if (lower == "army" || lower == "armies") return ScopeType::Army;
    if (lower == "front" || lower == "fronts") return ScopeType::Front;
    if (lower == "war" || lower == "wars") return ScopeType::War;
    return ScopeType::None;
}


} // namespace

ScriptCompiler::ScriptCompiler(SymbolTable& symbols, const ScriptRegistry& registry)
    : symbols_(symbols), registry_(registry) {
    sym_script_ = symbols_.intern("script");
    sym_scripted_value_ = symbols_.intern("scripted_value");
    sym_history_ = symbols_.intern("history");
    sym_scope_ = symbols_.intern("scope");
    sym_trigger_ = symbols_.intern("trigger");
    sym_effect_ = symbols_.intern("effect");
    sym_source_ = symbols_.intern("source");
    sym_multiply_ = symbols_.intern("multiply");
    sym_add_ = symbols_.intern("add");
    sym_expression_ = symbols_.intern("expression");
    sym_date_ = symbols_.intern("date");
    sym_all_ = symbols_.intern("all");
    sym_any_ = symbols_.intern("any");
    sym_not_ = symbols_.intern("not");
    sym_save_scope_as_ = symbols_.intern("save_scope_as");
    sym_scripted_trigger_ = symbols_.intern("scripted_trigger");
    sym_scripted_effect_ = symbols_.intern("scripted_effect");
    sym_name_ = symbols_.intern("name");
    sym_value_ = symbols_.intern("value");
    sym_parameters_ = symbols_.intern("parameters");
    sym_type_ = symbols_.intern("type");
    sym_required_ = symbols_.intern("required");
    sym_default_ = symbols_.intern("default");
}

bool ScriptCompiler::track_stable_name(
    std::string_view name, std::uint32_t line,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    const auto key = script_stable_key(name);
    const auto [found, inserted] = stable_names_.try_emplace(key, name);
    if (inserted || found->second == name) return true;
    diagnostics.push_back({"ThunderScript stable-key collision: " + found->second +
                           " / " + std::string{name}, line});
    return false;
}

ScopeType ScriptCompiler::parse_scope(const ScriptNode& node) const noexcept {
    if (node.kind != ScriptValueKind::Symbol) return ScopeType::None;
    return parse_scope_text(symbols_.text(node.symbol));
}

bool ScriptCompiler::parse_scope_selector(std::string_view text, ScopeSelector& selector,
                                          CompileScope current, ScopeType root_scope,
                                          CompileScope& nested) const {
    const auto lower = ascii_lower(text);
    if (lower == "this") { selector.kind = ScopeSelectorKind::This; nested = current; return true; }
    if (lower == "root") { selector.kind = ScopeSelectorKind::Root; nested = {root_scope, true}; return true; }
    if (lower == "from") { selector.kind = ScopeSelectorKind::From; nested = {}; return true; }
    if (lower == "prev") { selector.kind = ScopeSelectorKind::Prev; nested = {}; return true; }
    if (lower == "owner") { selector.kind = ScopeSelectorKind::Owner; nested = {ScopeType::Country, true}; return true; }
    if (lower == "country") { selector.kind = ScopeSelectorKind::Country; nested = {ScopeType::Country, true}; return true; }
    if (lower == "market") { selector.kind = ScopeSelectorKind::Market; nested = {ScopeType::Market, true}; return true; }
    if (lower == "state") { selector.kind = ScopeSelectorKind::State; nested = {ScopeType::State, true}; return true; }
    if (lower == "province") { selector.kind = ScopeSelectorKind::Province; nested = {ScopeType::Province, true}; return true; }
    constexpr std::string_view prefix = "saved:";
    if (lower.starts_with(prefix) && text.size() > prefix.size()) {
        selector.kind = ScopeSelectorKind::Saved;
        selector.saved_name = script_stable_key(text.substr(prefix.size()));
        nested = {};
        return true;
    }
    constexpr std::string_view event_prefix = "event_target:";
    if (lower.starts_with(event_prefix) && text.size() > event_prefix.size()) {
        selector.kind = ScopeSelectorKind::Saved;
        selector.saved_name = script_stable_key(text.substr(event_prefix.size()));
        nested = {};
        return true;
    }
    return false;
}

bool ScriptCompiler::parse_iterator(std::string_view text, ScopeIteratorMode& mode,
                                    ScopeType& target) const noexcept {
    const auto lower = ascii_lower(text);
    std::string_view suffix;
    if (lower.starts_with("any_")) {
        mode = ScopeIteratorMode::Any;
        suffix = std::string_view{lower}.substr(4u);
    } else if (lower.starts_with("every_")) {
        mode = ScopeIteratorMode::Every;
        suffix = std::string_view{lower}.substr(6u);
    } else if (lower.starts_with("random_")) {
        mode = ScopeIteratorMode::Random;
        suffix = std::string_view{lower}.substr(7u);
    } else if (lower.starts_with("ordered_")) {
        mode = ScopeIteratorMode::Ordered;
        suffix = std::string_view{lower}.substr(8u);
    } else {
        return false;
    }
    target = parse_scope_text(suffix);
    return target != ScopeType::None;
}

bool ScriptCompiler::parse_collection_iterator(std::string_view text, ScopeIteratorMode& mode,
                                               ScriptStableKey& collection) const noexcept {
    const auto lower = ascii_lower(text);
    std::size_t prefix_size = 0u;
    if (lower.starts_with("any_in:")) {
        mode = ScopeIteratorMode::Any;
        prefix_size = 7u;
    } else if (lower.starts_with("every_in:")) {
        mode = ScopeIteratorMode::Every;
        prefix_size = 9u;
    } else if (lower.starts_with("random_in:")) {
        mode = ScopeIteratorMode::Random;
        prefix_size = 10u;
    } else {
        return false;
    }
    if (text.size() <= prefix_size) return false;
    collection = script_stable_key(text.substr(prefix_size));
    return collection != 0;
}

ScopeType ScriptCompiler::value_source_scope(ValueSource source) const noexcept {
    switch (source) {
        case ValueSource::Population:
        case ValueSource::Gdp:
        case ValueSource::Treasury:
        case ValueSource::TaxRate: return ScopeType::Country;
        case ValueSource::PopulationSize:
        case ValueSource::Employment:
        case ValueSource::StandardOfLiving:
        case ValueSource::Literacy:
        case ValueSource::Qualification:
        case ValueSource::Wealth:
        case ValueSource::PoliticalStrength: return ScopeType::Pop;
        case ValueSource::MarketSupply:
        case ValueSource::MarketDemand: return ScopeType::Market;
        case ValueSource::StatePopulation: return ScopeType::State;
        case ValueSource::ProvincePopulation: return ScopeType::Province;
        case ValueSource::BuildingLevel:
        case ValueSource::BuildingEmployees:
        case ValueSource::BuildingProfit:
        case ValueSource::BuildingCash: return ScopeType::Building;
        case ValueSource::ArmyManpower:
        case ValueSource::ArmyOrganization: return ScopeType::Army;
        case ValueSource::FrontProgress: return ScopeType::Front;
        case ValueSource::WarScore:
        case ValueSource::WarWeeks: return ScopeType::War;
        case ValueSource::RuntimeArgument:
        case ValueSource::Constant:
        case ValueSource::VariableRef:
        case ValueSource::ScriptedValueRef: return ScopeType::None;
    }
    return ScopeType::None;
}



bool ScriptCompiler::compile_argument(const ScriptNode& node, CompiledScriptArgument& out,
                                      std::vector<ScriptCompileDiagnostic>& diagnostics,
                                      std::string_view description) const {
    if (node.kind == ScriptValueKind::Number) {
        out.source = ScriptArgumentSourceKind::Literal;
        out.literal = ScriptArgument::numeric(node.number);
        return true;
    }
    if (node.kind != ScriptValueKind::Symbol) {
        diagnostics.push_back({std::string{description} + " must be numeric, symbolic or a runtime reference",
                               node.line});
        return false;
    }

    const auto text = symbols_.text(node.symbol);
    const auto lower = ascii_lower(text);
    if (lower == "yes" || lower == "true") {
        out.source = ScriptArgumentSourceKind::Literal;
        out.literal = ScriptArgument::boolean(true);
        return true;
    }
    if (lower == "no" || lower == "false") {
        out.source = ScriptArgumentSourceKind::Literal;
        out.literal = ScriptArgument::boolean(false);
        return true;
    }
    if (lower == "this") { out.source = ScriptArgumentSourceKind::ThisScope; return true; }
    if (lower == "root") { out.source = ScriptArgumentSourceKind::RootScope; return true; }
    if (lower == "from") { out.source = ScriptArgumentSourceKind::FromScope; return true; }
    if (lower == "prev") { out.source = ScriptArgumentSourceKind::PrevScope; return true; }

    const auto bind_key = [&](std::string_view prefix, ScriptArgumentSourceKind source) {
        if (!lower.starts_with(prefix) || text.size() <= prefix.size()) return false;
        out.source = source;
        const auto name = text.substr(prefix.size());
        out.key = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    };
    if (bind_key("var:", ScriptArgumentSourceKind::Variable) ||
        bind_key("variable:", ScriptArgumentSourceKind::Variable) ||
        bind_key("arg:", ScriptArgumentSourceKind::Parameter) ||
        bind_key("parameter:", ScriptArgumentSourceKind::Parameter) ||
        bind_key("event_target:", ScriptArgumentSourceKind::EventTarget) ||
        bind_key("saved:", ScriptArgumentSourceKind::EventTarget)) {
        return true;
    }

    constexpr std::string_view value_prefix = "value:";
    constexpr std::string_view scripted_value_prefix = "scripted_value:";
    std::string_view called_name;
    if (lower.starts_with(scripted_value_prefix) && text.size() > scripted_value_prefix.size())
        called_name = text.substr(scripted_value_prefix.size());
    else if (lower.starts_with(value_prefix) && text.size() > value_prefix.size())
        called_name = text.substr(value_prefix.size());
    if (!called_name.empty()) {
        out.source = ScriptArgumentSourceKind::ScriptedValue;
        out.script_name = symbols_.intern(called_name);
        return true;
    }

    out.source = ScriptArgumentSourceKind::Literal;
    out.literal = ScriptArgument::symbol(script_symbol_hash(text));
    return true;
}

bool ScriptCompiler::compile_script_call(const ScriptNode& node, SymbolId& script_name,
                                         std::vector<CompiledNamedArgument>& arguments,
                                         std::vector<ScriptCompileDiagnostic>& diagnostics,
                                         std::string_view description) const {
    if (node.kind == ScriptValueKind::Symbol) {
        script_name = node.symbol;
        return true;
    }
    if (node.kind != ScriptValueKind::Block) {
        diagnostics.push_back({std::string{description} + " requires a script name or call block", node.line});
        return false;
    }

    bool ok = true;
        const auto compile_parameter = [&](const ScriptNode& parameter) {
        const auto name_text = symbols_.text(parameter.key);
        const auto key = script_stable_key(name_text);
        (void)track_stable_name(name_text, parameter.line, diagnostics);
        if (std::any_of(arguments.begin(), arguments.end(),
                        [key](const CompiledNamedArgument& existing) { return existing.name == key; })) {
            diagnostics.push_back({"duplicate scripted-call argument: " + std::string{name_text}, parameter.line});
            ok = false;
            return;
        }
        CompiledNamedArgument argument;
        argument.name = key;
        if (!compile_argument(parameter, argument.value, diagnostics, "scripted-call argument")) {
            ok = false;
            return;
        }
        arguments.push_back(std::move(argument));
    };

    for (const auto& field : node.children) {
        if (field.key == sym_name_ || field.key == sym_script_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({std::string{description} + " name must be symbolic", field.line});
                ok = false;
            } else if (script_name.valid()) {
                diagnostics.push_back({std::string{description} + " contains duplicate script name", field.line});
                ok = false;
            } else {
                script_name = field.symbol;
            }
        } else if (field.key == sym_parameters_) {
            if (field.kind != ScriptValueKind::Block) {
                diagnostics.push_back({std::string{description} + " parameters must be a block", field.line});
                ok = false;
            } else {
                for (const auto& parameter : field.children) compile_parameter(parameter);
            }
        } else {
            compile_parameter(field);
        }
    }
    if (!script_name.valid()) {
        diagnostics.push_back({std::string{description} + " call block requires name", node.line});
        ok = false;
    }
    std::sort(arguments.begin(), arguments.end(),
              [](const CompiledNamedArgument& a, const CompiledNamedArgument& b) { return a.name < b.name; });
    return ok;
}

bool ScriptCompiler::compile_parameters(
    const ScriptNode& node, std::vector<ScriptParameterDefinition>& parameters,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (node.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"script parameters must be a block", node.line});
        return false;
    }
    bool ok = true;
    for (const auto& entry : node.children) {
        ScriptParameterDefinition parameter;
        parameter.name = std::string{symbols_.text(entry.key)};
        parameter.key = script_stable_key(parameter.name);
        (void)track_stable_name(parameter.name, entry.line, diagnostics);
        parameter.source_line = entry.line;
        const ScriptNode* type_node = nullptr;
        const ScriptNode* required_node = nullptr;
        const ScriptNode* default_node = nullptr;
        if (entry.kind == ScriptValueKind::Symbol) {
            type_node = &entry;
        } else if (entry.kind == ScriptValueKind::Block) {
            type_node = entry.find(sym_type_);
            required_node = entry.find(sym_required_);
            default_node = entry.find(sym_default_);
        } else {
            diagnostics.push_back({"script parameter requires a type or declaration block", entry.line});
            ok = false;
            continue;
        }
        if (type_node == nullptr || type_node->kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"script parameter declaration requires symbolic type", entry.line});
            ok = false;
            continue;
        }
        const auto type = ascii_lower(symbols_.text(type_node->symbol));
        if (type == "number") parameter.kind = ScriptArgumentKind::Number;
        else if (type == "symbol" || type == "key") parameter.kind = ScriptArgumentKind::SymbolHash;
        else if (type == "boolean" || type == "bool") parameter.kind = ScriptArgumentKind::Boolean;
        else if (type == "scope") parameter.kind = ScriptArgumentKind::Scope;
        else {
            parameter.scope = parse_scope_text(type);
            if (parameter.scope != ScopeType::None) parameter.kind = ScriptArgumentKind::Scope;
            else {
                diagnostics.push_back({"unknown script parameter type: " + type, type_node->line});
                ok = false;
                continue;
            }
        }
        if (required_node != nullptr) {
            if (required_node->kind == ScriptValueKind::Number &&
                (required_node->number == 0.0 || required_node->number == 1.0)) {
                parameter.required = required_node->number != 0.0;
            } else if (required_node->kind == ScriptValueKind::Symbol) {
                const auto required = ascii_lower(symbols_.text(required_node->symbol));
                if (required == "yes" || required == "true" || required == "on") parameter.required = true;
                else if (required == "no" || required == "false" || required == "off") parameter.required = false;
                else {
                    diagnostics.push_back({"script parameter required expects yes/no", required_node->line});
                    ok = false;
                }
            } else {
                diagnostics.push_back({"script parameter required expects yes/no", required_node->line});
                ok = false;
            }
        }
        if (default_node != nullptr) {
            CompiledScriptArgument compiled_default;
            if (!compile_argument(*default_node, compiled_default, diagnostics,
                                  "script parameter default") ||
                compiled_default.source != ScriptArgumentSourceKind::Literal) {
                diagnostics.push_back({"script parameter default must be a literal", default_node->line});
                ok = false;
            } else if (compiled_default.literal.kind != parameter.kind ||
                       parameter.kind == ScriptArgumentKind::Scope) {
                diagnostics.push_back({"script parameter default type mismatch", default_node->line});
                ok = false;
            } else {
                parameter.default_value = compiled_default.literal;
                parameter.required = false;
            }
        }
        const auto duplicate = std::find_if(parameters.begin(), parameters.end(),
            [&parameter](const ScriptParameterDefinition& existing) {
                return existing.key == parameter.key;
            });
        if (duplicate != parameters.end()) {
            diagnostics.push_back({duplicate->name == parameter.name
                ? "duplicate script parameter: " + parameter.name
                : "script parameter stable-key collision: " + duplicate->name + " / " + parameter.name,
                entry.line});
            ok = false;
            continue;
        }
        parameters.push_back(std::move(parameter));
    }
    std::sort(parameters.begin(), parameters.end(),
        [](const ScriptParameterDefinition& first, const ScriptParameterDefinition& second) {
            return first.key < second.key;
        });
    return ok;
}

bool ScriptCompiler::is_advanced_condition(const ScriptNode& node) const {
    const auto text = symbols_.text(node.key);
    if (node.kind == ScriptValueKind::Symbol) return true;
    if (node.kind == ScriptValueKind::Expression) return true; // infix comparison
    const auto lower = ascii_lower(text);
    ScopeSelector selector;
    CompileScope nested;
    ScopeIteratorMode mode;
    ScopeType target = ScopeType::None;
    ScriptStableKey collection = 0;
    if (lower == "scripted_trigger") return true;
    if (parse_scope_selector(text, selector, {}, ScopeType::None, nested)) return true;
    if (parse_iterator(text, mode, target)) return true;
    if (parse_collection_iterator(text, mode, collection)) return true;
    if (node.kind == ScriptValueKind::Block) {
        for (const auto& child : node.children) if (is_advanced_condition(child)) return true;
    }
    return false;
}

bool ScriptCompiler::is_advanced_effect(const ScriptNode& node) const {
    const auto text = symbols_.text(node.key);
    if (node.kind == ScriptValueKind::Symbol) return true;
    const auto lower = ascii_lower(text);
    ScopeSelector selector;
    CompileScope nested;
    ScopeIteratorMode mode;
    ScopeType target = ScopeType::None;
    ScriptStableKey collection = 0;
    if (lower == "save_scope_as" || lower == "scripted_effect" || lower == "if" ||
        lower == "else" || lower == "else_if" || lower == "while" || lower == "random_list") return true;
    if (parse_scope_selector(text, selector, {}, ScopeType::None, nested)) return true;
    if (parse_iterator(text, mode, target)) return true;
    if (parse_collection_iterator(text, mode, collection)) return true;
    if (node.kind == ScriptValueKind::Block) {
        for (const auto& child : node.children) if (is_advanced_effect(child)) return true;
    }
    return false;
}



bool ScriptCompiler::compile_condition_node(const ScriptNode& node, ScopeType scope,
                                            std::vector<ConditionInstruction>& code,
                                            std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (node.key == sym_all_ || node.key == sym_any_ || node.key == sym_not_) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"boolean condition group must be a block", node.line});
            return false;
        }
        if (node.key == sym_not_) {
            const bool ok = compile_condition_block(node, scope, ConditionOp::And, code, diagnostics);
            if (ok) code.push_back({ConditionOp::Not, 0u, {}, 0.0});
            return ok;
        }
        return compile_condition_block(node, scope, node.key == sym_all_ ? ConditionOp::And : ConditionOp::Or,
                                       code, diagnostics);
    }

    if (node.kind != ScriptValueKind::Number) {
        diagnostics.push_back({"trigger primitive argument must be numeric", node.line});
        return false;
    }
    const auto name = symbols_.text(node.key);
    const auto id = registry_.find_trigger(name);
    if (!id.valid()) {
        diagnostics.push_back({"unknown trigger: " + std::string{name}, node.line});
        return false;
    }
    if (registry_.trigger_scope(id) != scope) {
        diagnostics.push_back({"trigger scope mismatch: " + std::string{name}, node.line});
        return false;
    }
    code.push_back({ConditionOp::CallTrigger, 0u, id, node.number});
    return true;
}

bool ScriptCompiler::compile_condition_block(const ScriptNode& block, ScopeType scope, ConditionOp combine,
                                             std::vector<ConditionInstruction>& code,
                                             std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"trigger must be a block", block.line});
        return false;
    }
    if (block.children.empty()) {
        code.push_back({ConditionOp::PushTrue, 0u, {}, 0.0});
        return true;
    }
    bool ok = true;
    std::size_t compiled_children = 0;
    for (const auto& child : block.children) {
        const auto before = code.size();
        const bool child_ok = compile_condition_node(child, scope, code, diagnostics);
        ok &= child_ok;
        if (!child_ok) {
            code.resize(before);
            continue;
        }
        ++compiled_children;
        if (compiled_children > 1u) code.push_back({combine, 0u, {}, 0.0});
    }
    if (compiled_children == 0u) code.push_back({ConditionOp::PushTrue, 0u, {}, 0.0});
    return ok;
}

bool ScriptCompiler::compile_fast_all(const ScriptNode& block, ScopeType scope,
                                      std::vector<CompiledTriggerCall>& calls,
                                      std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"trigger must be a block", block.line});
        return false;
    }
    bool ok = true;
    calls.reserve(block.children.size());
    for (const auto& node : block.children) {
        if (node.key == sym_all_ || node.key == sym_any_ || node.key == sym_not_ || node.kind != ScriptValueKind::Number) {
            diagnostics.push_back({"internal fast-condition classification mismatch", node.line});
            return false;
        }
        const auto name = symbols_.text(node.key);
        const auto id = registry_.find_trigger(name);
        if (!id.valid()) { diagnostics.push_back({"unknown trigger: " + std::string{name}, node.line}); ok = false; continue; }
        if (registry_.trigger_scope(id) != scope) { diagnostics.push_back({"trigger scope mismatch: " + std::string{name}, node.line}); ok = false; continue; }
        calls.push_back({id, node.number});
    }
    return ok;
}

bool ScriptCompiler::compile_effects(const ScriptNode& block, ScopeType scope,
                                     std::vector<CompiledEffectCall>& effects,
                                     std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"effect must be a block", block.line});
        return false;
    }
    bool ok = true;
    effects.reserve(effects.size() + block.children.size());
    for (const auto& call : block.children) {
        if (call.kind != ScriptValueKind::Number) {
            diagnostics.push_back({"effect primitive argument must be numeric", call.line});
            ok = false;
            continue;
        }
        const auto name = symbols_.text(call.key);
        const auto id = registry_.find_effect(name);
        if (!id.valid()) { diagnostics.push_back({"unknown effect: " + std::string{name}, call.line}); ok = false; continue; }
        if (registry_.effect_scope(id) != scope) { diagnostics.push_back({"effect scope mismatch: " + std::string{name}, call.line}); ok = false; continue; }
        effects.push_back({id, call.number});
    }
    return ok;
}






} // namespace thunder
