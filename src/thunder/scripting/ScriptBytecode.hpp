#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScriptContext.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

class World;

enum class BytecodeOpcode : std::uint8_t {
    Nop = 0,
    PushTrue = 1,
    PushFalse = 2,
    CallTrigger = 3,
    JumpIfFalse = 4,
    JumpIfTrue = 5,
    Jump = 6,
    EnterScope = 7,
    LeaveScope = 8,
    HasVariable = 9,
    CompareVariable = 10,
    Not = 11,
    Return = 12
};

struct BytecodeInstruction {
    BytecodeOpcode opcode = BytecodeOpcode::Nop;
    std::uint8_t comparison = 0; // ScriptComparison cast
    std::uint16_t primitive = 0; // TriggerPrimitiveId raw value
    std::uint32_t jump_target = 0; // Absolute target instruction index
    std::uint32_t scope_selector_kind = 0; // ScopeSelectorKind cast
    std::uint32_t reserved = 0;
    double argument_num = 0.0;
    std::uint64_t argument_key = 0;
};
static_assert(sizeof(BytecodeInstruction) == 32u);

struct ScriptBytecode {
    std::vector<BytecodeInstruction> instructions;
    std::uint64_t checksum = 0;

    [[nodiscard]] bool empty() const noexcept { return instructions.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return instructions.size(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(*this) + instructions.capacity() * sizeof(BytecodeInstruction);
    }
};

class BytecodeCompiler {
public:
    static ScriptBytecode compile(std::span<const CompiledScopedCondition> conditions);

private:
    static void compile_node(const CompiledScopedCondition& node,
                             std::vector<BytecodeInstruction>& instructions);
    static void compile_all(std::span<const CompiledScopedCondition> nodes,
                            std::vector<BytecodeInstruction>& instructions);
    static void compile_any(std::span<const CompiledScopedCondition> nodes,
                            std::vector<BytecodeInstruction>& instructions);
    static void optimize_peephole(std::vector<BytecodeInstruction>& instructions);
};

class BytecodeVm {
public:
    [[nodiscard]] static bool evaluate(const ScriptBytecode& bytecode,
                                       const ScriptRegistry& registry,
                                       const World& world,
                                       ScriptExecutionContext& context);
};

} // namespace thunder
