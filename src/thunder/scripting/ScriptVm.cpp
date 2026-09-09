#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thunder {
namespace {

class ScopeEnterGuard {
public:
    ScopeEnterGuard(ScriptExecutionContext& context, ScopeRef next) : context_(context) { context_.enter(next); }
    ScopeEnterGuard(const ScopeEnterGuard&) = delete;
    ScopeEnterGuard& operator=(const ScopeEnterGuard&) = delete;
    ~ScopeEnterGuard() { context_.leave(); }
private:
    ScriptExecutionContext& context_;
};

class ScriptCallGuard {
public:
    ScriptCallGuard(ScriptExecutionContext& context, std::span<const ScriptNamedValue> arguments)
        : context_(context) {
        // Public contexts may be aggregate-initialized. Materialize their root frame
        // before pushing the callee so callee parameters never leak after the guard.
        if (context_.call_depth() == 0u) context_.push_call_frame();
        context_.push_call_frame(arguments);
    }
    ScriptCallGuard(const ScriptCallGuard&) = delete;
    ScriptCallGuard& operator=(const ScriptCallGuard&) = delete;
    ~ScriptCallGuard() { context_.pop_call_frame(); }
private:
    ScriptExecutionContext& context_;
};

bool parameter_value_matches(const ScriptParameterDefinition& parameter,
                             const ScriptArgument& value,
                             const World& world) noexcept {
    if (value.kind != parameter.kind) return false;
    if (value.kind != ScriptArgumentKind::Scope) return true;
    return ScopeResolver::valid(world, value.scope_value) &&
        (parameter.scope == ScopeType::None || value.scope_value.type == parameter.scope);
}

bool bind_parameter_schema(std::span<const ScriptParameterDefinition> schema,
                           std::span<const ScriptNamedValue> supplied,
                           const World& world,
                           std::vector<ScriptNamedValue>& bound) {
    bound.clear();
    if (schema.empty()) {
        bound.assign(supplied.begin(), supplied.end());
        return true;
    }
    for (std::size_t i = 1u; i < supplied.size(); ++i)
        if (supplied[i - 1u].name >= supplied[i].name) return false;
    bound.reserve(schema.size());
    std::size_t supplied_index = 0u;
    for (const auto& parameter : schema) {
        while (supplied_index < supplied.size() &&
               supplied[supplied_index].name < parameter.key) return false;
        if (supplied_index < supplied.size() && supplied[supplied_index].name == parameter.key) {
            if (!parameter_value_matches(parameter, supplied[supplied_index].value, world)) return false;
            bound.push_back(supplied[supplied_index++]);
        } else if (parameter.default_value) {
            bound.push_back({parameter.key, *parameter.default_value});
        } else if (parameter.required) {
            return false;
        }
    }
    return supplied_index == supplied.size();
}

constexpr std::uint32_t kMaxScriptDepth = 64u;



} // namespace

bool ScriptVm::evaluate_classic(const ScriptProgram& program, const World& world,
                                ScriptExecutionContext& context) const {
    const auto work = !program.fast_all.empty() ? program.fast_all.size() : program.condition.size();
    if (!context.consume_work(static_cast<std::uint64_t>(std::max<std::size_t>(1u, work)))) {
        throw std::runtime_error("ThunderScript execution budget exceeded");
    }
    const auto scope = context.current;
    if (!program.fast_all.empty() || program.condition.empty()) {
        for (const auto& call : program.fast_all) {
            if (!registry_.evaluate_trigger(call.primitive, world, scope, call.argument)) return false;
        }
        return true;
    }
    // Size the stack from the compiled program: each instruction pushes at
    // most one value, so this can never overflow. The previous fixed 256-slot
    // array threw "condition stack overflow" at runtime for blocks that
    // compiled fine with more than 256 leaves, taking down the whole tick.
    //
    // A thread-local `char` buffer is reused across evaluations: the per-call
    // `std::vector<bool>` heap allocation (malloc+free, plus the bit-packed
    // proxy's slow random access) dominated trigger evaluation time for
    // non-flat conditions. `char` is used instead of `bool` because
    // `std::vector<bool>` is the standards-mandated bit-packed specialisation,
    // which is materially slower per access than a byte vector. Stack
    // discipline (push-before-read) makes stale bytes harmless, so only the
    // grown region needs initialising.
    thread_local std::vector<char> stack;
    const std::size_t stack_size = program.condition.size() + 1u;
    if (stack.size() < stack_size) stack.resize(stack_size, 0);
    std::size_t sp = 0;
    for (const auto& instruction : program.condition) {
        switch (instruction.op) {
            case ConditionOp::PushTrue:
                if (sp >= stack_size) throw std::runtime_error("condition stack overflow");
                stack[sp++] = 1;
                break;
            case ConditionOp::CallTrigger:
                if (sp >= stack_size) throw std::runtime_error("condition stack overflow");
                stack[sp++] = registry_.evaluate_trigger(instruction.primitive, world, scope, instruction.argument) ? 1 : 0;
                break;
            case ConditionOp::And:
            case ConditionOp::Or: {
                if (sp < 2u) throw std::runtime_error("invalid condition bytecode stack underflow");
                const bool rhs = stack[--sp] != 0;
                const bool lhs = stack[sp - 1u] != 0;
                stack[sp - 1u] = (instruction.op == ConditionOp::And ? (lhs && rhs) : (lhs || rhs)) ? 1 : 0;
                break;
            }
            case ConditionOp::Not:
                if (sp < 1u) throw std::runtime_error("invalid condition bytecode stack underflow");
                stack[sp - 1u] = stack[sp - 1u] != 0 ? 0 : 1;
                break;
        }
    }
    if (sp != 1u) throw std::runtime_error("invalid condition bytecode final stack");
    return stack[0] != 0;
}

bool ScriptVm::evaluate(const ScriptProgram& program, const World& world, ScopeRef scope) const {
    if (program.scope != scope.type) throw std::runtime_error("program scope mismatch");
    if (!ScopeResolver::valid(world, scope)) return false;
    if (program.scoped_conditions.empty() && program.parameters.empty() && !program.fast_all.empty()) {
        for (const auto& call : program.fast_all) {
            if (!registry_.evaluate_trigger(call.primitive, world, scope, call.argument)) return false;
        }
        return true;
    }
    auto context = ScriptExecutionContext::rooted(scope);
    return evaluate(program, world, std::move(context));
}

bool ScriptVm::evaluate(const ScriptProgram& program, const World& world,
                        ScriptExecutionContext context) const {
    context.begin_execution();
    if (program.scope != context.current.type) throw std::runtime_error("program scope mismatch");
    if (!ScopeResolver::valid(world, context.current)) return false;
    if (!prepare_invocation(program, world, context)) return false;
    if (!program.scoped_conditions.empty()) {
        return evaluate_scoped_nodes(program.scoped_conditions, world, context, 0u);
    }
    return evaluate_classic(program, world, context);
}

bool ScriptVm::execute_if(const ScriptProgram& program, World& world, ScopeRef scope,
                          ScopeRef from, std::uint64_t random_seed) const {
    auto context = ScriptExecutionContext::rooted(scope, from, random_seed);
    return execute_if(program, world, context);
}

bool ScriptVm::execute_if(const ScriptProgram& program, World& world,
                          ScriptExecutionContext& context) const {
    context.begin_execution();
    if (program.scope != context.current.type) throw std::runtime_error("program scope mismatch");
    if (!ScopeResolver::valid(world, context.current)) return false;
    if (!prepare_invocation(program, world, context)) return false;
    bool passes = false;
    if (!program.scoped_conditions.empty()) passes = evaluate_scoped_nodes(program.scoped_conditions, world, context, 0u);
    else passes = evaluate_classic(program, world, context);
    if (!passes) return false;
    apply_program_effects(program, world, context);
    return true;
}

ScopeRef ScriptVm::resolve_selector(const ScopeSelector& selector, const World& world,
                                    const ScriptExecutionContext& context) const noexcept {
    switch (selector.kind) {
        case ScopeSelectorKind::This: return context.current;
        case ScopeSelectorKind::Root: return context.root;
        case ScopeSelectorKind::From: return context.from;
        case ScopeSelectorKind::Prev: return context.prev();
        case ScopeSelectorKind::Owner:
        case ScopeSelectorKind::Country: return ScopeResolver::owner(world, context.current);
        case ScopeSelectorKind::Market: return ScopeResolver::market(world, context.current);
        case ScopeSelectorKind::State: return ScopeResolver::state(world, context.current);
        case ScopeSelectorKind::Province: return ScopeResolver::province(world, context.current);
        case ScopeSelectorKind::Saved: return context.saved(selector.saved_name);
    }
    return {};
}

std::size_t ScriptVm::deterministic_index(ScriptExecutionContext& context, std::uint64_t salt,
                                          ScopeType target, std::size_t count,
                                          ScriptStableKey collection) const noexcept {
    if (count == 0u) return 0u;
    // Keep this recipe byte-stable: random_* iterator draws are replay inputs.
    Fnv1a64 hash;
    hash.add(context.random_seed);
    hash.add(static_cast<std::uint8_t>(context.root.type));
    hash.add(context.root.raw_id);
    hash.add(static_cast<std::uint8_t>(context.current.type));
    hash.add(context.current.raw_id);
    hash.add(static_cast<std::uint8_t>(target));
    hash.add(collection);
    hash.add(salt);
    hash.add(context.consume_random_draw(salt));
    const auto depth = static_cast<std::uint32_t>(context.previous.size());
    hash.add(depth);
    return static_cast<std::size_t>(hash.value() % static_cast<std::uint64_t>(count));
}

std::uint64_t ScriptVm::deterministic_draw(ScriptExecutionContext& context, std::uint64_t salt) const noexcept {
    // Weighted sampling (random_list) wants a full-range value rather than an
    // index; distinct domain separator keeps both draw families independent.
    Fnv1a64 hash;
    hash.add(context.random_seed);
    hash.add(static_cast<std::uint8_t>(context.root.type));
    hash.add(context.root.raw_id);
    hash.add(static_cast<std::uint8_t>(context.current.type));
    hash.add(context.current.raw_id);
    hash.add(salt);
    hash.add(context.consume_random_draw(salt));
    const auto depth = static_cast<std::uint32_t>(context.previous.size());
    hash.add(depth);
    return hash.value();
}

std::optional<ScriptArgument> ScriptVm::resolve_argument(const CompiledScriptArgument& argument,
                                                         const World& world,
                                                         ScriptExecutionContext& context,
                                                         std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("ThunderScript maximum call depth exceeded");
    switch (argument.source) {
        case ScriptArgumentSourceKind::Literal:
            return argument.literal.valid() ? std::optional<ScriptArgument>{argument.literal} : std::nullopt;
        case ScriptArgumentSourceKind::Variable: {
            const auto value = context.variable(argument.key);
            return value.valid() ? std::optional<ScriptArgument>{value} : std::nullopt;
        }
        case ScriptArgumentSourceKind::Parameter: {
            const auto value = context.parameter(argument.key);
            return value.valid() ? std::optional<ScriptArgument>{value} : std::nullopt;
        }
        case ScriptArgumentSourceKind::EventTarget: {
            const auto scope = context.event_target(argument.key);
            return scope.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(scope)} : std::nullopt;
        }
        case ScriptArgumentSourceKind::ThisScope:
            return context.current.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(context.current)} : std::nullopt;
        case ScriptArgumentSourceKind::RootScope:
            return context.root.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(context.root)} : std::nullopt;
        case ScriptArgumentSourceKind::FromScope:
            return context.from.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(context.from)} : std::nullopt;
        case ScriptArgumentSourceKind::PrevScope: {
            const auto scope = context.prev();
            return scope.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(scope)} : std::nullopt;
        }
        case ScriptArgumentSourceKind::ScriptedValue: {
            if (programs_ == nullptr) throw std::runtime_error("scripted value reference requires ScriptProgramDatabase");
            const auto* value = programs_->find_value(argument.script_name);
            if (value == nullptr) throw std::runtime_error("scripted value reference is unknown");
            if (context.calls.empty()) context.calls.emplace_back();
            std::vector<ScriptNamedValue> projected;
            if (value->parameters.empty()) {
                projected = context.calls.back().parameters;
            } else {
                projected.reserve(value->parameters.size());
                for (const auto& parameter : value->parameters) {
                    const auto found = std::lower_bound(
                        context.calls.back().parameters.begin(),
                        context.calls.back().parameters.end(), parameter.key,
                        [](const ScriptNamedValue& supplied, ScriptStableKey key) {
                            return supplied.name < key;
                        });
                    if (found != context.calls.back().parameters.end() &&
                        found->name == parameter.key) projected.push_back(*found);
                }
            }
            std::vector<ScriptNamedValue> bound;
            if (!bind_parameter_schema(value->parameters, projected, world, bound)) return std::nullopt;
            ScriptCallGuard guard{context, bound};
            return ScriptArgument::numeric(
                evaluate_value_internal(*value, world, context, depth + 1u));
        }
    }
    return std::nullopt;
}



bool ScriptVm::resolve_call_arguments(std::span<const CompiledNamedArgument> arguments,
                                      const ScriptProgram& called,
                                      const World& world, ScriptExecutionContext& context,
                                      std::uint32_t depth,
                                      std::vector<ScriptNamedValue>& out) const {
    std::vector<ScriptNamedValue> resolved;
    resolved.reserve(arguments.size());
    for (const auto& argument : arguments) {
        const auto value = resolve_argument(argument.value, world, context, depth + 1u);
        if (!value.has_value()) return false;
        resolved.push_back({argument.name, *value});
    }
    return bind_parameter_schema(called.parameters, resolved, world, out);
}

bool ScriptVm::prepare_invocation(const ScriptProgram& program, const World& world,
                                  ScriptExecutionContext& context) const {
    if (context.calls.empty()) context.calls.emplace_back();
    std::vector<ScriptNamedValue> bound;
    if (!bind_parameter_schema(program.parameters, context.calls.back().parameters,
                               world, bound)) return false;
    context.calls.back().parameters = std::move(bound);
    return true;
}

bool ScriptVm::prepare_value_invocation(const ScriptedValueProgram& program,
                                        const World& world,
                                        ScriptExecutionContext& context) const {
    if (context.calls.empty()) context.calls.emplace_back();
    std::vector<ScriptNamedValue> bound;
    if (!bind_parameter_schema(program.parameters, context.calls.back().parameters,
                               world, bound)) return false;
    context.calls.back().parameters = std::move(bound);
    return true;
}

bool ScriptVm::evaluate_scoped_nodes(std::span<const CompiledScopedCondition> nodes,
                                     const World& world, ScriptExecutionContext& context,
                                     std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("ThunderScript maximum call depth exceeded");
    for (const auto& node : nodes) {
        if (!evaluate_scoped_node(node, world, context, depth + 1u)) return false;
    }
    return true;
}

bool ScriptVm::evaluate_scoped_node(const CompiledScopedCondition& node, const World& world,
                                    ScriptExecutionContext& context, std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("ThunderScript maximum call depth exceeded");
    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
    switch (node.kind) {
        case ScopedConditionKind::Trigger:
            if (!ScopeResolver::valid(world, context.current)) return false;
            if (registry_.trigger_scope(node.primitive) != context.current.type) return false;
            if (const auto argument = resolve_argument(node.argument, world, context, depth + 1u)) {
                if (!registry_.trigger_accepts_argument(node.primitive, argument->kind)) return false;
                return registry_.evaluate_trigger(node.primitive, world, context.current, *argument);
            }
            return false;
        case ScopedConditionKind::ScriptCall: {
            if (programs_ == nullptr) throw std::runtime_error("scripted_trigger requires ScriptProgramDatabase");
            const auto* called = programs_->find_script(node.script_name);
            if (called == nullptr) throw std::runtime_error("scripted_trigger references unknown script");
            if (called->scope != context.current.type) return false;
            std::vector<ScriptNamedValue> arguments;
            if (!resolve_call_arguments(node.arguments, *called, world, context,
                                        depth + 1u, arguments)) return false;
            ScriptCallGuard call_guard{context, arguments};
            if (!called->scoped_conditions.empty()) {
                return evaluate_scoped_nodes(called->scoped_conditions, world, context, depth + 1u);
            }
            return evaluate_classic(*called, world, context);
        }
        case ScopedConditionKind::All:
            return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        case ScopedConditionKind::Any:
            for (const auto& child : node.children) {
                if (evaluate_scoped_node(child, world, context, depth + 1u)) return true;
            }
            return node.children.empty();
        case ScopedConditionKind::Not:
            return !evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        case ScopedConditionKind::Scope: {
            const auto next = resolve_selector(node.selector, world, context);
            if (!ScopeResolver::valid(world, next)) return false;
            ScopeEnterGuard guard{context, next};
            return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        }
        case ScopedConditionKind::Iterator: {
            // `limit = { <conditions> }` candidate filter (non-empty
            // iterator_filter): failing candidates are excluded from the
            // iteration, whatever the surrounding any/every/ordered/random
            // mode is. Evaluated in the candidate's own scope.
            const auto passes_iterator_filter = [&](ScopeRef candidate) {
                if (node.iterator_filter.empty()) return true;
                ScopeEnterGuard guard{context, candidate};
                return evaluate_scoped_nodes(node.iterator_filter, world, context, depth + 1u);
            };
            if (node.iterator_source == ScopeIteratorSource::Collection) {
                const auto values = context.collection(node.collection_name);
                const auto evaluate_value = [&](const ScriptArgument& value) {
                    if (value.kind != ScriptArgumentKind::Scope ||
                        !ScopeResolver::valid(world, value.scope_value)) return false;
                    if (!passes_iterator_filter(value.scope_value)) return false;
                    ScopeEnterGuard guard{context, value.scope_value};
                    return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
                };
                if (node.iterator_mode == ScopeIteratorMode::Any) {
                    for (const auto& value : values) {
                        if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                        if (evaluate_value(value)) return true;
                    }
                    return false;
                }
                if (node.iterator_mode == ScopeIteratorMode::Every) {
                    for (const auto& value : values) {
                        if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                        if (!evaluate_value(value)) return false;
                    }
                    return true;
                }
                if (values.empty()) return false;
                if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                const auto index = deterministic_index(context, node.salt, ScopeType::None,
                                                       values.size(), node.collection_name);
                return evaluate_value(values[index]);
            }
            auto candidates = ScopeResolver::children(world, context.current, node.iterator_target);
            if (!node.iterator_filter.empty()) {
                std::vector<ScopeRef> passing;
                passing.reserve(candidates.size());
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    if (passes_iterator_filter(candidate)) passing.push_back(candidate);
                }
                candidates = std::move(passing);
            }
            if (node.iterator_mode == ScopeIteratorMode::Any) {
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    ScopeEnterGuard guard{context, candidate};
                    if (evaluate_scoped_nodes(node.children, world, context, depth + 1u)) return true;
                }
                return false;
            }
            if (node.iterator_mode == ScopeIteratorMode::Every) {
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    ScopeEnterGuard guard{context, candidate};
                    if (!evaluate_scoped_nodes(node.children, world, context, depth + 1u)) return false;
                }
                return true;
            }
            if (node.iterator_mode == ScopeIteratorMode::Ordered) {
                if (candidates.empty()) return false;
                // Sort by scripted value if order_by present, else stable by id
                std::vector<std::pair<double, ScopeRef>> scored;
                scored.reserve(candidates.size());
                for (auto c : candidates) {
                    // Charge the ORDER BY evaluation to the parent budget. This
                    // used to copy the context and call begin_execution(),
                    // which reset the budget to the default and then discarded
                    // the spend with the copy — so a wide ordered_* iterator
                    // could consume unbounded work and never trip the limit.
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    double score = static_cast<double>(c.raw_id);
                    if (node.iterator_config.has_order_by && programs_ != nullptr) {
                        if (auto *sv = programs_->find_value(node.iterator_config.order_by_value)) {
                            ScopeEnterGuard guard{context, c};
                            try { score = evaluate_value_internal(*sv, world, context, depth+1u); } catch(...) { score = 0; }
                        }
                    }
                    scored.emplace_back(score, c);
                }
                std::sort(scored.begin(), scored.end(), [&](auto &a, auto &b){
                    if (a.first != b.first) return node.iterator_config.descending ? a.first > b.first : a.first < b.first;
                    return a.second.raw_id < b.second.raw_id;
                });
                std::size_t offset = std::min<std::size_t>(node.iterator_config.offset, scored.size());
                std::size_t limit = node.iterator_config.has_limit ? std::min<std::size_t>(node.iterator_config.limit, scored.size()-offset) : scored.size()-offset;
                // `ordered_` is a windowed, ordered iteration primitive and must
                // mean the same thing on the condition and effect sides: apply
                // the block to *every* element in the sorted/limited window.
                // The previous code short-circuited on the first match ("any"
                // semantics), silently contradicting the effect side (which
                // always touches every element in the window) and making
                // `ordered_X` under a trigger behave like `any_X`.
                bool all_match = true;
                for (std::size_t i = offset; i < offset + limit; ++i) {
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    ScopeEnterGuard guard{context, scored[i].second};
                    if (!evaluate_scoped_nodes(node.children, world, context, depth + 1u)) {
                        all_match = false;
                        break;
                    }
                }
                return all_match;
            }
            if (candidates.empty()) return false;
            if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
            const auto index = deterministic_index(context, node.salt, node.iterator_target, candidates.size());
            ScopeEnterGuard guard{context, candidates[index]};
            return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        }
        case ScopedConditionKind::SourceCompare: {
            // Infix trigger comparison: both sides are value bytecode evaluated
            // in the current scope. Tolerance semantics match the bytecode
            // comparison ops (Eq/Ne 1e-6, Le/Ge epsilon-inclusive).
            if (!ScopeResolver::valid(world, context.current)) return false;
            const double lhs = eval_value_bytecode(node.compare_lhs, world, context, depth + 1u);
            const double rhs = eval_value_bytecode(node.compare_rhs, world, context, depth + 1u);
            switch (node.comparison) {
                case ScriptComparison::Equal: return std::abs(lhs - rhs) < 1e-6;
                case ScriptComparison::NotEqual: return std::abs(lhs - rhs) >= 1e-6;
                case ScriptComparison::Above: return lhs > rhs;
                case ScriptComparison::AtLeast: return lhs >= rhs - 1e-6;
                case ScriptComparison::Below: return lhs < rhs;
                case ScriptComparison::AtMost: return lhs <= rhs + 1e-6;
            }
            return false;
        }
        case ScopedConditionKind::HasVariable:
            return context.has_variable(node.binding_name);
        case ScopedConditionKind::CompareVariable: {
            const auto lhs = context.variable(node.binding_name);
            if (!lhs.valid()) return false;
            const auto rhs = resolve_argument(node.argument, world, context, depth + 1u);
            if (!rhs.has_value()) return false;
            if (node.comparison == ScriptComparison::Equal) return lhs == *rhs;
            if (node.comparison == ScriptComparison::NotEqual) return !(lhs == *rhs);
            if (lhs.kind != ScriptArgumentKind::Number || rhs->kind != ScriptArgumentKind::Number) return false;
            switch (node.comparison) {
                case ScriptComparison::Above: return lhs.number > rhs->number;
                case ScriptComparison::AtLeast: return lhs.number >= rhs->number;
                case ScriptComparison::Below: return lhs.number < rhs->number;
                case ScriptComparison::AtMost: return lhs.number <= rhs->number;
                case ScriptComparison::Equal:
                case ScriptComparison::NotEqual: break;
            }
            return false;
        }
    }
    return false;
}

void ScriptVm::apply_program_effects(const ScriptProgram& program, World& world,
                                     ScriptExecutionContext& context) const {
    if (!program.scoped_effects.empty()) apply_scoped_nodes(program.scoped_effects, world, context, 0u);
    else {
        if (!context.consume_work(static_cast<std::uint64_t>(program.effects.size()))) {
            throw std::runtime_error("ThunderScript execution budget exceeded");
        }
        apply(program.effects, world, context.current);
    }
}



void ScriptVm::apply_scoped_nodes(std::span<const CompiledScopedEffect> nodes, World& world,
                                  ScriptExecutionContext& context, std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("ThunderScript maximum call depth exceeded");
    for (const auto& node : nodes) apply_scoped_node(node, world, context, depth + 1u);
}

void ScriptVm::apply_scoped_node(const CompiledScopedEffect& node, World& world,
                                 ScriptExecutionContext& context, std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("ThunderScript maximum call depth exceeded");
    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
    switch (node.kind) {
        case ScopedEffectKind::Effect:
            if (!ScopeResolver::valid(world, context.current)) return;
            if (registry_.effect_scope(node.primitive) != context.current.type) {
                throw std::runtime_error("effect scope mismatch at runtime");
            }
            if (const auto argument = resolve_argument(node.argument, world, context, depth + 1u)) {
                if (!registry_.effect_accepts_argument(node.primitive, argument->kind))
                    throw std::runtime_error("effect argument kind mismatch at runtime");
                registry_.execute_effect(node.primitive, world, context.current, *argument);
            } else {
                throw std::runtime_error("effect argument reference is unset");
            }
            return;
        case ScopedEffectKind::ScriptCall: {
            if (programs_ == nullptr) throw std::runtime_error("scripted_effect requires ScriptProgramDatabase");
            const auto* called = programs_->find_script(node.script_name);
            if (called == nullptr) throw std::runtime_error("scripted_effect references unknown script");
            if (called->scope != context.current.type) throw std::runtime_error("scripted_effect scope mismatch");
            std::vector<ScriptNamedValue> arguments;
            if (!resolve_call_arguments(node.arguments, *called, world, context,
                                        depth + 1u, arguments))
                throw std::runtime_error("scripted_effect argument reference is unset");
            ScriptCallGuard call_guard{context, arguments};
            apply_program_effects(*called, world, context);
            return;
        }
        case ScopedEffectKind::Scope: {
            const auto next = resolve_selector(node.selector, world, context);
            if (!ScopeResolver::valid(world, next)) return;
            ScopeEnterGuard guard{context, next};
            apply_scoped_nodes(node.children, world, context, depth + 1u);
            return;
        }
        case ScopedEffectKind::Iterator: {
            // `limit = { <conditions> }` candidate filter, evaluated in the
            // candidate's own scope. Candidates failing it are invisible to
            // every/ordered/random semantics alike (Clausewitz-style gating).
            const auto passes_iterator_filter = [&](ScopeRef candidate) {
                if (!node.has_iterator_condition) return true;
                ScopeEnterGuard guard{context, candidate};
                return evaluate_scoped_nodes(node.condition.children, world, context, depth + 1u);
            };
            if (node.iterator_source == ScopeIteratorSource::Collection) {
                const auto span = context.collection(node.collection_name);
                const std::vector<ScriptArgument> values{span.begin(), span.end()};
                const auto apply_value = [&](const ScriptArgument& value) {
                    if (value.kind != ScriptArgumentKind::Scope ||
                        !ScopeResolver::valid(world, value.scope_value)) return;
                    if (!passes_iterator_filter(value.scope_value)) return;
                    ScopeEnterGuard guard{context, value.scope_value};
                    apply_scoped_nodes(node.children, world, context, depth + 1u);
                };
                if (node.iterator_mode == ScopeIteratorMode::Every) {
                    for (const auto& value : values) {
                        if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                        apply_value(value);
                    }
                    return;
                }
                if (values.empty()) return;
                if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                const auto index = deterministic_index(context, node.salt, ScopeType::None,
                                                       values.size(), node.collection_name);
                apply_value(values[index]);
                return;
            }
            auto candidates = ScopeResolver::children(world, context.current, node.iterator_target);
            if (node.has_iterator_condition) {
                std::vector<ScopeRef> passing;
                passing.reserve(candidates.size());
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    if (passes_iterator_filter(candidate)) passing.push_back(candidate);
                }
                candidates = std::move(passing);
            }
            if (node.iterator_mode == ScopeIteratorMode::Every) {
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    ScopeEnterGuard guard{context, candidate};
                    apply_scoped_nodes(node.children, world, context, depth + 1u);
                }
                return;
            }
            if (node.iterator_mode == ScopeIteratorMode::Ordered) {
                if (candidates.empty()) return;
                std::vector<std::pair<double, ScopeRef>> scored;
                scored.reserve(candidates.size());
                for (auto c : candidates) {
                    // Charge the ORDER BY evaluation to the parent budget. This
                    // used to copy the context and call begin_execution(),
                    // which reset the budget to the default and then discarded
                    // the spend with the copy — so a wide ordered_* iterator
                    // could consume unbounded work and never trip the limit.
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    double score = static_cast<double>(c.raw_id);
                    if (node.iterator_config.has_order_by && programs_ != nullptr) {
                        if (auto *sv = programs_->find_value(node.iterator_config.order_by_value)) {
                            ScopeEnterGuard guard{context, c};
                            try { score = evaluate_value_internal(*sv, world, context, depth+1u); } catch(...) { score = 0; }
                        }
                    }
                    scored.emplace_back(score, c);
                }
                std::sort(scored.begin(), scored.end(), [&](auto &a, auto &b){
                    if (a.first != b.first) return node.iterator_config.descending ? a.first > b.first : a.first < b.first;
                    return a.second.raw_id < b.second.raw_id;
                });
                std::size_t offset = std::min<std::size_t>(node.iterator_config.offset, scored.size());
                std::size_t limit = node.iterator_config.has_limit ? std::min<std::size_t>(node.iterator_config.limit, scored.size()-offset) : scored.size()-offset;
                for (std::size_t i=offset; i<offset+limit; ++i) {
                    if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
                    ScopeEnterGuard guard{context, scored[i].second};
                    apply_scoped_nodes(node.children, world, context, depth + 1u);
                }
                return;
            }
            if (candidates.empty()) return;
            if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
            const auto index = deterministic_index(context, node.salt, node.iterator_target, candidates.size());
            ScopeEnterGuard guard{context, candidates[index]};
            apply_scoped_nodes(node.children, world, context, depth + 1u);
            return;
        }
        case ScopedEffectKind::SaveScope:
            context.save_event_target(node.binding_name, context.current);
            return;
        case ScopedEffectKind::ClearEventTarget:
            (void)context.clear_event_target(node.binding_name);
            return;
        case ScopedEffectKind::SetVariable: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value()) throw std::runtime_error("set_variable value reference is unset");
            context.set_variable(node.binding_name, *value);
            return;
        }
        case ScopedEffectKind::ChangeVariable: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value() || value->kind != ScriptArgumentKind::Number)
                throw std::runtime_error("change_variable value is not numeric");
            if (!context.has_variable(node.binding_name)) {
                context.set_variable(node.binding_name, *value);
            } else if (!context.change_variable(node.binding_name, value->number)) {
                throw std::runtime_error("change_variable target is not numeric");
            }
            return;
        }
        case ScopedEffectKind::ClearVariable:
            (void)context.clear_variable(node.binding_name);
            return;
        case ScopedEffectKind::AddToCollection: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value()) throw std::runtime_error("add_to_collection value reference is unset");
            if (!context.add_to_collection(node.collection_name, *value, true))
                throw std::runtime_error("add_to_collection element type mismatch");
            return;
        }
        case ScopedEffectKind::RemoveFromCollection: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value()) throw std::runtime_error("remove_from_collection value reference is unset");
            (void)context.remove_from_collection(node.collection_name, *value);
            return;
        }
        case ScopedEffectKind::ClearCollection:
            (void)context.clear_collection(node.collection_name);
            return;
        case ScopedEffectKind::Group:
            apply_scoped_nodes(node.children, world, context, depth + 1u);
            return;
        case ScopedEffectKind::RandomList: {
            // Weighted branch draw: cumulative weights against a keyed
            // deterministic draw. Equal (seed, scope, callsite) always picks
            // the same branch, so replays and desync checks stay exact.
            const auto branch_count = std::min(node.children.size(), node.random_list_weights.size());
            if (branch_count == 0u) return;
            std::uint64_t total_weight = 0u;
            for (std::size_t i = 0; i < branch_count; ++i) total_weight += node.random_list_weights[i];
            if (total_weight == 0u) return;
            if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
            const auto draw = deterministic_draw(context, node.salt) % total_weight;
            std::uint64_t accumulated = 0u;
            for (std::size_t i = 0; i < branch_count; ++i) {
                accumulated += node.random_list_weights[i];
                if (draw < accumulated) {
                    apply_scoped_nodes(node.children[i].children, world, context, depth + 1u);
                    return;
                }
            }
            apply_scoped_nodes(node.children[branch_count - 1u].children, world, context, depth + 1u);
            return;
        }
        case ScopedEffectKind::While:
            // Each pass re-evaluates the condition in the current scope and is
            // charged to the work budget: a limit that never fails terminates
            // deterministically with a budget error, never a hang.
            while (evaluate_scoped_node(node.condition, world, context, depth + 1u)) {
                if (!context.consume_work())
                    throw std::runtime_error("ThunderScript execution budget exceeded");
                apply_scoped_nodes(node.children, world, context, depth + 1u);
            }
            return;
        case ScopedEffectKind::If:
            // The condition tree is evaluated in the current scope; taken and
            // else branches share the scope (no implicit scope change).
            if (evaluate_scoped_node(node.condition, world, context, depth + 1u)) {
                apply_scoped_nodes(node.children, world, context, depth + 1u);
            } else {
                apply_scoped_nodes(node.else_children, world, context, depth + 1u);
            }
            return;
    }
}

void ScriptVm::apply(std::span<const CompiledEffectCall> effects, World& world, ScopeRef scope) const {
    for (const auto& call : effects) registry_.execute_effect(call.primitive, world, scope, call.argument);
}



} // namespace thunder
