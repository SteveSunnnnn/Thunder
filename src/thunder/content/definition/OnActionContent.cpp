#include "thunder/content/definition/OnActionContent.hpp"

#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace thunder {
namespace {

bool read_symbol(const ScriptNode& node, SymbolId& output) noexcept {
    if (node.kind != ScriptValueKind::Symbol) return false;
    output = node.symbol;
    return true;
}

} // namespace

OnActionContentDatabase::OnActionContentDatabase(SymbolTable& symbols) : symbols_(symbols) {
    sym_on_action_ = symbols_.intern("on_action");
    sym_scope_ = symbols_.intern("scope");
    sym_action_ = symbols_.intern("action");
    sym_script_ = symbols_.intern("script");
    sym_event_ = symbols_.intern("event");
    lookup_.reserve(256u);
}

ScopeType OnActionContentDatabase::parse_scope(const ScriptNode& node) const noexcept {
    if (node.kind != ScriptValueKind::Symbol) return ScopeType::None;
    const auto text = symbols_.text(node.symbol);
    if (text == "country") return ScopeType::Country;
    if (text == "state") return ScopeType::State;
    if (text == "province") return ScopeType::Province;
    if (text == "pop") return ScopeType::Pop;
    if (text == "market") return ScopeType::Market;
    return ScopeType::None;
}

bool OnActionContentDatabase::parse_definition(
    const ScriptObject& object, OnActionDefinitionSpec& output,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    output.key = object.name;
    output.line = object.line;
    bool ok = true;
    for (const auto& field : object.fields) {
        if (field.key == sym_scope_) {
            output.scope = parse_scope(field);
            if (output.scope == ScopeType::None) {
                diagnostics.push_back(
                    {"on_action scope requires country/state/province/pop/market", field.line});
                ok = false;
            }
            continue;
        }
        if (field.key != sym_action_) continue;
        if (field.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"on_action action requires a block", field.line});
            ok = false;
            continue;
        }

        OnActionStepSpec step;
        step.line = field.line;
        std::uint32_t target_fields = 0u;
        for (const auto& child : field.children) {
            if (child.key != sym_script_ && child.key != sym_event_) continue;
            ++target_fields;
            if (!read_symbol(child, step.target)) {
                diagnostics.push_back(
                    {child.key == sym_script_
                         ? "on_action script action requires a script name"
                         : "on_action event action requires an event key",
                     child.line});
                ok = false;
                continue;
            }
            step.kind = child.key == sym_script_ ? OnActionStepSpecKind::Script
                                                 : OnActionStepSpecKind::Event;
            step.line = child.line;
        }
        if (target_fields != 1u || !step.target.valid()) {
            diagnostics.push_back(
                {"on_action action requires exactly one script or event target", field.line});
            ok = false;
            continue;
        }
        output.steps.push_back(step);
    }

    if (output.scope == ScopeType::None) {
        diagnostics.push_back({"on_action requires scope", object.line});
        ok = false;
    }
    if (output.steps.empty()) {
        diagnostics.push_back({"on_action requires at least one action", object.line});
        ok = false;
    }
    return ok;
}

bool OnActionContentDatabase::ingest(
    const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    bool ok = parsed.ok();
    for (const auto& object : parsed.objects) {
        if (object.type != sym_on_action_) continue;
        OnActionDefinitionSpec spec;
        if (!parse_definition(object, spec, diagnostics)) {
            ok = false;
            continue;
        }
        const auto found = lookup_.find(spec.key.value());
        if (found == lookup_.end()) {
            const auto index = static_cast<std::uint32_t>(definitions_.size());
            lookup_.emplace(spec.key.value(), index);
            definitions_.push_back(std::move(spec));
        } else {
            definitions_[found->second] = std::move(spec);
        }
    }
    return ok;
}

bool OnActionContentDatabase::bind(
    const ScriptProgramDatabase& programs, const ScriptedGameplayRuntime& gameplay,
    OnActionRuntime& runtime, std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    runtime.clear_content();
    runtime.set_program_database(&programs);
    bool ok = true;

    for (const auto& spec : definitions_) {
        OnActionDefinition definition;
        definition.key = std::string{symbols_.text(spec.key)};
        definition.scope = spec.scope;
        definition.steps.reserve(spec.steps.size());
        bool definition_ok = true;
        for (const auto& step_spec : spec.steps) {
            OnActionStepDefinition step;
            step.target_key = std::string{symbols_.text(step_spec.target)};
            if (step_spec.kind == OnActionStepSpecKind::Script) {
                step.kind = OnActionStepKind::Script;
                const auto* program = programs.find_script(step_spec.target);
                if (program == nullptr) {
                    diagnostics.push_back({"on_action references unknown script: " +
                                           step.target_key, step_spec.line});
                    definition_ok = false;
                } else if (program->scope != spec.scope) {
                    diagnostics.push_back({"on_action script scope mismatch: " +
                                           step.target_key, step_spec.line});
                    definition_ok = false;
                } else {
                    step.script = *program;
                }
            } else {
                step.kind = OnActionStepKind::Event;
                for (std::uint32_t index = 0u; index < gameplay.definitions().size(); ++index) {
                    if (gameplay.definitions()[index].key == step.target_key) {
                        step.gameplay_definition = index;
                        break;
                    }
                }
                if (step.gameplay_definition ==
                    OnActionStepDefinition::invalid_gameplay_definition) {
                    diagnostics.push_back({"on_action references unknown event: " +
                                           step.target_key, step_spec.line});
                    definition_ok = false;
                } else {
                    const auto& event = gameplay.definitions()[step.gameplay_definition];
                    if (event.kind != GameplayItemKind::Event) {
                        diagnostics.push_back({"on_action target is not an event: " +
                                               step.target_key, step_spec.line});
                        definition_ok = false;
                    } else if (event.scope != spec.scope) {
                        diagnostics.push_back({"on_action event scope mismatch: " +
                                               step.target_key, step_spec.line});
                        definition_ok = false;
                    }
                }
            }
            definition.steps.push_back(std::move(step));
        }

        if (!definition_ok) {
            ok = false;
            continue;
        }
        try {
            (void)runtime.add_definition(std::move(definition));
        } catch (const std::exception& error) {
            diagnostics.push_back({error.what(), spec.line});
            ok = false;
        }
    }
    return ok;
}

std::size_t OnActionContentDatabase::memory_bytes() const noexcept {
    std::size_t bytes = definitions_.capacity() * sizeof(OnActionDefinitionSpec);
    for (const auto& definition : definitions_)
        bytes += definition.steps.capacity() * sizeof(OnActionStepSpec);
    bytes += lookup_.size() * (sizeof(std::uint32_t) * 2u);
    return bytes;
}

} // namespace thunder
