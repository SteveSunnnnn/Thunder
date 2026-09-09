#pragma once
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptContext.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace thunder {

struct CompiledTriggerCall {
    TriggerPrimitiveId primitive{};
    double argument = 0.0;
};

struct CompiledEffectCall {
    EffectPrimitiveId primitive{};
    double argument = 0.0;
};

enum class ScriptArgumentSourceKind : std::uint8_t {
    Literal,
    Variable,
    Parameter,
    EventTarget,
    ThisScope,
    RootScope,
    FromScope,
    PrevScope,
    ScriptedValue
};

struct CompiledScriptArgument {
    ScriptArgumentSourceKind source = ScriptArgumentSourceKind::Literal;
    ScriptArgument literal{};
    ScriptStableKey key = 0;
    SymbolId script_name{};
};

struct CompiledNamedArgument {
    ScriptStableKey name = 0;
    CompiledScriptArgument value{};
};

enum class ConditionOp : std::uint8_t { PushTrue, CallTrigger, And, Or, Not };

struct ConditionInstruction {
    ConditionOp op = ConditionOp::PushTrue;
    std::uint8_t reserved = 0;
    TriggerPrimitiveId primitive{};
    double argument = 0.0;
};
static_assert(sizeof(ConditionInstruction) <= 16u);

enum class ScopeSelectorKind : std::uint8_t {
    This,
    Root,
    From,
    Prev,
    Owner,
    Country,
    Market,
    State,
    Province,
    Saved
};

struct ScopeSelector {
    ScopeSelectorKind kind = ScopeSelectorKind::This;
    ScriptStableKey saved_name = 0;
};

// Generic iterator vocabulary: ordered/limit/order_by are engine-level
// primitives, not game-specific content. Ordering is driven by a scripted
// value so any domain (economy, politics, warfare) can reuse the same
// deterministic traversal without hard-coding domain keys.
enum class ScopeIteratorMode : std::uint8_t { Any, Every, Random, Ordered };
enum class ScopeIteratorSource : std::uint8_t { Children, Collection };
enum class ScriptComparison : std::uint8_t { Equal, NotEqual, Above, AtLeast, Below, AtMost };

struct ScopeIteratorConfig {
    // Deterministic ordered traversal (stable tie-break). limit/order_by
    // are generic; content supplies the ordering scripted value.
    bool ordered = false;
    bool has_limit = false;
    bool has_order_by = false;
    std::uint32_t limit = 0;                 // 0 = no truncation
    std::uint32_t offset = 0;                // pagination offset
    bool descending = false;
    SymbolId order_by_value{};               // scripted_value name
    ScriptStableKey order_by_key = 0;
};

enum class ValueSource : std::uint8_t {
    Population,
    Gdp,
    Treasury,
    TaxRate,
    PopulationSize,
    Employment,
    StandardOfLiving,
    Literacy,
    Qualification,
    Wealth,
    PoliticalStrength,
    MarketSupply,
    MarketDemand,
    StatePopulation,
    ProvincePopulation,
    BuildingLevel,
    BuildingEmployees,
    BuildingProfit,
    BuildingCash,
    ArmyManpower,
    ArmyOrganization,
    FrontProgress,
    WarScore,
    WarWeeks,
    RuntimeArgument,
    // Generic expression bytecode sources: content composes arithmetic/
    // conditional values without engine hard-coding domain formulas.
    Constant,
    VariableRef,
    ScriptedValueRef
};

enum class ScriptValueOp : std::uint8_t {
    PushConst, PushVariable, PushScriptedValue, PushSource,
    Add, Sub, Mul, Div, Neg,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or, Not, If,
    Max, Min, Clamp
};

struct ScriptValueBytecode {
    std::vector<ScriptValueOp> ops;
    std::vector<double> const_pool;
    std::vector<ScriptStableKey> var_keys; // for PushVariable
    std::vector<ValueSource> source_pool;  // for PushSource
    std::vector<SymbolId> value_refs;      // for PushScriptedValue
    // Deterministic evaluation: no heap allocation per eval, integer
    // fast-path preserved for simple source*mul+add programs.
};

enum class ScopedConditionKind : std::uint8_t {
    Trigger,
    ScriptCall,
    All,
    Any,
    Not,
    Scope,
    Iterator,
    HasVariable,
    CompareVariable,
    SourceCompare
};

struct CompiledScopedCondition {
    ScopedConditionKind kind = ScopedConditionKind::All;
    TriggerPrimitiveId primitive{};
    CompiledScriptArgument argument{};
    ScopeSelector selector{};
    ScopeType iterator_target = ScopeType::None;
    ScopeIteratorMode iterator_mode = ScopeIteratorMode::Any;
    ScopeIteratorSource iterator_source = ScopeIteratorSource::Children;
    ScopeIteratorConfig iterator_config{};
    // Iterator `limit = { <conditions> }` filter (Clausewitz-style candidate
    // gating). Non-empty means every candidate must satisfy ALL entries in its
    // own scope before the iterator body/window sees it; distinct from
    // `limit = <number>` which is a count cap in iterator_config.
    std::vector<CompiledScopedCondition> iterator_filter;
    ScriptComparison comparison = ScriptComparison::Equal;
    SymbolId script_name{};
    ScriptStableKey binding_name = 0;
    ScriptStableKey collection_name = 0;
    ScopeType call_scope = ScopeType::None;
    std::uint64_t salt = 0;
    std::uint32_t source_line = 0u;
    std::vector<CompiledNamedArgument> arguments;
    std::vector<CompiledScopedCondition> children;
    // SourceCompare: infix trigger comparisons (`population > 1000`,
    // `value:income - value:upkeep >= 3`). Both sides lower to value bytecode
    // evaluated in the current scope; `comparison` carries the operator.
    ScriptValueBytecode compare_lhs;
    ScriptValueBytecode compare_rhs;
};

enum class ScopedEffectKind : std::uint8_t {
    Effect,
    ScriptCall,
    Scope,
    Iterator,
    SaveScope,
    ClearEventTarget,
    SetVariable,
    ChangeVariable,
    ClearVariable,
    AddToCollection,
    RemoveFromCollection,
    ClearCollection,
    Group,
    RandomList,
    While,
    If
};

struct CompiledScopedEffect {
    ScopedEffectKind kind = ScopedEffectKind::Effect;
    EffectPrimitiveId primitive{};
    CompiledScriptArgument argument{};
    ScopeSelector selector{};
    ScopeType iterator_target = ScopeType::None;
    ScopeIteratorMode iterator_mode = ScopeIteratorMode::Every;
    ScopeIteratorSource iterator_source = ScopeIteratorSource::Children;
    ScopeIteratorConfig iterator_config{};
    // If: `limit` compiles into `condition` (All over its children); the
    // taken branch is `children`, the optional `else` block is `else_children`.
    // Iterator: when `has_iterator_condition` is set, `condition` holds the
    // `limit = { <conditions> }` candidate filter evaluated in each candidate.
    CompiledScopedCondition condition{};
    bool has_iterator_condition = false;
    std::vector<CompiledScopedEffect> else_children;
    // RandomList: one weight per `children` entry (each branch compiles into a
    // Group child). Weights come from the numeric branch keys (`10 = { ... }`).
    std::vector<std::uint32_t> random_list_weights;
    SymbolId script_name{};
    ScriptStableKey binding_name = 0;
    ScriptStableKey collection_name = 0;
    ScopeType call_scope = ScopeType::None;
    std::uint64_t salt = 0;
    std::uint32_t source_line = 0u;
    std::vector<CompiledNamedArgument> arguments;
    std::vector<CompiledScopedEffect> children;
};

struct ScriptParameterDefinition {
    std::string name;
    ScriptStableKey key = 0u;
    ScriptArgumentKind kind = ScriptArgumentKind::None;
    // Only meaningful for Scope. None means any valid concrete scope type.
    ScopeType scope = ScopeType::None;
    bool required = true;
    std::optional<ScriptArgument> default_value;
    std::uint32_t source_line = 0u;
};

struct ScriptProgram {
    SymbolId name{};
    ScopeType scope = ScopeType::None;
    std::vector<ScriptParameterDefinition> parameters;
    // Fast path for the overwhelmingly common `trigger = { a = x b = y }` AND case.
    std::vector<CompiledTriggerCall> fast_all;
    // Same-scope boolean expressions use the compact RPN VM.
    std::vector<ConditionInstruction> condition;
    std::vector<CompiledEffectCall> effects;
    // ThunderScript 2.0 scope traversal and iterator trees. These remain empty for
    // simple scripts so existing hot-path performance is retained.
    std::vector<CompiledScopedCondition> scoped_conditions;
    std::vector<CompiledScopedEffect> scoped_effects;
};

struct ScriptedValueProgram {
    SymbolId name{};
    ScopeType scope = ScopeType::None;
    ValueSource source = ValueSource::Treasury;
    double multiply = 1.0;
    double add = 0.0;
    CompiledScriptArgument runtime_source{};
    std::vector<ScriptParameterDefinition> parameters;
    std::uint32_t source_line = 0u;
    // Generic bytecode: when non-empty, VM evaluates bytecode instead of
    // legacy source*mul+add. Keeps old programs working unchanged.
    ScriptValueBytecode bytecode{};
    bool uses_bytecode = false;
};

struct CompiledHistoryPatch {
    SymbolId target{};
    std::int32_t yyyymmdd = 0;
    ScopeType scope = ScopeType::Country;
    std::vector<CompiledEffectCall> effects;
};

struct ScriptCompileDiagnostic {
    std::string message;
    std::uint32_t line = 0;
};

class ScriptProgramDatabase {
public:
    void reserve(std::size_t scripts, std::size_t values, std::size_t history);
    void add(ScriptProgram program);
    void add(ScriptedValueProgram program);
    void add(CompiledHistoryPatch patch);

    [[nodiscard]] bool validate_links(const SymbolTable& symbols,
                                      std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    [[nodiscard]] const ScriptProgram* find_script(SymbolId name) const noexcept;
    [[nodiscard]] const ScriptedValueProgram* find_value(SymbolId name) const noexcept;
    [[nodiscard]] std::span<const CompiledHistoryPatch> history() const noexcept { return history_; }
    [[nodiscard]] std::size_t script_count() const noexcept { return scripts_.size(); }
    [[nodiscard]] std::size_t value_count() const noexcept { return values_.size(); }
    [[nodiscard]] std::size_t instruction_bytes() const noexcept;

private:
    std::vector<ScriptProgram> scripts_;
    std::vector<ScriptedValueProgram> values_;
    std::vector<CompiledHistoryPatch> history_;
    std::unordered_map<std::uint32_t, std::uint32_t> script_lookup_;
    std::unordered_map<std::uint32_t, std::uint32_t> value_lookup_;
};

class ScriptCompiler {
public:
    ScriptCompiler(SymbolTable& symbols, const ScriptRegistry& registry);

    [[nodiscard]] bool compile(const ScriptParseResult& parsed, ScriptProgramDatabase& out,
                               std::vector<ScriptCompileDiagnostic>& diagnostics) const;

private:
    struct CompileScope {
        ScopeType type = ScopeType::None;
        bool known = false;
    };

    [[nodiscard]] ScopeType parse_scope(const ScriptNode& node) const noexcept;
    [[nodiscard]] bool is_advanced_condition(const ScriptNode& node) const;
    [[nodiscard]] bool is_advanced_effect(const ScriptNode& node) const;
    [[nodiscard]] bool parse_scope_selector(std::string_view text, ScopeSelector& selector,
                                            CompileScope current, ScopeType root_scope,
                                            CompileScope& nested) const;
    [[nodiscard]] bool parse_iterator(std::string_view text, ScopeIteratorMode& mode,
                                      ScopeType& target) const noexcept;
    [[nodiscard]] bool parse_collection_iterator(std::string_view text, ScopeIteratorMode& mode,
                                                 ScriptStableKey& collection) const noexcept;
    [[nodiscard]] ScopeType value_source_scope(ValueSource source) const noexcept;
    bool compile_argument(const ScriptNode& node, CompiledScriptArgument& out,
                          std::vector<ScriptCompileDiagnostic>& diagnostics,
                          std::string_view description) const;
    bool compile_script_call(const ScriptNode& node, SymbolId& script_name,
                             std::vector<CompiledNamedArgument>& arguments,
                             std::vector<ScriptCompileDiagnostic>& diagnostics,
                             std::string_view description) const;
    bool compile_parameters(const ScriptNode& node,
                            std::vector<ScriptParameterDefinition>& parameters,
                            std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool track_stable_name(std::string_view name, std::uint32_t line,
                           std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    bool compile_condition_block(const ScriptNode& block, ScopeType scope, ConditionOp combine,
                                 std::vector<ConditionInstruction>& code,
                                 std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_condition_node(const ScriptNode& node, ScopeType scope,
                                std::vector<ConditionInstruction>& code,
                                std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_fast_all(const ScriptNode& block, ScopeType scope,
                          std::vector<CompiledTriggerCall>& calls,
                          std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_effects(const ScriptNode& block, ScopeType scope,
                         std::vector<CompiledEffectCall>& effects,
                         std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_condition_node(const ScriptNode& node, CompileScope scope, ScopeType root_scope,
                                       CompiledScopedCondition& out,
                                       std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_condition_block(const ScriptNode& block, CompileScope scope, ScopeType root_scope,
                                        std::vector<CompiledScopedCondition>& out,
                                        std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_effect_node(const ScriptNode& node, CompileScope scope, ScopeType root_scope,
                                    CompiledScopedEffect& out,
                                    std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_effect_block(const ScriptNode& block, CompileScope scope, ScopeType root_scope,
                                     std::vector<CompiledScopedEffect>& out,
                                     std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    // Lowers a symbolic expression tree (ScriptValueKind::Expression) into RPN
    // value bytecode. Operands: builtin value sources, `value:<name>` scripted
    // values, `var:<name>` variables and numeric constants.
    bool compile_value_expression(const ScriptNode& node, CompileScope scope,
                                  ScriptValueBytecode& out,
                                  std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    SymbolTable& symbols_;
    const ScriptRegistry& registry_;
    SymbolId sym_script_{};
    SymbolId sym_scripted_value_{};
    SymbolId sym_history_{};
    SymbolId sym_scope_{};
    SymbolId sym_trigger_{};
    SymbolId sym_effect_{};
    SymbolId sym_source_{};
    SymbolId sym_multiply_{};
    SymbolId sym_add_{};
    SymbolId sym_expression_{};
    SymbolId sym_date_{};
    SymbolId sym_all_{};
    SymbolId sym_any_{};
    SymbolId sym_not_{};
    SymbolId sym_save_scope_as_{};
    SymbolId sym_scripted_trigger_{};
    SymbolId sym_scripted_effect_{};
    SymbolId sym_name_{};
    SymbolId sym_value_{};
    SymbolId sym_parameters_{};
    SymbolId sym_type_{};
    SymbolId sym_required_{};
    SymbolId sym_default_{};
    mutable std::unordered_map<ScriptStableKey, std::string> stable_names_;
};

class ScriptVm {
public:
    explicit ScriptVm(const ScriptRegistry& registry, const ScriptProgramDatabase* programs = nullptr)
        : registry_(registry), programs_(programs) {}
    void set_program_database(const ScriptProgramDatabase* programs) noexcept { programs_ = programs; }

    [[nodiscard]] bool evaluate(const ScriptProgram& program, const World& world, ScopeRef scope) const;
    [[nodiscard]] bool evaluate(const ScriptProgram& program, const World& world,
                                ScriptExecutionContext context) const;
    bool execute_if(const ScriptProgram& program, World& world, ScopeRef scope,
                    ScopeRef from = {}, std::uint64_t random_seed = 0) const;
    bool execute_if(const ScriptProgram& program, World& world, ScriptExecutionContext& context) const;
    [[nodiscard]] double evaluate(const ScriptedValueProgram& program, const World& world, ScopeRef scope) const;
    [[nodiscard]] double evaluate(const ScriptedValueProgram& program, const World& world,
                                  ScriptExecutionContext context) const;
    [[nodiscard]] double evaluate_value(SymbolId name, const World& world,
                                        ScriptExecutionContext context,
                                        std::span<const ScriptNamedValue> arguments = {}) const;
    void apply(std::span<const CompiledEffectCall> effects, World& world, ScopeRef scope) const;
    void apply_program_effects(const ScriptProgram& program, World& world, ScriptExecutionContext& context) const;

private:
    [[nodiscard]] bool evaluate_classic(const ScriptProgram& program, const World& world,
                                        ScriptExecutionContext& context) const;
    [[nodiscard]] bool evaluate_scoped_nodes(std::span<const CompiledScopedCondition> nodes,
                                             const World& world, ScriptExecutionContext& context,
                                             std::uint32_t depth) const;
    [[nodiscard]] bool evaluate_scoped_node(const CompiledScopedCondition& node, const World& world,
                                            ScriptExecutionContext& context, std::uint32_t depth) const;
    void apply_scoped_nodes(std::span<const CompiledScopedEffect> nodes, World& world,
                            ScriptExecutionContext& context, std::uint32_t depth) const;
    void apply_scoped_node(const CompiledScopedEffect& node, World& world,
                           ScriptExecutionContext& context, std::uint32_t depth) const;
    [[nodiscard]] ScopeRef resolve_selector(const ScopeSelector& selector, const World& world,
                                            const ScriptExecutionContext& context) const noexcept;
    [[nodiscard]] std::size_t deterministic_index(ScriptExecutionContext& context,
                                                  std::uint64_t salt, ScopeType target,
                                                  std::size_t count,
                                                  ScriptStableKey collection = 0) const noexcept;
    // Raw keyed draw for weighted sampling (random_list): same hash recipe as
    // deterministic_index but returns the full 64-bit value for caller-side
    // modulo against a dynamic weight total.
    [[nodiscard]] std::uint64_t deterministic_draw(ScriptExecutionContext& context,
                                                   std::uint64_t salt) const noexcept;
    // Stack-machine evaluation shared by scripted_value programs and
    // SourceCompare condition operands.
    [[nodiscard]] double eval_value_bytecode(const ScriptValueBytecode& bytecode,
                                             const World& world,
                                             ScriptExecutionContext& context,
                                             std::uint32_t depth) const;
    [[nodiscard]] std::optional<ScriptArgument> resolve_argument(
        const CompiledScriptArgument& argument, const World& world,
        ScriptExecutionContext& context, std::uint32_t depth) const;
    [[nodiscard]] bool resolve_call_arguments(std::span<const CompiledNamedArgument> arguments,
                                              const ScriptProgram& called,
                                              const World& world, ScriptExecutionContext& context,
                                              std::uint32_t depth,
                                              std::vector<ScriptNamedValue>& out) const;
    [[nodiscard]] bool prepare_invocation(const ScriptProgram& program,
                                          const World& world,
                                          ScriptExecutionContext& context) const;
    [[nodiscard]] bool prepare_value_invocation(const ScriptedValueProgram& program,
                                                const World& world,
                                                ScriptExecutionContext& context) const;
    [[nodiscard]] double evaluate_value_internal(const ScriptedValueProgram& program,
                                                 const World& world,
                                                 ScriptExecutionContext& context,
                                                 std::uint32_t depth) const;

    const ScriptRegistry& registry_;
    const ScriptProgramDatabase* programs_ = nullptr;
};

struct WeightedRandomEntry {
    std::uint32_t base_weight = 100u;
    std::uint64_t branch_id = 0u;
    std::int32_t modifier_ppm = 0;
};

struct WeightedRandomList {
    std::vector<WeightedRandomEntry> entries;
    [[nodiscard]] std::size_t sample(std::uint64_t random_val) const noexcept {
        if (entries.empty()) return 0;
        std::uint64_t total_weight = 0;
        for (const auto& e : entries) {
            std::int64_t w = static_cast<std::int64_t>(e.base_weight);
            w += (w * e.modifier_ppm) / 1'000'000;
            total_weight += static_cast<std::uint64_t>(std::max<std::int64_t>(0, w));
        }
        if (total_weight == 0) return 0;
        std::uint64_t draw = random_val % total_weight;
        std::uint64_t accum = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            std::int64_t w = static_cast<std::int64_t>(entries[i].base_weight);
            w += (w * entries[i].modifier_ppm) / 1'000'000;
            accum += static_cast<std::uint64_t>(std::max<std::int64_t>(0, w));
            if (draw < accum) return i;
        }
        return entries.size() - 1;
    }
};

struct ScriptProfileRecord {
    std::uint64_t script_hash = 0;
    std::uint64_t invocations = 0;
    std::uint64_t total_nanoseconds = 0;
    std::uint64_t max_nanoseconds = 0;
};

class ScriptProfiler {
public:
    void record(std::uint64_t script_hash, std::uint64_t duration_ns) noexcept {
        for (auto& r : records_) {
            if (r.script_hash == script_hash) {
                ++r.invocations;
                r.total_nanoseconds += duration_ns;
                if (duration_ns > r.max_nanoseconds) r.max_nanoseconds = duration_ns;
                return;
            }
        }
        records_.push_back({script_hash, 1, duration_ns, duration_ns});
    }

    void reset() noexcept { records_.clear(); }
    [[nodiscard]] std::span<const ScriptProfileRecord> records() const noexcept { return records_; }
    [[nodiscard]] std::string dump_flamegraph_json() const;

private:
    std::vector<ScriptProfileRecord> records_;
};

} // namespace thunder
