#pragma once
#include "thunder/scripting/ScriptContext.hpp"
#include <cstdint>

namespace thunder {
class World;

// Global persistent script state outside Event/Journal instances.
// External scripts can use set_variable/change_variable on the global scope
// to keep world-level flags, counters, or aggregated metrics across ticks.
// The store is intentionally content-agnostic: it only owns the canonical
// context. SaveGameCodec persists it through the tagged GLB1 extension.
class GlobalScriptStore {
public:
    ScriptExecutionContext& context() noexcept { return context_; }
    const ScriptExecutionContext& context() const noexcept { return context_; }

    void clear() noexcept { context_ = ScriptExecutionContext{}; }

    // True when no authoritative global script state has been installed. A
    // completely empty store is omitted from legacy-compatible save payloads.
    [[nodiscard]] bool empty() const noexcept;

    // Deterministic checksum of global variables/collections
    [[nodiscard]] std::uint64_t checksum() const noexcept { return context_.checksum(); }

    // Validation against world (scope refs etc.)
    [[nodiscard]] bool validate(const World& world) const noexcept;

private:
    ScriptExecutionContext context_;
};

} // namespace thunder
