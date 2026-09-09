#include "thunder/scripting/ScriptBytecode.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace thunder {

namespace {

ScopeRef resolve_bytecode_scope(ScopeSelectorKind kind, ScriptStableKey saved_name,
                                const World& world, const ScriptExecutionContext& context) noexcept {
    switch (kind) {
        case ScopeSelectorKind::This: return context.current;
        case ScopeSelectorKind::Root: return context.root;
        case ScopeSelectorKind::From: return context.from;
        case ScopeSelectorKind::Prev: return context.prev();
        case ScopeSelectorKind::Owner:
        case ScopeSelectorKind::Country: return ScopeResolver::owner(world, context.current);
        case ScopeSelectorKind::Market: return ScopeResolver::market(world, context.current);
        case ScopeSelectorKind::State: return ScopeResolver::state(world, context.current);
        case ScopeSelectorKind::Province: return ScopeResolver::province(world, context.current);
        case ScopeSelectorKind::Saved: return context.saved(saved_name);
    }
    return {};
}

bool compare_script_values(double lhs, ScriptComparison comp, double rhs) noexcept {
    switch (comp) {
        case ScriptComparison::Equal: return std::abs(lhs - rhs) < 1e-6;
        case ScriptComparison::NotEqual: return std::abs(lhs - rhs) >= 1e-6;
        case ScriptComparison::Above: return lhs > rhs;
        case ScriptComparison::AtLeast: return lhs >= rhs - 1e-6;
        case ScriptComparison::Below: return lhs < rhs;
        case ScriptComparison::AtMost: return lhs <= rhs + 1e-6;
    }
    return false;
}

} // namespace

ScriptBytecode BytecodeCompiler::compile(std::span<const CompiledScopedCondition> conditions) {
    ScriptBytecode bytecode;
    if (conditions.empty()) {
        bytecode.instructions.push_back({.opcode = BytecodeOpcode::PushTrue});
        bytecode.instructions.push_back({.opcode = BytecodeOpcode::Return});
        return bytecode;
    }

    compile_all(conditions, bytecode.instructions);
    bytecode.instructions.push_back({.opcode = BytecodeOpcode::Return});
    optimize_peephole(bytecode.instructions);

    Fnv1a64 h;
    h.add(static_cast<std::uint64_t>(bytecode.instructions.size()));
    for (const auto& inst : bytecode.instructions) {
        h.add(static_cast<std::uint8_t>(inst.opcode));
        h.add(inst.comparison);
        h.add(inst.primitive);
        h.add(inst.jump_target);
        h.add(inst.scope_selector_kind);
        h.add(inst.argument_num);
        h.add(inst.argument_key);
    }
    bytecode.checksum = h.value();
    return bytecode;
}

void BytecodeCompiler::optimize_peephole(std::vector<BytecodeInstruction>& instructions) {
    if (instructions.size() <= 2) return;
    bool changed = true;
    for (int pass = 0; pass < 4 && changed; ++pass) {
        changed = false;
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            auto& inst = instructions[i];
            // 1. Jump threading: If a Jump points to another Jump, redirect directly to final target
            if (inst.opcode == BytecodeOpcode::Jump ||
                inst.opcode == BytecodeOpcode::JumpIfFalse ||
                inst.opcode == BytecodeOpcode::JumpIfTrue) {
                if (inst.jump_target < instructions.size()) {
                    const auto& target_inst = instructions[inst.jump_target];
                    if (target_inst.opcode == BytecodeOpcode::Jump &&
                        target_inst.jump_target < instructions.size() &&
                        target_inst.jump_target != inst.jump_target) {
                        inst.jump_target = target_inst.jump_target;
                        changed = true;
                    }
                }
                // 2. Eliminate redundant Jump to the immediate next instruction
                if (inst.opcode == BytecodeOpcode::Jump && inst.jump_target == i + 1) {
                    inst.opcode = BytecodeOpcode::Nop;
                    changed = true;
                }
            }
        }
    }
}

void BytecodeCompiler::compile_all(std::span<const CompiledScopedCondition> nodes,
                                   std::vector<BytecodeInstruction>& instructions) {
    if (nodes.empty()) {
        instructions.push_back({.opcode = BytecodeOpcode::PushTrue});
        return;
    }

    std::vector<std::uint32_t> jump_to_falses;

    for (const auto& child : nodes) {
        compile_node(child, instructions);
        // After evaluating child, result is in reg. If false, jump to exit
        const auto jump_idx = static_cast<std::uint32_t>(instructions.size());
        instructions.push_back({.opcode = BytecodeOpcode::JumpIfFalse, .jump_target = 0});
        jump_to_falses.push_back(jump_idx);
    }

    // All passed -> Push True
    instructions.push_back({.opcode = BytecodeOpcode::PushTrue});
    const auto exit_all_true = static_cast<std::uint32_t>(instructions.size());
    instructions.push_back({.opcode = BytecodeOpcode::Jump, .jump_target = 0});

    // False target
    const auto false_target = static_cast<std::uint32_t>(instructions.size());
    for (const auto idx : jump_to_falses) {
        instructions[idx].jump_target = false_target;
    }
    instructions.push_back({.opcode = BytecodeOpcode::PushFalse});

    // End target
    const auto end_target = static_cast<std::uint32_t>(instructions.size());
    instructions[exit_all_true].jump_target = end_target;
}

void BytecodeCompiler::compile_any(std::span<const CompiledScopedCondition> nodes,
                                   std::vector<BytecodeInstruction>& instructions) {
    if (nodes.empty()) {
        instructions.push_back({.opcode = BytecodeOpcode::PushTrue});
        return;
    }

    std::vector<std::uint32_t> jump_to_trues;

    for (const auto& child : nodes) {
        compile_node(child, instructions);
        // If true, jump to exit_true
        const auto jump_idx = static_cast<std::uint32_t>(instructions.size());
        instructions.push_back({.opcode = BytecodeOpcode::JumpIfTrue, .jump_target = 0});
        jump_to_trues.push_back(jump_idx);
    }

    // All failed -> Push False
    instructions.push_back({.opcode = BytecodeOpcode::PushFalse});
    const auto exit_all_false = static_cast<std::uint32_t>(instructions.size());
    instructions.push_back({.opcode = BytecodeOpcode::Jump, .jump_target = 0});

    // True target
    const auto true_target = static_cast<std::uint32_t>(instructions.size());
    for (const auto idx : jump_to_trues) {
        instructions[idx].jump_target = true_target;
    }
    instructions.push_back({.opcode = BytecodeOpcode::PushTrue});

    // End target
    const auto end_target = static_cast<std::uint32_t>(instructions.size());
    instructions[exit_all_false].jump_target = end_target;
}

void BytecodeCompiler::compile_node(const CompiledScopedCondition& node,
                                    std::vector<BytecodeInstruction>& instructions) {
    switch (node.kind) {
        case ScopedConditionKind::Trigger: {
            BytecodeInstruction inst;
            inst.opcode = BytecodeOpcode::CallTrigger;
            inst.primitive = static_cast<std::uint16_t>(node.primitive.value());
            if (node.argument.source == ScriptArgumentSourceKind::Literal &&
                node.argument.literal.kind == ScriptArgumentKind::Number) {
                inst.argument_num = node.argument.literal.number;
            }
            instructions.push_back(inst);
            break;
        }
        case ScopedConditionKind::All: {
            compile_all(node.children, instructions);
            break;
        }
        case ScopedConditionKind::Any: {
            compile_any(node.children, instructions);
            break;
        }
        case ScopedConditionKind::Not: {
            if (node.children.empty()) {
                instructions.push_back({.opcode = BytecodeOpcode::PushFalse});
            } else {
                compile_node(node.children.front(), instructions);
                instructions.push_back({.opcode = BytecodeOpcode::Not});
            }
            break;
        }
        case ScopedConditionKind::Scope: {
            BytecodeInstruction enter_inst;
            enter_inst.opcode = BytecodeOpcode::EnterScope;
            enter_inst.scope_selector_kind = static_cast<std::uint32_t>(node.selector.kind);
            enter_inst.argument_key = node.selector.saved_name;
            instructions.push_back(enter_inst);

            compile_all(node.children, instructions);

            instructions.push_back({.opcode = BytecodeOpcode::LeaveScope});
            break;
        }
        case ScopedConditionKind::HasVariable: {
            BytecodeInstruction inst;
            inst.opcode = BytecodeOpcode::HasVariable;
            inst.argument_key = node.binding_name;
            instructions.push_back(inst);
            break;
        }
        case ScopedConditionKind::CompareVariable: {
            BytecodeInstruction inst;
            inst.opcode = BytecodeOpcode::CompareVariable;
            inst.argument_key = node.binding_name;
            inst.comparison = static_cast<std::uint8_t>(node.comparison);
            if (node.argument.source == ScriptArgumentSourceKind::Literal &&
                node.argument.literal.kind == ScriptArgumentKind::Number) {
                inst.argument_num = node.argument.literal.number;
            }
            instructions.push_back(inst);
            break;
        }
        default:
            instructions.push_back({.opcode = BytecodeOpcode::PushTrue});
            break;
    }
}

bool BytecodeVm::evaluate(const ScriptBytecode& bytecode,
                          const ScriptRegistry& registry,
                          const World& world,
                          ScriptExecutionContext& context) {
    if (bytecode.instructions.empty()) return true;

    if (context.transient_work_remaining == 0) {
        context.begin_execution();
    }

    const auto num_instructions = bytecode.instructions.size();
    std::size_t pc = 0;
    bool reg = true;

    while (pc < num_instructions) {
        if (!context.consume_work(1u)) {
            return false;
        }

        const auto& inst = bytecode.instructions[pc];
        switch (inst.opcode) {
            case BytecodeOpcode::Nop:
                ++pc;
                break;
            case BytecodeOpcode::PushTrue:
                reg = true;
                ++pc;
                break;
            case BytecodeOpcode::PushFalse:
                reg = false;
                ++pc;
                break;
            case BytecodeOpcode::CallTrigger: {
                const TriggerPrimitiveId prim{inst.primitive};
                // A bytecode context can outlive an entity (for example an
                // event queued before a POP is merged/destroyed).  Treat the
                // stale scope as a false trigger instead of dispatching into
                // a callback that would throw from an SoA accessor.
                reg = ScopeResolver::valid(world, context.current) &&
                    registry.evaluate_trigger(prim, world, context.current, inst.argument_num);
                ++pc;
                break;
            }
            case BytecodeOpcode::JumpIfFalse: {
                if (!reg) {
                    pc = inst.jump_target;
                } else {
                    ++pc;
                }
                break;
            }
            case BytecodeOpcode::JumpIfTrue: {
                if (reg) {
                    pc = inst.jump_target;
                } else {
                    ++pc;
                }
                break;
            }
            case BytecodeOpcode::Jump: {
                pc = inst.jump_target;
                break;
            }
            case BytecodeOpcode::EnterScope: {
                const auto kind = static_cast<ScopeSelectorKind>(inst.scope_selector_kind);
                const auto target_scope = resolve_bytecode_scope(kind, inst.argument_key, world, context);
                context.enter(target_scope);
                ++pc;
                break;
            }
            case BytecodeOpcode::LeaveScope: {
                context.leave();
                ++pc;
                break;
            }
            case BytecodeOpcode::HasVariable: {
                reg = context.has_variable(inst.argument_key);
                ++pc;
                break;
            }
            case BytecodeOpcode::CompareVariable: {
                const auto var = context.variable(inst.argument_key);
                if (var.kind != ScriptArgumentKind::Number) {
                    reg = false;
                } else {
                    const auto comp = static_cast<ScriptComparison>(inst.comparison);
                    reg = compare_script_values(var.number, comp, inst.argument_num);
                }
                ++pc;
                break;
            }
            case BytecodeOpcode::Not: {
                reg = !reg;
                ++pc;
                break;
            }
            case BytecodeOpcode::Return: {
                return reg;
            }
        }
    }

    return reg;
}

} // namespace thunder
