#include "thunder/scripting/ScriptProgram.hpp"

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace thunder {
namespace {

class ScriptCallGuard {
public:
    ScriptCallGuard(ScriptExecutionContext& context,
                    std::span<const ScriptNamedValue> arguments)
        : context_(context) {
        if (context_.call_depth() == 0u) context_.push_call_frame();
        context_.push_call_frame(arguments);
    }
    ScriptCallGuard(const ScriptCallGuard&) = delete;
    ScriptCallGuard& operator=(const ScriptCallGuard&) = delete;
    ~ScriptCallGuard() { context_.pop_call_frame(); }

private:
    ScriptExecutionContext& context_;
};

constexpr std::uint32_t kMaxScriptDepth = 64u;

// Builtin per-scope value sources. Shared by the legacy source*multiply+add
// path and the PushSource bytecode op, so expression operands read exactly the
// same world columns as `source =` programs.
double source_value(ValueSource source, const World& world, ScopeRef scope,
                    ScriptExecutionContext& context) {
    double value = 0.0;
    switch (source) {
    case ValueSource::Population: value = world.countries.population(CountryId{scope.raw_id}); break;
    case ValueSource::Gdp: value = world.countries.gdp(CountryId{scope.raw_id}); break;
    case ValueSource::Treasury: value = world.countries.treasury(CountryId{scope.raw_id}); break;
    case ValueSource::TaxRate: value = world.countries.tax_rate(CountryId{scope.raw_id}); break;
    case ValueSource::PopulationSize: value = static_cast<double>(world.pops.population(PopId{scope.raw_id})); break;
    case ValueSource::Employment: value = static_cast<double>(world.pops.employed(PopId{scope.raw_id})); break;
    case ValueSource::StandardOfLiving: value = static_cast<double>(world.pops.standard_of_living_milli(PopId{scope.raw_id})) / 1000.0; break;
    case ValueSource::Literacy: value = static_cast<double>(world.pops.literacy_permyriad(PopId{scope.raw_id})) / 10000.0; break;
    case ValueSource::Qualification: value = static_cast<double>(world.pops.qualification_permyriad(PopId{scope.raw_id})) / 10000.0; break;
    case ValueSource::Wealth: value = static_cast<double>(world.pops.wealth_milli(PopId{scope.raw_id})) / 1000.0; break;
    case ValueSource::PoliticalStrength: value = static_cast<double>(world.pops.political_strength_milli(PopId{scope.raw_id})) / 1000.0; break;
    case ValueSource::MarketSupply:
        for (const auto item : world.markets.supply_row(MarketId{scope.raw_id})) {
            if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
            value += static_cast<double>(item);
        }
        break;
    case ValueSource::MarketDemand:
        for (const auto item : world.markets.demand_row(MarketId{scope.raw_id})) {
            if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
            value += static_cast<double>(item);
        }
        break;
    case ValueSource::StatePopulation:
        for (std::size_t index = 0u; index < world.pops.size(); ++index) {
            if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
            if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(index))) continue;
            const auto pop = PopId{static_cast<std::uint32_t>(index)};
            const auto province = world.pops.province(pop);
            if (province.valid() && world.geography.province_state(province) == StateId{scope.raw_id})
                value += static_cast<double>(world.pops.population(pop));
        }
        break;
    case ValueSource::ProvincePopulation:
        for (std::size_t index = 0u; index < world.pops.size(); ++index) {
            if (!context.consume_work()) throw std::runtime_error("ThunderScript execution budget exceeded");
            if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(index))) continue;
            const auto pop = PopId{static_cast<std::uint32_t>(index)};
            if (world.pops.province(pop) == ProvinceId{scope.raw_id})
                value += static_cast<double>(world.pops.population(pop));
        }
        break;
    case ValueSource::BuildingLevel: value = static_cast<double>(world.buildings.level(BuildingId{scope.raw_id})); break;
    case ValueSource::BuildingEmployees: value = static_cast<double>(world.buildings.employees(BuildingId{scope.raw_id})); break;
    case ValueSource::BuildingProfit: value = static_cast<double>(world.buildings.last_profit(BuildingId{scope.raw_id})) / 1000.0; break;
    case ValueSource::BuildingCash: value = static_cast<double>(world.buildings.cash(BuildingId{scope.raw_id})) / 1000.0; break;
    case ValueSource::ArmyManpower: value = static_cast<double>(world.grand_strategy.armys()[scope.raw_id].manpower); break;
    case ValueSource::ArmyOrganization: value = static_cast<double>(world.grand_strategy.armys()[scope.raw_id].organization_ppm) / 1'000'000.0; break;
    case ValueSource::FrontProgress: value = static_cast<double>(world.grand_strategy.fronts()[scope.raw_id].progress_milli) / 1000.0; break;
    case ValueSource::WarScore: value = static_cast<double>(world.grand_strategy.wars()[scope.raw_id].war_score_milli) / 1000.0; break;
    case ValueSource::WarWeeks: value = static_cast<double>(world.grand_strategy.wars()[scope.raw_id].weeks); break;
    case ValueSource::Constant:
    case ValueSource::VariableRef:
    case ValueSource::ScriptedValueRef:
        break;
    case ValueSource::RuntimeArgument:
        throw std::runtime_error("scripted value PushSource cannot read a runtime argument");
    }
    return value;
}

} // namespace

double ScriptVm::evaluate(const ScriptedValueProgram& program,
                          const World& world, ScopeRef scope) const {
    return evaluate(program, world, ScriptExecutionContext::rooted(scope));
}

double ScriptVm::evaluate(const ScriptedValueProgram& program,
                          const World& world,
                          ScriptExecutionContext context) const {
    context.begin_execution();
    return evaluate_value_internal(program, world, context, 0u);
}

double ScriptVm::evaluate_value(SymbolId name, const World& world,
                                ScriptExecutionContext context,
                                std::span<const ScriptNamedValue> arguments) const {
    if (programs_ == nullptr)
        throw std::runtime_error("scripted value call requires ScriptProgramDatabase");
    const auto* program = programs_->find_value(name);
    if (program == nullptr)
        throw std::runtime_error("scripted value call references unknown value");
    context.begin_execution();
    ScriptCallGuard guard{context, arguments};
    return evaluate_value_internal(*program, world, context, 0u);
}

double ScriptVm::eval_value_bytecode(const ScriptValueBytecode& bytecode,
                                     const World& world,
                                     ScriptExecutionContext& context,
                                     std::uint32_t depth) const {
    std::vector<double> stack;
    stack.reserve(bytecode.ops.size());
    std::size_t const_index = 0u;
    std::size_t var_index = 0u;
    std::size_t source_index = 0u;
    std::size_t value_index = 0u;
    for (std::size_t index = 0u; index < bytecode.ops.size(); ++index) {
        const auto op = bytecode.ops[index];
        switch (op) {
        case ScriptValueOp::PushConst: {
            const double value = const_index < bytecode.const_pool.size()
                ? bytecode.const_pool[const_index++]
                : 0.0;
            stack.push_back(value);
            break;
        }
        case ScriptValueOp::PushVariable: {
            const ScriptStableKey key = var_index < bytecode.var_keys.size()
                ? bytecode.var_keys[var_index++] : 0u;
            const auto value = context.variable(key);
            stack.push_back(value.valid() && value.kind == ScriptArgumentKind::Number
                                ? value.number : 0.0);
            break;
        }
        case ScriptValueOp::PushScriptedValue: {
            if (programs_ == nullptr)
                throw std::runtime_error("scripted value reference requires ScriptProgramDatabase");
            const SymbolId name = value_index < bytecode.value_refs.size()
                ? bytecode.value_refs[value_index++]
                : SymbolId{};
            const auto* called = programs_->find_value(name);
            if (called == nullptr)
                throw std::runtime_error("scripted value reference is unknown");
            ScriptCallGuard call_guard{context, {}};
            stack.push_back(evaluate_value_internal(*called, world, context, depth + 1u));
            break;
        }
        case ScriptValueOp::PushSource: {
            const auto source = source_index < bytecode.source_pool.size()
                ? bytecode.source_pool[source_index++]
                : ValueSource::Constant;
            stack.push_back(source_value(source, world, context.current, context));
            break;
        }
            case ScriptValueOp::Add:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() += rhs;
                }
                break;
            case ScriptValueOp::Sub:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() -= rhs;
                }
                break;
            case ScriptValueOp::Mul:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() *= rhs;
                }
                break;
            case ScriptValueOp::Div:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = std::abs(rhs) > 1e-12 ? stack.back() / rhs : 0.0;
                }
                break;
            case ScriptValueOp::Neg:
                if (!stack.empty()) stack.back() = -stack.back();
                break;
            case ScriptValueOp::Eq:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (std::abs(stack.back() - rhs) < 1e-6) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::Ne:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (std::abs(stack.back() - rhs) >= 1e-6) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::Lt:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (stack.back() < rhs) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::Le:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (stack.back() <= rhs + 1e-6) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::Gt:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (stack.back() > rhs) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::Ge:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (stack.back() >= rhs - 1e-6) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::And:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (stack.back() != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::Or:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = (stack.back() != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::Not:
                if (!stack.empty()) {
                    stack.back() = (stack.back() == 0.0) ? 1.0 : 0.0;
                }
                break;
            case ScriptValueOp::If:
                if (stack.size() >= 3u) {
                    const double false_val = stack.back(); stack.pop_back();
                    const double true_val = stack.back(); stack.pop_back();
                    const double cond = stack.back(); stack.pop_back();
                    stack.push_back(cond != 0.0 ? true_val : false_val);
                }
                break;
            case ScriptValueOp::Max:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = std::max(stack.back(), rhs);
                }
                break;
            case ScriptValueOp::Min:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = std::min(stack.back(), rhs);
                }
                break;
            case ScriptValueOp::Clamp:
                if (stack.size() >= 3u) {
                    const double max_val = stack.back(); stack.pop_back();
                    const double min_val = stack.back(); stack.pop_back();
                    const double val = stack.back(); stack.pop_back();
                    stack.push_back(std::clamp(val, min_val, max_val));
                }
                break;
            default:
                break;
            }
            if (!context.consume_work())
                throw std::runtime_error("ThunderScript execution budget exceeded");
        }
        const double result = stack.empty() ? 0.0 : stack.back();
        if (!std::isfinite(result))
            throw std::range_error("scripted value bytecode result non-finite");
        return result;
}

double ScriptVm::evaluate_value_internal(const ScriptedValueProgram& program,
                                         const World& world,
                                         ScriptExecutionContext& context,
                                         std::uint32_t depth) const {
    if (depth > kMaxScriptDepth)
        throw std::runtime_error("ThunderScript maximum call depth exceeded");
    if (!context.consume_work())
        throw std::runtime_error("ThunderScript execution budget exceeded");
    const auto scope = context.current;
    if (scope.type != program.scope)
        throw std::runtime_error("scripted value scope mismatch");
    if (!ScopeResolver::valid(world, scope))
        throw std::runtime_error("invalid scripted value scope");
    if (!prepare_value_invocation(program, world, context))
        throw std::runtime_error("scripted value typed arguments do not match signature");

    if (program.uses_bytecode && !program.bytecode.ops.empty()) {
        return eval_value_bytecode(program.bytecode, world, context, depth);
    }

    double value = 0.0;
    switch (program.source) {
    case ValueSource::RuntimeArgument: {
        const auto resolved = resolve_argument(program.runtime_source, world, context, depth + 1u);
        if (!resolved.has_value()) throw std::runtime_error("scripted value source reference is unset");
        if (resolved->kind == ScriptArgumentKind::Number) value = resolved->number;
        else if (resolved->kind == ScriptArgumentKind::Boolean) value = resolved->boolean_value() ? 1.0 : 0.0;
        else throw std::runtime_error("scripted value source is not numeric");
        break;
    }
    case ValueSource::Constant:
    case ValueSource::VariableRef:
    case ValueSource::ScriptedValueRef:
        break;
    default:
        value = source_value(program.source, world, scope, context);
        break;
    }

    const double result = value * program.multiply + program.add;
    if (!std::isfinite(result))
        throw std::range_error("scripted value result is non-finite");
    return result;
}

} // namespace thunder
