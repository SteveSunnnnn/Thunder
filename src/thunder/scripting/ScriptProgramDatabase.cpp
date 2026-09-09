#include "thunder/scripting/ScriptProgram.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace thunder {
namespace {

std::size_t value_bytecode_bytes(const ScriptValueBytecode& bytecode) noexcept {
    return bytecode.ops.size() * sizeof(ScriptValueOp) +
           bytecode.const_pool.size() * sizeof(double) +
           bytecode.var_keys.size() * sizeof(ScriptStableKey) +
           bytecode.source_pool.size() * sizeof(ValueSource) +
           bytecode.value_refs.size() * sizeof(SymbolId);
}

std::size_t scoped_condition_bytes(const CompiledScopedCondition& node) noexcept {
    std::size_t bytes = sizeof(CompiledScopedCondition) +
                        node.arguments.size() * sizeof(CompiledNamedArgument) +
                        value_bytecode_bytes(node.compare_lhs) +
                        value_bytecode_bytes(node.compare_rhs);
    for (const auto& child : node.children) bytes += scoped_condition_bytes(child);
    for (const auto& filter : node.iterator_filter) bytes += scoped_condition_bytes(filter);
    return bytes;
}

std::size_t scoped_effect_bytes(const CompiledScopedEffect& node) noexcept {
    std::size_t bytes = sizeof(CompiledScopedEffect) +
                        node.arguments.size() * sizeof(CompiledNamedArgument) +
                        (node.has_iterator_condition ? scoped_condition_bytes(node.condition) : 0u) +
                        node.random_list_weights.size() * sizeof(std::uint32_t);
    for (const auto& child : node.children) bytes += scoped_effect_bytes(child);
    for (const auto& child : node.else_children) bytes += scoped_effect_bytes(child);
    return bytes;
}

} // namespace

void ScriptProgramDatabase::reserve(std::size_t scripts, std::size_t values, std::size_t history) {
    scripts_.reserve(scripts);
    values_.reserve(values);
    history_.reserve(history);
    script_lookup_.reserve(scripts * 2u + 1u);
    value_lookup_.reserve(values * 2u + 1u);
}

void ScriptProgramDatabase::add(ScriptProgram program) {
    if (const auto it = script_lookup_.find(program.name.value()); it != script_lookup_.end()) {
        scripts_[it->second] = std::move(program);
        return;
    }
    const auto index = static_cast<std::uint32_t>(scripts_.size());
    script_lookup_.emplace(program.name.value(), index);
    scripts_.push_back(std::move(program));
}

void ScriptProgramDatabase::add(ScriptedValueProgram program) {
    if (const auto it = value_lookup_.find(program.name.value()); it != value_lookup_.end()) {
        values_[it->second] = std::move(program);
        return;
    }
    const auto index = static_cast<std::uint32_t>(values_.size());
    value_lookup_.emplace(program.name.value(), index);
    values_.push_back(std::move(program));
}

void ScriptProgramDatabase::add(CompiledHistoryPatch patch) { history_.push_back(std::move(patch)); }

const ScriptProgram* ScriptProgramDatabase::find_script(SymbolId name) const noexcept {
    const auto it = script_lookup_.find(name.value());
    return it == script_lookup_.end() ? nullptr : &scripts_[it->second];
}

const ScriptedValueProgram* ScriptProgramDatabase::find_value(SymbolId name) const noexcept {
    const auto it = value_lookup_.find(name.value());
    return it == value_lookup_.end() ? nullptr : &values_[it->second];
}

bool ScriptProgramDatabase::validate_links(
    const SymbolTable& symbols, std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    struct StaticArgumentType {
        std::optional<ScriptArgumentKind> kind;
        ScopeType scope = ScopeType::None;
    };
    bool ok = true;
    std::vector<std::vector<std::uint32_t>> edges(scripts_.size());

    const auto program_index = [this](SymbolId name) -> std::optional<std::uint32_t> {
        const auto found = script_lookup_.find(name.value());
        if (found == script_lookup_.end()) return std::nullopt;
        return found->second;
    };
    const auto parameter_for = [](const auto& program, ScriptStableKey key)
        -> const ScriptParameterDefinition* {
        const auto found = std::lower_bound(program.parameters.begin(), program.parameters.end(), key,
            [](const ScriptParameterDefinition& parameter, ScriptStableKey wanted) {
                return parameter.key < wanted;
            });
        return found != program.parameters.end() && found->key == key ? &*found : nullptr;
    };
    const auto static_type = [&](const CompiledScriptArgument& argument,
                                 const ScriptProgram& caller,
                                 ScopeType call_scope,
                                 std::uint32_t line) -> StaticArgumentType {
        switch (argument.source) {
            case ScriptArgumentSourceKind::Literal:
                return {argument.literal.kind,
                        argument.literal.kind == ScriptArgumentKind::Scope
                            ? argument.literal.scope_value.type : ScopeType::None};
            case ScriptArgumentSourceKind::Parameter: {
                if (caller.parameters.empty()) return {};
                const auto* parameter = parameter_for(caller, argument.key);
                if (parameter == nullptr) {
                    diagnostics.push_back({"undeclared typed script parameter reference", line});
                    ok = false;
                    return {};
                }
                return {parameter->kind, parameter->scope};
            }
            case ScriptArgumentSourceKind::Variable:
                return {};
            case ScriptArgumentSourceKind::EventTarget:
            case ScriptArgumentSourceKind::FromScope:
            case ScriptArgumentSourceKind::PrevScope:
                return {ScriptArgumentKind::Scope, ScopeType::None};
            case ScriptArgumentSourceKind::ThisScope:
                return {ScriptArgumentKind::Scope, call_scope};
            case ScriptArgumentSourceKind::RootScope:
                return {ScriptArgumentKind::Scope, caller.scope};
            case ScriptArgumentSourceKind::ScriptedValue: {
                const auto* value = find_value(argument.script_name);
                if (value == nullptr) {
                    diagnostics.push_back({"script call references unknown scripted_value argument: " +
                                           std::string{symbols.text(argument.script_name)}, line});
                    ok = false;
                } else if (call_scope != ScopeType::None && value->scope != call_scope) {
                    diagnostics.push_back({"scripted_value argument scope mismatch: " +
                                           std::string{symbols.text(argument.script_name)}, line});
                    ok = false;
                } else if (!caller.parameters.empty()) {
                    for (const auto& value_parameter : value->parameters) {
                        const auto* caller_parameter = parameter_for(caller, value_parameter.key);
                        if (caller_parameter == nullptr) {
                            if (value_parameter.required && !value_parameter.default_value) {
                                diagnostics.push_back({"scripted_value argument is missing typed parameter: " +
                                                       value_parameter.name, line});
                                ok = false;
                            }
                            continue;
                        }
                        if (caller_parameter->kind != value_parameter.kind ||
                            (value_parameter.kind == ScriptArgumentKind::Scope &&
                             value_parameter.scope != ScopeType::None &&
                             caller_parameter->scope != ScopeType::None &&
                             caller_parameter->scope != value_parameter.scope)) {
                            diagnostics.push_back({"scripted_value typed parameter mismatch: " +
                                                   value_parameter.name, line});
                            ok = false;
                        }
                    }
                }
                return {ScriptArgumentKind::Number, ScopeType::None};
            }
        }
        return {};
    };

    const auto validate_call = [&](std::uint32_t caller_index, SymbolId target,
                                   ScopeType call_scope,
                                   std::span<const CompiledNamedArgument> arguments,
                                   std::uint32_t line) {
        const auto target_index = program_index(target);
        if (!target_index) {
            diagnostics.push_back({"script call references unknown script: " +
                                   std::string{symbols.text(target)}, line});
            ok = false;
            return;
        }
        const auto& caller = scripts_[caller_index];
        const auto& called = scripts_[*target_index];
        edges[caller_index].push_back(*target_index);
        if (call_scope != ScopeType::None && called.scope != call_scope) {
            diagnostics.push_back({"script call scope mismatch: " +
                                   std::string{symbols.text(target)}, line});
            ok = false;
        }
        if (called.parameters.empty()) return;
        std::unordered_set<ScriptStableKey> supplied;
        supplied.reserve(arguments.size());
        for (const auto& argument : arguments) {
            if (!supplied.insert(argument.name).second) {
                diagnostics.push_back({"duplicate named script call argument", line});
                ok = false;
                continue;
            }
            const auto* parameter = parameter_for(called, argument.name);
            if (parameter == nullptr) {
                diagnostics.push_back({"unknown named argument for typed script call", line});
                ok = false;
                continue;
            }
            const auto actual = static_type(argument.value, caller, call_scope, line);
            if (actual.kind && *actual.kind != parameter->kind) {
                diagnostics.push_back({"typed script call argument kind mismatch for " +
                                       parameter->name, line});
                ok = false;
            } else if (actual.kind == ScriptArgumentKind::Scope &&
                       parameter->scope != ScopeType::None &&
                       actual.scope != ScopeType::None && actual.scope != parameter->scope) {
                diagnostics.push_back({"typed script call scope argument mismatch for " +
                                       parameter->name, line});
                ok = false;
            }
        }
        for (const auto& parameter : called.parameters) {
            if (parameter.required && !parameter.default_value && !supplied.contains(parameter.key)) {
                diagnostics.push_back({"typed script call is missing required argument: " +
                                       parameter.name, line});
                ok = false;
            }
        }
    };

    for (std::uint32_t index = 0; index < scripts_.size(); ++index) {
        const auto visit_conditions = [&](auto&& self,
                                          std::span<const CompiledScopedCondition> nodes) -> void {
            for (const auto& node : nodes) {
                if (node.kind == ScopedConditionKind::ScriptCall)
                    validate_call(index, node.script_name, node.call_scope,
                                  node.arguments, node.source_line);
                if (node.kind == ScopedConditionKind::SourceCompare) {
                    for (const auto* bytecode : {&node.compare_lhs, &node.compare_rhs}) {
                        for (const auto ref : bytecode->value_refs) {
                            const auto found = value_lookup_.find(ref.value());
                            if (found == value_lookup_.end()) {
                                diagnostics.push_back({"condition expression references unknown scripted_value: " +
                                                       std::string{symbols.text(ref)}, node.source_line});
                                ok = false;
                                continue;
                            }
                            if (values_[found->second].scope != scripts_[index].scope) {
                                diagnostics.push_back({"condition expression scripted_value scope mismatch: " +
                                                       std::string{symbols.text(ref)}, node.source_line});
                                ok = false;
                            }
                        }
                    }
                }
                self(self, node.children);
            }
        };
        const auto visit_effects = [&](auto&& self,
                                       std::span<const CompiledScopedEffect> nodes) -> void {
            for (const auto& node : nodes) {
                if (node.kind == ScopedEffectKind::ScriptCall)
                    validate_call(index, node.script_name, node.call_scope,
                                  node.arguments, node.source_line);
                self(self, node.children);
            }
        };
        visit_conditions(visit_conditions, scripts_[index].scoped_conditions);
        visit_effects(visit_effects, scripts_[index].scoped_effects);
        std::sort(edges[index].begin(), edges[index].end());
        edges[index].erase(std::unique(edges[index].begin(), edges[index].end()), edges[index].end());
    }

    enum class Visit : std::uint8_t { New, Active, Done };
    std::vector<Visit> visits(scripts_.size(), Visit::New);
    const auto visit = [&](auto&& self, std::uint32_t node) -> bool {
        if (visits[node] == Visit::Active) {
            diagnostics.push_back({"cyclic scripted call graph contains: " +
                                   std::string{symbols.text(scripts_[node].name)}, 0u});
            return false;
        }
        if (visits[node] == Visit::Done) return true;
        visits[node] = Visit::Active;
        bool acyclic = true;
        for (const auto child : edges[node]) acyclic &= self(self, child);
        visits[node] = Visit::Done;
        return acyclic;
    };
    for (std::uint32_t index = 0; index < scripts_.size(); ++index) {
        if (!visit(visit, index)) ok = false;
    }

    std::vector<std::vector<std::uint32_t>> value_edges(values_.size());
    for (std::uint32_t index = 0; index < values_.size(); ++index) {
        const auto& value = values_[index];
        // Expression bytecode: every PushScriptedValue reference must resolve,
        // match the declaring scope, and join the cycle-detection graph.
        for (const auto ref : value.bytecode.value_refs) {
            const auto found = value_lookup_.find(ref.value());
            if (found == value_lookup_.end()) {
                diagnostics.push_back({"scripted_value expression references unknown scripted_value: " +
                                       std::string{symbols.text(ref)}, value.source_line});
                ok = false;
                continue;
            }
            const auto& target = values_[found->second];
            if (target.scope != value.scope) {
                diagnostics.push_back({"scripted_value expression scope mismatch: " +
                                       std::string{symbols.text(target.name)}, value.source_line});
                ok = false;
                continue;
            }
            value_edges[index].push_back(found->second);
        }
        if (value.source != ValueSource::RuntimeArgument) continue;
        if (value.runtime_source.source == ScriptArgumentSourceKind::Parameter &&
            !value.parameters.empty() &&
            parameter_for(value, value.runtime_source.key) == nullptr) {
            diagnostics.push_back({"scripted_value references undeclared typed parameter: " +
                                   std::string{symbols.text(value.name)}, value.source_line});
            ok = false;
        }
        if (value.runtime_source.source != ScriptArgumentSourceKind::ScriptedValue) continue;
        const auto found = value_lookup_.find(value.runtime_source.script_name.value());
        if (found == value_lookup_.end()) {
            diagnostics.push_back({"scripted_value references unknown scripted_value: " +
                                   std::string{symbols.text(value.runtime_source.script_name)},
                                   value.source_line});
            ok = false;
            continue;
        }
        const auto& target = values_[found->second];
        value_edges[index].push_back(found->second);
        if (target.scope != value.scope) {
            diagnostics.push_back({"nested scripted_value scope mismatch: " +
                                   std::string{symbols.text(target.name)}, value.source_line});
            ok = false;
        }
        if (!value.parameters.empty()) {
            for (const auto& target_parameter : target.parameters) {
                const auto* supplied = parameter_for(value, target_parameter.key);
                if (supplied == nullptr) {
                    if (target_parameter.required && !target_parameter.default_value) {
                        diagnostics.push_back({"nested scripted_value is missing typed parameter: " +
                                               target_parameter.name, value.source_line});
                        ok = false;
                    }
                } else if (supplied->kind != target_parameter.kind ||
                           (target_parameter.kind == ScriptArgumentKind::Scope &&
                            target_parameter.scope != ScopeType::None &&
                            supplied->scope != ScopeType::None &&
                            supplied->scope != target_parameter.scope)) {
                    diagnostics.push_back({"nested scripted_value typed parameter mismatch: " +
                                           target_parameter.name, value.source_line});
                    ok = false;
                }
            }
        }
    }
    std::vector<Visit> value_visits(values_.size(), Visit::New);
    const auto visit_value = [&](auto&& self, std::uint32_t node) -> bool {
        if (value_visits[node] == Visit::Active) {
            diagnostics.push_back({"cyclic scripted_value graph contains: " +
                                   std::string{symbols.text(values_[node].name)},
                                   values_[node].source_line});
            return false;
        }
        if (value_visits[node] == Visit::Done) return true;
        value_visits[node] = Visit::Active;
        bool acyclic = true;
        for (const auto child : value_edges[node]) acyclic &= self(self, child);
        value_visits[node] = Visit::Done;
        return acyclic;
    };
    for (std::uint32_t index = 0; index < values_.size(); ++index) {
        if (!visit_value(visit_value, index)) ok = false;
    }
    return ok;
}

std::size_t ScriptProgramDatabase::instruction_bytes() const noexcept {
    std::size_t result = values_.size() * sizeof(ScriptedValueProgram);
    for (const auto& value : values_) {
        result += value.parameters.size() * sizeof(ScriptParameterDefinition);
        for (const auto& parameter : value.parameters) result += parameter.name.capacity();
        result += value_bytecode_bytes(value.bytecode);
    }
    for (const auto& script : scripts_) {
        result += script.fast_all.size() * sizeof(CompiledTriggerCall);
        result += script.condition.size() * sizeof(ConditionInstruction);
        result += script.effects.size() * sizeof(CompiledEffectCall);
        result += script.parameters.size() * sizeof(ScriptParameterDefinition);
        for (const auto& parameter : script.parameters) result += parameter.name.capacity();
        for (const auto& node : script.scoped_conditions) result += scoped_condition_bytes(node);
        for (const auto& node : script.scoped_effects) result += scoped_effect_bytes(node);
    }
    for (const auto& h : history_) result += h.effects.size() * sizeof(CompiledEffectCall);
    return result;
}



} // namespace thunder
