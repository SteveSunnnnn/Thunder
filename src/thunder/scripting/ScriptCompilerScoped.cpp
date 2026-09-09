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

std::int32_t parse_yyyymmdd(std::string_view text) noexcept {
    if (text.size() != 10u || text[4] != '.' || text[7] != '.') return 0;
    auto digit = [](char c) -> int { return (c >= '0' && c <= '9') ? c - '0' : -1; };
    int y = 0, m = 0, d = 0;
    for (int i = 0; i < 4; ++i) { const int v = digit(text[static_cast<std::size_t>(i)]); if (v < 0) return 0; y = y * 10 + v; }
    for (int i = 5; i < 7; ++i) { const int v = digit(text[static_cast<std::size_t>(i)]); if (v < 0) return 0; m = m * 10 + v; }
    for (int i = 8; i < 10; ++i) { const int v = digit(text[static_cast<std::size_t>(i)]); if (v < 0) return 0; d = d * 10 + v; }
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    return y * 10000 + m * 100 + d;
}

std::string ascii_lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

[[nodiscard]] bool read_u32_count(const ScriptNode& node, std::uint32_t& out) noexcept {
    if (node.kind != ScriptValueKind::Number) return false;
    if (!std::isfinite(node.number)) return false;
    if (node.number < 0.0) return false;
    if (node.number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) return false;
    if (std::floor(node.number) != node.number) return false;
    out = static_cast<std::uint32_t>(node.number);
    return true;
}

// random_list branch weights come from the numeric branch key text (`10 = {...}`).
[[nodiscard]] bool parse_u32_weight(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty() || text.size() > 10u) return false;
    std::uint64_t value = 0u;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10u + static_cast<std::uint64_t>(c - '0');
    }
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

void assign_condition_callsites(std::vector<CompiledScopedCondition>& nodes,
                                ScriptStableKey program_key, std::uint64_t parent) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        Fnv1a64 hash;
        hash.add(program_key);
        hash.add(std::uint8_t{0x43u});
        hash.add(parent);
        hash.add(static_cast<std::uint64_t>(index));
        hash.add(static_cast<std::uint8_t>(node.kind));
        hash.add(static_cast<std::uint8_t>(node.iterator_source));
        hash.add(static_cast<std::uint8_t>(node.iterator_mode));
        hash.add(static_cast<std::uint8_t>(node.iterator_target));
        hash.add(node.collection_name);
        node.salt = hash.value();
        assign_condition_callsites(node.children, program_key, node.salt);
    }
}

void assign_effect_callsites(std::vector<CompiledScopedEffect>& nodes,
                             ScriptStableKey program_key, std::uint64_t parent) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        Fnv1a64 hash;
        hash.add(program_key);
        hash.add(std::uint8_t{0x45u});
        hash.add(parent);
        hash.add(static_cast<std::uint64_t>(index));
        hash.add(static_cast<std::uint8_t>(node.kind));
        hash.add(static_cast<std::uint8_t>(node.iterator_source));
        hash.add(static_cast<std::uint8_t>(node.iterator_mode));
        hash.add(static_cast<std::uint8_t>(node.iterator_target));
        hash.add(node.collection_name);
        node.salt = hash.value();
        assign_effect_callsites(node.children, program_key, node.salt);
    }
}

// Builtin per-scope value sources shared by `source =` and expression operands.
[[nodiscard]] std::optional<ValueSource> builtin_source_from_name(std::string_view name) noexcept {
    if (name == "population") return ValueSource::Population;
    if (name == "gdp") return ValueSource::Gdp;
    if (name == "treasury") return ValueSource::Treasury;
    if (name == "tax_rate") return ValueSource::TaxRate;
    if (name == "pop_size") return ValueSource::PopulationSize;
    if (name == "employment") return ValueSource::Employment;
    if (name == "standard_of_living") return ValueSource::StandardOfLiving;
    if (name == "literacy") return ValueSource::Literacy;
    if (name == "qualification") return ValueSource::Qualification;
    if (name == "wealth") return ValueSource::Wealth;
    if (name == "political_strength") return ValueSource::PoliticalStrength;
    if (name == "market_supply") return ValueSource::MarketSupply;
    if (name == "market_demand") return ValueSource::MarketDemand;
    if (name == "state_population") return ValueSource::StatePopulation;
    if (name == "province_population") return ValueSource::ProvincePopulation;
    if (name == "building_level") return ValueSource::BuildingLevel;
    if (name == "building_employees") return ValueSource::BuildingEmployees;
    if (name == "building_profit") return ValueSource::BuildingProfit;
    if (name == "building_cash") return ValueSource::BuildingCash;
    if (name == "army_manpower") return ValueSource::ArmyManpower;
    if (name == "army_organization") return ValueSource::ArmyOrganization;
    if (name == "front_progress") return ValueSource::FrontProgress;
    if (name == "war_score") return ValueSource::WarScore;
    if (name == "war_weeks") return ValueSource::WarWeeks;
    return std::nullopt;
}

[[nodiscard]] std::optional<ScriptComparison> comparison_from_op_text(std::string_view op) noexcept {
    if (op == ">") return ScriptComparison::Above;
    if (op == "<") return ScriptComparison::Below;
    if (op == ">=") return ScriptComparison::AtLeast;
    if (op == "<=") return ScriptComparison::AtMost;
    if (op == "!=") return ScriptComparison::NotEqual;
    return std::nullopt;
}
} // namespace

bool ScriptCompiler::compile_value_expression(const ScriptNode& node, CompileScope scope,
                                              ScriptValueBytecode& out,
                                              std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    switch (node.kind) {
    case ScriptValueKind::Number:
        out.ops.push_back(ScriptValueOp::PushConst);
        out.const_pool.push_back(node.number);
        return true;
    case ScriptValueKind::Symbol: {
        const auto text = symbols_.text(node.symbol);
        if (const auto source = builtin_source_from_name(text)) {
            if (scope.known && value_source_scope(*source) != scope.type) {
                diagnostics.push_back({"value source does not match scope: " + std::string{text}, node.line});
                return false;
            }
            out.ops.push_back(ScriptValueOp::PushSource);
            out.source_pool.push_back(*source);
            return true;
        }
        constexpr std::string_view value_prefix = "value:";
        if (text.starts_with(value_prefix) && text.size() > value_prefix.size()) {
            out.ops.push_back(ScriptValueOp::PushScriptedValue);
            out.value_refs.push_back(symbols_.find(text.substr(value_prefix.size())));
            return true;
        }
        constexpr std::string_view var_prefix = "var:";
        if (text.starts_with(var_prefix) && text.size() > var_prefix.size()) {
            out.ops.push_back(ScriptValueOp::PushVariable);
            out.var_keys.push_back(script_stable_key(text.substr(var_prefix.size())));
            return true;
        }
        diagnostics.push_back({"unknown value expression operand: " + std::string{text}, node.line});
        return false;
    }
    case ScriptValueKind::Expression: {
        const auto op = symbols_.text(node.key);
        const std::size_t operand_count = op == "neg" ? 1u : 2u;
        if (node.children.size() != operand_count) {
            diagnostics.push_back({"malformed value expression", node.line});
            return false;
        }
        for (const auto& operand : node.children) {
            if (!compile_value_expression(operand, scope, out, diagnostics)) return false;
        }
        if (op == "+") out.ops.push_back(ScriptValueOp::Add);
        else if (op == "-") out.ops.push_back(ScriptValueOp::Sub);
        else if (op == "*") out.ops.push_back(ScriptValueOp::Mul);
        else if (op == "/") out.ops.push_back(ScriptValueOp::Div);
        else if (op == "neg") out.ops.push_back(ScriptValueOp::Neg);
        else {
            diagnostics.push_back({"unknown value expression operator: " + std::string{op}, node.line});
            return false;
        }
        return true;
    }
    default:
        diagnostics.push_back({"expected a value expression", node.line});
        return false;
    }
}

bool ScriptCompiler::compile_scoped_condition_node(const ScriptNode& node, CompileScope scope,
                                                   ScopeType root_scope, CompiledScopedCondition& out,
                                                   std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    const auto text = symbols_.text(node.key);
    const auto lower = ascii_lower(text);
    out.source_line = node.line;

    if (lower == "all" || lower == "any" || lower == "not") {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"boolean condition group must be a block", node.line});
            return false;
        }
        out.kind = lower == "all" ? ScopedConditionKind::All : (lower == "any" ? ScopedConditionKind::Any : ScopedConditionKind::Not);
        return compile_scoped_condition_block(node, scope, root_scope, out.children, diagnostics);
    }

    if (lower == "scripted_trigger") {
        out.kind = ScopedConditionKind::ScriptCall;
        out.call_scope = scope.known ? scope.type : ScopeType::None;
        return compile_script_call(node, out.script_name, out.arguments, diagnostics,
                                   "scripted_trigger");
    }

    if (node.kind == ScriptValueKind::Expression) {
        // Infix trigger comparison (`population > 1000`, `value:income - 5 >= 2`):
        // both operands lower to value bytecode evaluated in the current scope.
        const auto comparison = comparison_from_op_text(symbols_.text(node.key));
        if (!comparison || node.children.size() != 2u) {
            diagnostics.push_back({"arithmetic expressions cannot act as conditions", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::SourceCompare;
        out.comparison = *comparison;
        if (!compile_value_expression(node.children[0], scope, out.compare_lhs, diagnostics)) return false;
        if (!compile_value_expression(node.children[1], scope, out.compare_rhs, diagnostics)) return false;
        return true;
    }

    if (lower == "has_variable") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"has_variable requires a variable name", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::HasVariable;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "variable_equals" || lower == "variable_not_equals" ||
        lower == "variable_above" || lower == "variable_at_least" ||
        lower == "variable_below" || lower == "variable_at_most") {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"variable comparison requires a block", node.line});
            return false;
        }
        const ScriptNode* name_node = node.find(sym_name_);
        const ScriptNode* value_node = node.find(sym_value_);
        if (name_node == nullptr || name_node->kind != ScriptValueKind::Symbol || value_node == nullptr) {
            diagnostics.push_back({"variable comparison requires symbolic name and value", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::CompareVariable;
        const auto variable_name = symbols_.text(name_node->symbol);
        out.binding_name = script_stable_key(variable_name);
        (void)track_stable_name(variable_name, name_node->line, diagnostics);
        if (lower == "variable_equals") out.comparison = ScriptComparison::Equal;
        else if (lower == "variable_not_equals") out.comparison = ScriptComparison::NotEqual;
        else if (lower == "variable_above") out.comparison = ScriptComparison::Above;
        else if (lower == "variable_at_least") out.comparison = ScriptComparison::AtLeast;
        else if (lower == "variable_below") out.comparison = ScriptComparison::Below;
        else out.comparison = ScriptComparison::AtMost;
        if (!compile_argument(*value_node, out.argument, diagnostics, "variable comparison value")) return false;
        const bool ordering = out.comparison != ScriptComparison::Equal &&
                              out.comparison != ScriptComparison::NotEqual;
        if (ordering && out.argument.source == ScriptArgumentSourceKind::Literal &&
            out.argument.literal.kind != ScriptArgumentKind::Number) {
            diagnostics.push_back({"ordered variable comparison requires a numeric value", value_node->line});
            return false;
        }
        return true;
    }

    ScopeSelector selector;
    CompileScope nested;
    if (parse_scope_selector(text, selector, scope, root_scope, nested)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope switch must be a block", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::Scope;
        out.selector = selector;
        if (selector.kind == ScopeSelectorKind::Saved) {
            const auto colon = text.find(':');
            if (colon != std::string_view::npos)
                (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        }
        return compile_scoped_condition_block(node, nested, root_scope, out.children, diagnostics);
    }

    ScopeIteratorMode iterator_mode;
    ScopeType iterator_target = ScopeType::None;
    ScriptStableKey collection_name = 0;
    if (parse_collection_iterator(text, iterator_mode, collection_name)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"collection iterator must be a block", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_source = ScopeIteratorSource::Collection;
        out.collection_name = collection_name;
        const auto colon = text.find(':');
        if (colon != std::string_view::npos)
            (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        return compile_scoped_condition_block(node, {}, root_scope, out.children, diagnostics);
    }
    if (parse_iterator(text, iterator_mode, iterator_target)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope iterator must be a block", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_target = iterator_target;
        // Parse generic iterator config (limit/order_by/offset) if present as direct children
        ScopeIteratorConfig cfg{};
        if (iterator_mode == ScopeIteratorMode::Ordered) cfg.ordered = true;
        // Extract limit/order_by from block's direct children before recursing
        // We keep original node but filter config keys for child compilation
        ScriptNode filtered = node;
        filtered.children.clear();
        for (auto &child : node.children) {
            const auto clower = ascii_lower(symbols_.text(child.key));
            if (clower == "limit" && child.kind == ScriptValueKind::Number) {
                std::uint32_t value = 0u;
                if (!read_u32_count(child, value)) {
                    diagnostics.push_back({"limit must be a non-negative integer below 4294967296", child.line});
                    return false;
                }
                cfg.has_limit=true; cfg.limit=value; cfg.ordered=true; continue;
            }
            if (clower == "limit" && child.kind == ScriptValueKind::Block) {
                // Clausewitz-style candidate filter, evaluated per candidate in
                // the iterated scope. Candidates failing it are excluded from
                // any/every/ordered/random semantics alike.
                if (!compile_scoped_condition_block(child, {iterator_target, true}, root_scope,
                                                    out.iterator_filter, diagnostics)) {
                    return false;
                }
                continue;
            }
            if (clower == "offset" && child.kind == ScriptValueKind::Number) {
                std::uint32_t value = 0u;
                if (!read_u32_count(child, value)) {
                    diagnostics.push_back({"offset must be a non-negative integer below 4294967296", child.line});
                    return false;
                }
                // offset is only honoured in ordered iteration; without this an
                // `offset =` with no limit/order_by was silently discarded.
                cfg.offset=value; cfg.ordered=true; continue;
            }
            if (clower == "order_by" && child.kind == ScriptValueKind::Symbol) { cfg.has_order_by=true; cfg.order_by_value=child.symbol; cfg.order_by_key=script_stable_key(symbols_.text(child.symbol)); cfg.ordered=true; continue; }
            if (clower == "descending" && child.kind == ScriptValueKind::Symbol) { const auto v=ascii_lower(symbols_.text(child.symbol)); cfg.descending=(v=="yes"||v=="true"); continue; }
            filtered.children.push_back(child);
        }
        out.iterator_config = cfg;
        if (cfg.ordered) out.iterator_mode = ScopeIteratorMode::Ordered;
        return compile_scoped_condition_block(filtered, {iterator_target, true}, root_scope, out.children, diagnostics);
    }

    const auto id = registry_.find_trigger(text);
    if (!id.valid()) {
        diagnostics.push_back({"unknown trigger: " + std::string{text}, node.line});
        return false;
    }
    if (scope.known && registry_.trigger_scope(id) != scope.type) {
        diagnostics.push_back({"trigger scope mismatch: " + std::string{text}, node.line});
        return false;
    }
    CompiledScriptArgument argument;
    if (!compile_argument(node, argument, diagnostics, "trigger primitive argument")) return false;
    std::optional<ScriptArgumentKind> static_kind;
    if (argument.source == ScriptArgumentSourceKind::Literal) static_kind = argument.literal.kind;
    else if (argument.source == ScriptArgumentSourceKind::ThisScope ||
             argument.source == ScriptArgumentSourceKind::RootScope ||
             argument.source == ScriptArgumentSourceKind::FromScope ||
             argument.source == ScriptArgumentSourceKind::PrevScope ||
             argument.source == ScriptArgumentSourceKind::EventTarget) static_kind = ScriptArgumentKind::Scope;
    else if (argument.source == ScriptArgumentSourceKind::ScriptedValue) static_kind = ScriptArgumentKind::Number;
    if (static_kind.has_value() && !registry_.trigger_accepts_argument(id, *static_kind)) {
        diagnostics.push_back({"trigger does not accept this argument type: " + std::string{text}, node.line});
        return false;
    }
    out.kind = ScopedConditionKind::Trigger;
    out.primitive = id;
    out.argument = argument;
    return true;
}

bool ScriptCompiler::compile_scoped_condition_block(const ScriptNode& block, CompileScope scope,
                                                    ScopeType root_scope,
                                                    std::vector<CompiledScopedCondition>& out,
                                                    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"trigger must be a block", block.line});
        return false;
    }
    bool ok = true;
    out.reserve(out.size() + block.children.size());
    for (const auto& child : block.children) {
        CompiledScopedCondition compiled;
        if (!compile_scoped_condition_node(child, scope, root_scope, compiled, diagnostics)) {
            ok = false;
            continue;
        }
        out.push_back(std::move(compiled));
    }
    return ok;
}

bool ScriptCompiler::compile_scoped_effect_node(const ScriptNode& node, CompileScope scope,
                                                ScopeType root_scope, CompiledScopedEffect& out,
                                                std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    const auto text = symbols_.text(node.key);
    const auto lower = ascii_lower(text);
    out.source_line = node.line;

    if (lower == "save_scope_as" || lower == "save_event_target_as") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"save_event_target_as requires a target name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::SaveScope;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "clear_event_target") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"clear_event_target requires a target name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::ClearEventTarget;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "set_variable" || lower == "change_variable") {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({std::string{lower} + " requires a block", node.line});
            return false;
        }
        const ScriptNode* name_node = node.find(sym_name_);
        const ScriptNode* value_node = node.find(lower == "change_variable" ? sym_add_ : sym_value_);
        if (value_node == nullptr && lower == "change_variable") value_node = node.find(sym_value_);
        if (name_node == nullptr || name_node->kind != ScriptValueKind::Symbol || value_node == nullptr) {
            diagnostics.push_back({std::string{lower} + " requires symbolic name and value", node.line});
            return false;
        }
        out.kind = lower == "set_variable" ? ScopedEffectKind::SetVariable : ScopedEffectKind::ChangeVariable;
        const auto variable_name = symbols_.text(name_node->symbol);
        out.binding_name = script_stable_key(variable_name);
        (void)track_stable_name(variable_name, name_node->line, diagnostics);
        if (!compile_argument(*value_node, out.argument, diagnostics, "variable value")) return false;
        if (out.kind == ScopedEffectKind::ChangeVariable &&
            out.argument.source == ScriptArgumentSourceKind::Literal &&
            out.argument.literal.kind != ScriptArgumentKind::Number) {
            diagnostics.push_back({"change_variable requires a numeric value", value_node->line});
            return false;
        }
        return true;
    }

    if (lower == "clear_variable") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"clear_variable requires a variable name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::ClearVariable;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "add_to_collection" || lower == "remove_from_collection") {
        const bool adding = lower == "add_to_collection";
        out.kind = adding ? ScopedEffectKind::AddToCollection : ScopedEffectKind::RemoveFromCollection;
        if (node.kind == ScriptValueKind::Symbol) {
            const auto name = symbols_.text(node.symbol);
            out.collection_name = script_stable_key(name);
            (void)track_stable_name(name, node.line, diagnostics);
            out.argument.source = ScriptArgumentSourceKind::ThisScope;
            return true;
        }
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({std::string{lower} + " requires a name or block", node.line});
            return false;
        }
        const ScriptNode* name_node = node.find(sym_name_);
        const ScriptNode* value_node = node.find(sym_value_);
        if (name_node == nullptr || name_node->kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({std::string{lower} + " requires symbolic name", node.line});
            return false;
        }
        const auto collection_name_text = symbols_.text(name_node->symbol);
        out.collection_name = script_stable_key(collection_name_text);
        (void)track_stable_name(collection_name_text, name_node->line, diagnostics);
        if (value_node == nullptr) {
            out.argument.source = ScriptArgumentSourceKind::ThisScope;
            return true;
        }
        return compile_argument(*value_node, out.argument, diagnostics, "collection value");
    }

    if (lower == "clear_collection") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"clear_collection requires a collection name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::ClearCollection;
        const auto name = symbols_.text(node.symbol);
        out.collection_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "scripted_effect") {
        out.kind = ScopedEffectKind::ScriptCall;
        out.call_scope = scope.known ? scope.type : ScopeType::None;
        return compile_script_call(node, out.script_name, out.arguments, diagnostics,
                                   "scripted_effect");
    }

    ScopeSelector selector;
    CompileScope nested;
    if (parse_scope_selector(text, selector, scope, root_scope, nested)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope switch must be a block", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::Scope;
        out.selector = selector;
        if (selector.kind == ScopeSelectorKind::Saved) {
            const auto colon = text.find(':');
            if (colon != std::string_view::npos)
                (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        }
        return compile_scoped_effect_block(node, nested, root_scope, out.children, diagnostics);
    }

    ScopeIteratorMode iterator_mode;
    ScopeType iterator_target = ScopeType::None;
    ScriptStableKey collection_name = 0;
    if (parse_collection_iterator(text, iterator_mode, collection_name)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"collection iterator must be a block", node.line});
            return false;
        }
        if (iterator_mode == ScopeIteratorMode::Any) {
            diagnostics.push_back({"any_in:* iterators are trigger-only", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_source = ScopeIteratorSource::Collection;
        out.collection_name = collection_name;
        const auto colon = text.find(':');
        if (colon != std::string_view::npos)
            (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        return compile_scoped_effect_block(node, {}, root_scope, out.children, diagnostics);
    }
    if (parse_iterator(text, iterator_mode, iterator_target)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope iterator must be a block", node.line});
            return false;
        }
        if (iterator_mode == ScopeIteratorMode::Any) {
            diagnostics.push_back({"any_* iterators are trigger-only; use every_* or random_* in effects", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_target = iterator_target;
        ScopeIteratorConfig cfg{};
        if (iterator_mode == ScopeIteratorMode::Ordered) cfg.ordered = true;
        ScriptNode filtered = node;
        filtered.children.clear();
        for (auto &child : node.children) {
            const auto clower = ascii_lower(symbols_.text(child.key));
            if (clower == "limit" && child.kind == ScriptValueKind::Number) {
                std::uint32_t value = 0u;
                if (!read_u32_count(child, value)) {
                    diagnostics.push_back({"limit must be a non-negative integer below 4294967296", child.line});
                    return false;
                }
                cfg.has_limit=true; cfg.limit=value; cfg.ordered=true; continue;
            }
            if (clower == "limit" && child.kind == ScriptValueKind::Block) {
                // Clausewitz-style candidate filter: `limit = { <conditions> }`
                // gates each candidate in its own scope before the body runs.
                // Distinct from the numeric `limit = N` count cap above.
                out.has_iterator_condition = true;
                out.condition.kind = ScopedConditionKind::All;
                if (!compile_scoped_condition_block(child, {iterator_target, true}, root_scope,
                                                    out.condition.children, diagnostics)) {
                    return false;
                }
                continue;
            }
            if (clower == "offset" && child.kind == ScriptValueKind::Number) {
                std::uint32_t value = 0u;
                if (!read_u32_count(child, value)) {
                    diagnostics.push_back({"offset must be a non-negative integer below 4294967296", child.line});
                    return false;
                }
                cfg.offset=value; cfg.ordered=true; continue;
            }
            if (clower == "order_by" && child.kind == ScriptValueKind::Symbol) { cfg.has_order_by=true; cfg.order_by_value=child.symbol; cfg.order_by_key=script_stable_key(symbols_.text(child.symbol)); cfg.ordered=true; continue; }
            if (clower == "descending" && child.kind == ScriptValueKind::Symbol) { const auto v=ascii_lower(symbols_.text(child.symbol)); cfg.descending=(v=="yes"||v=="true"); continue; }
            filtered.children.push_back(child);
        }
        out.iterator_config = cfg;
        if (cfg.ordered) out.iterator_mode = ScopeIteratorMode::Ordered;
        return compile_scoped_effect_block(filtered, {iterator_target, true}, root_scope, out.children, diagnostics);
    }

    if (lower == "if") {
        // Conditional effects: `if = { limit = { <conditions> } <effects...>
        // else = { <effects...> } }`. The limit compiles into an All condition
        // (empty limit is vacuously true); `else` is optional.
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"if must be a block", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::If;
        bool have_limit = false;
        for (const auto& child : node.children) {
            const auto child_lower = ascii_lower(symbols_.text(child.key));
            if (child_lower == "limit") {
                if (child.kind != ScriptValueKind::Block) {
                    diagnostics.push_back({"if limit must be a block", child.line});
                    return false;
                }
                out.condition.kind = ScopedConditionKind::All;
                if (!compile_scoped_condition_block(child, scope, root_scope,
                                                    out.condition.children, diagnostics)) {
                    return false;
                }
                have_limit = true;
            } else if (child_lower == "else") {
                if (child.kind != ScriptValueKind::Block) {
                    diagnostics.push_back({"else must be a block", child.line});
                    return false;
                }
                if (!compile_scoped_effect_block(child, scope, root_scope,
                                                 out.else_children, diagnostics)) {
                    return false;
                }
            } else {
                CompiledScopedEffect compiled;
                if (!compile_scoped_effect_node(child, scope, root_scope, compiled, diagnostics)) {
                    return false;
                }
                out.children.push_back(std::move(compiled));
            }
        }
        if (!have_limit) {
            diagnostics.push_back({"if requires a limit block", node.line});
            return false;
        }
        return true;
    }

    if (lower == "while") {
        // Bounded loop: `while = { limit = { <conditions> } <effects...> }`.
        // The condition tree is re-evaluated in the current scope before every
        // pass; the VM work budget charges each pass, so a never-failing limit
        // terminates deterministically with a budget error instead of hanging.
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"while must be a block", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::While;
        bool have_limit = false;
        for (const auto& child : node.children) {
            const auto child_lower = ascii_lower(symbols_.text(child.key));
            if (child_lower == "limit" && child.kind == ScriptValueKind::Block) {
                out.condition.kind = ScopedConditionKind::All;
                if (!compile_scoped_condition_block(child, scope, root_scope,
                                                    out.condition.children, diagnostics)) {
                    return false;
                }
                have_limit = true;
            } else {
                CompiledScopedEffect compiled;
                if (!compile_scoped_effect_node(child, scope, root_scope, compiled, diagnostics)) {
                    return false;
                }
                out.children.push_back(std::move(compiled));
            }
        }
        if (!have_limit) {
            diagnostics.push_back({"while requires a limit block", node.line});
            return false;
        }
        return true;
    }

    if (lower == "random_list") {
        // Weighted random branch selection: each child is `<weight> = { <effects...> }`.
        // The draw is the VM's keyed deterministic RNG (callsite salt + a per-
        // callsite draw counter), so replays are stable for equal world state.
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"random_list must be a block", node.line});
            return false;
        }
        if (node.children.empty()) {
            diagnostics.push_back({"random_list must declare at least one weighted branch", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::RandomList;
        out.children.reserve(node.children.size());
        out.random_list_weights.reserve(node.children.size());
        for (const auto& branch : node.children) {
            std::uint32_t weight = 0u;
            const auto weight_text = symbols_.text(branch.key);
            if (branch.key == SymbolId{} || !parse_u32_weight(weight_text, weight)) {
                diagnostics.push_back({"random_list branch key must be a numeric weight, got: " +
                                       std::string{weight_text}, branch.line});
                return false;
            }
            if (branch.kind != ScriptValueKind::Block) {
                diagnostics.push_back({"random_list branch must be a block of effects", branch.line});
                return false;
            }
            CompiledScopedEffect branch_group;
            branch_group.kind = ScopedEffectKind::Group;
            if (!compile_scoped_effect_block(branch, scope, root_scope,
                                             branch_group.children, diagnostics)) {
                return false;
            }
            out.random_list_weights.push_back(weight);
            out.children.push_back(std::move(branch_group));
        }
        return true;
    }

    const auto id = registry_.find_effect(text);
    if (!id.valid()) {
        diagnostics.push_back({"unknown effect: " + std::string{text}, node.line});
        return false;
    }
    if (scope.known && registry_.effect_scope(id) != scope.type) {
        diagnostics.push_back({"effect scope mismatch: " + std::string{text}, node.line});
        return false;
    }
    CompiledScriptArgument argument;
    if (!compile_argument(node, argument, diagnostics, "effect primitive argument")) return false;
    std::optional<ScriptArgumentKind> static_kind;
    if (argument.source == ScriptArgumentSourceKind::Literal) static_kind = argument.literal.kind;
    else if (argument.source == ScriptArgumentSourceKind::ThisScope ||
             argument.source == ScriptArgumentSourceKind::RootScope ||
             argument.source == ScriptArgumentSourceKind::FromScope ||
             argument.source == ScriptArgumentSourceKind::PrevScope ||
             argument.source == ScriptArgumentSourceKind::EventTarget) static_kind = ScriptArgumentKind::Scope;
    else if (argument.source == ScriptArgumentSourceKind::ScriptedValue) static_kind = ScriptArgumentKind::Number;
    if (static_kind.has_value() && !registry_.effect_accepts_argument(id, *static_kind)) {
        diagnostics.push_back({"effect does not accept this argument type: " + std::string{text}, node.line});
        return false;
    }
    out.kind = ScopedEffectKind::Effect;
    out.primitive = id;
    out.argument = argument;
    return true;
}

bool ScriptCompiler::compile_scoped_effect_block(const ScriptNode& block, CompileScope scope,
                                                 ScopeType root_scope,
                                                 std::vector<CompiledScopedEffect>& out,
                                                 std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"effect must be a block", block.line});
        return false;
    }
    bool ok = true;
    out.reserve(out.size() + block.children.size());
    // `else_if` / trailing `else` attach to the innermost `if` of the chain
    // built so far: `if → else_if → else_if → else` nests each new branch into
    // the previous branch's else_children, which the VM evaluates in order.
    const auto innermost_else_chain = [](CompiledScopedEffect& node) -> CompiledScopedEffect* {
        CompiledScopedEffect* chain = &node;
        while (chain->kind == ScopedEffectKind::If && !chain->else_children.empty() &&
               chain->else_children.back().kind == ScopedEffectKind::If) {
            chain = &chain->else_children.back();
        }
        return chain;
    };
    for (const auto& child : block.children) {
        const auto child_lower = ascii_lower(symbols_.text(child.key));
        if ((child_lower == "else" || child_lower == "else_if") && !out.empty()) {
            auto* chain = innermost_else_chain(out.back());
            if (chain->kind != ScopedEffectKind::If) {
                diagnostics.push_back({std::string{child_lower} + " without a preceding if", child.line});
                ok = false;
                continue;
            }
            if (child.kind != ScriptValueKind::Block) {
                diagnostics.push_back({std::string{child_lower} + " must be a block", child.line});
                ok = false;
                continue;
            }
            if (child_lower == "else") {
                if (!compile_scoped_effect_block(child, scope, root_scope, chain->else_children, diagnostics)) {
                    ok = false;
                }
                continue;
            }
            // else_if: a nested If carrying this branch's limit and effects.
            CompiledScopedEffect branch;
            branch.kind = ScopedEffectKind::If;
            branch.source_line = child.line;
            bool branch_ok = true;
            bool have_limit = false;
            for (const auto& branch_child : child.children) {
                const auto branch_lower = ascii_lower(symbols_.text(branch_child.key));
                if (branch_lower == "limit" && branch_child.kind == ScriptValueKind::Block) {
                    branch.condition.kind = ScopedConditionKind::All;
                    branch_ok &= compile_scoped_condition_block(branch_child, scope, root_scope,
                                                                branch.condition.children, diagnostics);
                    have_limit = true;
                } else {
                    CompiledScopedEffect compiled;
                    if (!compile_scoped_effect_node(branch_child, scope, root_scope, compiled, diagnostics)) {
                        branch_ok = false;
                        continue;
                    }
                    branch.children.push_back(std::move(compiled));
                }
            }
            if (!have_limit) {
                diagnostics.push_back({"else_if requires a limit block", child.line});
                ok = false;
                continue;
            }
            if (branch_ok) chain->else_children.push_back(std::move(branch));
            else ok = false;
            continue;
        }
        CompiledScopedEffect compiled;
        if (!compile_scoped_effect_node(child, scope, root_scope, compiled, diagnostics)) {
            ok = false;
            continue;
        }
        out.push_back(std::move(compiled));
    }
    return ok;
}

bool ScriptCompiler::compile(const ScriptParseResult& parsed, ScriptProgramDatabase& out,
                             std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    bool ok = parsed.ok();
    for (const auto& d : parsed.diagnostics) diagnostics.push_back({d.message, d.line});

    for (const auto& object : parsed.objects) {
        if (object.type == sym_script_) {
            ScriptProgram program;
            program.name = object.name;
            const ScriptNode* scope_node = nullptr;
            for (const auto& field : object.fields) if (field.key == sym_scope_) { scope_node = &field; break; }
            if (scope_node == nullptr || (program.scope = parse_scope(*scope_node)) == ScopeType::None) {
                diagnostics.push_back({"script requires a valid scope", object.line});
                ok = false;
                continue;
            }
            bool object_ok = true;
            bool have_trigger = false;
            bool use_scoped_conditions = false;
            bool use_compact_conditions = false;
            bool use_scoped_effects = false;
            // A ScriptProgram must use one backend per phase. Selecting per top-level
            // block can otherwise populate both representations and make the VM silently
            // skip whichever representation it did not choose.
            for (const auto& field : object.fields) {
                if (field.key == sym_trigger_ && field.kind == ScriptValueKind::Block) {
                    for (const auto& child : field.children) {
                        const auto lower = ascii_lower(symbols_.text(child.key));
                        use_scoped_conditions |= is_advanced_condition(child);
                        use_compact_conditions |= lower == "all" || lower == "any" || lower == "not";
                    }
                } else if (field.key == sym_effect_ && field.kind == ScriptValueKind::Block) {
                    for (const auto& child : field.children)
                        use_scoped_effects |= is_advanced_effect(child);
                }
            }
            std::size_t compiled_trigger_blocks = 0u;
            for (const auto& field : object.fields) {
                if (field.key == sym_parameters_) {
                    object_ok &= compile_parameters(field, program.parameters, diagnostics);
                } else if (field.key == sym_trigger_) {
                    have_trigger = true;
                    if (use_scoped_conditions) {
                        object_ok &= compile_scoped_condition_block(field, {program.scope, true}, program.scope,
                                                                    program.scoped_conditions, diagnostics);
                    } else if (use_compact_conditions) {
                        const bool compiled = compile_condition_block(
                            field, program.scope, ConditionOp::And, program.condition, diagnostics);
                        object_ok &= compiled;
                        if (compiled) {
                            if (compiled_trigger_blocks > 0u)
                                program.condition.push_back({ConditionOp::And, 0u, {}, 0.0});
                            ++compiled_trigger_blocks;
                        }
                    } else {
                        object_ok &= compile_fast_all(field, program.scope, program.fast_all, diagnostics);
                    }
                } else if (field.key == sym_effect_) {
                    if (use_scoped_effects) {
                        object_ok &= compile_scoped_effect_block(field, {program.scope, true}, program.scope,
                                                                 program.scoped_effects, diagnostics);
                    } else {
                        object_ok &= compile_effects(field, program.scope, program.effects, diagnostics);
                    }
                }
            }
            if (!have_trigger) program.condition.push_back({ConditionOp::PushTrue, 0u, {}, 0.0});
            if (object_ok) {
                const auto program_name = symbols_.text(program.name);
                (void)track_stable_name(program_name, object.line, diagnostics);
                const auto program_key = script_stable_key(program_name);
                assign_condition_callsites(program.scoped_conditions, program_key, program_key);
                assign_effect_callsites(program.scoped_effects, program_key, program_key);
                out.add(std::move(program));
            } else ok = false;
        } else if (object.type == sym_scripted_value_) {
            ScriptedValueProgram program;
            program.name = object.name;
            program.source_line = object.line;
            bool object_ok = true;
            bool have_source = false;
            for (const auto& field : object.fields) {
                if (field.key == sym_scope_) {
                    program.scope = parse_scope(field);
                } else if (field.key == sym_expression_ && field.kind == ScriptValueKind::Number) {
                    // Constant-only expression, already folded by the parser
                    // (`expression = (900 - 100) / 4 * 10` → 2000). Compiles to
                    // a single PushConst.
                    if (have_source && !program.uses_bytecode) {
                        diagnostics.push_back({"scripted_value cannot mix source and expression", field.line});
                        object_ok = false;
                        continue;
                    }
                    program.bytecode = ScriptValueBytecode{};
                    program.uses_bytecode = true;
                    program.source = ValueSource::Constant;
                    program.bytecode.ops.push_back(ScriptValueOp::PushConst);
                    program.bytecode.const_pool.push_back(field.number);
                    have_source = true;
                } else if (field.kind == ScriptValueKind::Expression) {
                    // Symbolic expression tree: `expression = (population * 0.5 +
                    // gdp * 0.3)` lowers into ScriptValueBytecode with PushSource /
                    // PushScriptedValue / PushVariable operands. The field node IS
                    // the tree root (its key is the root operator). Mutually
                    // exclusive with `source =` by construction.
                    if (have_source && !program.uses_bytecode) {
                        diagnostics.push_back({"scripted_value cannot mix source and expression", field.line});
                        object_ok = false;
                        continue;
                    }
                    program.bytecode = ScriptValueBytecode{};
                    program.uses_bytecode = true;
                    program.source = ValueSource::Constant;
                    object_ok &= compile_value_expression(field, {program.scope, program.scope != ScopeType::None},
                                                          program.bytecode, diagnostics);
                    have_source = true;
                } else if (field.key == sym_source_) {
                    if (program.uses_bytecode) {
                        diagnostics.push_back({"scripted_value cannot mix source and expression", field.line});
                        object_ok = false;
                        continue;
                    }
                    bool builtin_source = false;
                    if (field.kind == ScriptValueKind::Symbol) {
                        if (const auto source = builtin_source_from_name(symbols_.text(field.symbol))) {
                            program.source = *source;
                            builtin_source = true;
                        }
                    }
                    if (!builtin_source) {
                        CompiledScriptArgument runtime_source;
                        if (!compile_argument(field, runtime_source, diagnostics, "scripted value source")) {
                            object_ok = false;
                        } else if (runtime_source.source == ScriptArgumentSourceKind::Literal &&
                                   runtime_source.literal.kind == ScriptArgumentKind::SymbolHash) {
                            diagnostics.push_back({"unknown scripted value source: " +
                                                   std::string{symbols_.text(field.symbol)}, field.line});
                            object_ok = false;
                        } else {
                            program.source = ValueSource::RuntimeArgument;
                            program.runtime_source = runtime_source;
                        }
                    }
                    have_source = true;
                } else if (field.key == sym_multiply_ && field.kind == ScriptValueKind::Number) {
                    program.multiply = field.number;
                } else if (field.key == sym_add_ && field.kind == ScriptValueKind::Number) {
                    program.add = field.number;
                } else if (field.key == sym_parameters_) {
                    object_ok &= compile_parameters(field, program.parameters, diagnostics);
                }
            }
            if (program.scope == ScopeType::None) {
                diagnostics.push_back({"scripted_value requires a valid scope", object.line});
                object_ok = false;
            }
            if (!have_source) {
                diagnostics.push_back({"scripted_value requires source or expression", object.line});
                object_ok = false;
            } else if (!program.uses_bytecode &&
                       program.scope != ScopeType::None && program.source != ValueSource::RuntimeArgument &&
                       value_source_scope(program.source) != program.scope) {
                diagnostics.push_back({"scripted_value source does not match declared scope", object.line});
                object_ok = false;
            }
            if (object_ok) out.add(program); else ok = false;
        } else if (object.type == sym_history_) {
            CompiledHistoryPatch patch;
            patch.target = object.name;
            bool object_ok = true;
            for (const auto& field : object.fields) {
                if (field.key == sym_date_ && field.kind == ScriptValueKind::Symbol) {
                    patch.yyyymmdd = parse_yyyymmdd(symbols_.text(field.symbol));
                    if (patch.yyyymmdd == 0) { diagnostics.push_back({"invalid history date", field.line}); object_ok = false; }
                } else if (field.key == sym_effect_) {
                    object_ok &= compile_effects(field, ScopeType::Country, patch.effects, diagnostics);
                }
            }
            if (patch.yyyymmdd == 0) { diagnostics.push_back({"history requires date", object.line}); object_ok = false; }
            if (object_ok) out.add(std::move(patch)); else ok = false;
        }
    }
    return ok && diagnostics.empty();
}
} // namespace thunder
