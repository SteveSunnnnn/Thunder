#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/Scope.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

class ScriptRegistry;
class ScriptedGameplayRuntime;
class World;

using OnActionInvocationId = StrongId<struct OnActionInvocationTag, std::uint64_t>;

enum class OnActionStepKind : std::uint8_t { Script, Event };

struct OnActionStepDefinition {
    static constexpr std::uint32_t invalid_gameplay_definition =
        std::numeric_limits<std::uint32_t>::max();

    OnActionStepKind kind = OnActionStepKind::Script;
    // Stable content key used by checksums and diagnostics. It is deliberately
    // independent from SymbolId and runtime definition insertion order.
    std::string target_key;
    std::optional<ScriptProgram> script;
    std::uint32_t gameplay_definition = invalid_gameplay_definition;
};

struct OnActionDefinition {
    std::string key;
    ScopeType scope = ScopeType::None;
    // Declaration order is execution order.
    std::vector<OnActionStepDefinition> steps;
};

struct ScheduledOnActionInvocation {
    OnActionInvocationId id{};
    std::uint32_t definition = 0u;
    ScopeRef scope{};
    ScopeRef from{};
    std::uint64_t due_tick = 0u;
};

class OnActionRuntime {
public:
    explicit OnActionRuntime(const ScriptRegistry& registry,
                             const ScriptProgramDatabase* programs = nullptr);

    void set_program_database(const ScriptProgramDatabase* programs) noexcept;
    void clear_content();
    void clear_state() noexcept;
    std::uint32_t add_definition(OnActionDefinition definition);

    [[nodiscard]] std::uint32_t find_definition(std::string_view key) const noexcept;
    [[nodiscard]] OnActionInvocationId schedule_at(std::uint32_t definition,
                                                   const World& world, ScopeRef scope,
                                                   std::uint64_t due_tick,
                                                   ScopeRef from = {});
    [[nodiscard]] OnActionInvocationId schedule_after(std::uint32_t definition,
                                                      const World& world, ScopeRef scope,
                                                      std::uint64_t current_tick,
                                                      std::uint64_t delay_ticks,
                                                      ScopeRef from = {});
    bool cancel(OnActionInvocationId id) noexcept;

    // Dispatches every invocation whose absolute due tick is <= tick, bounded
    // by budget. Due tick then stable invocation id define the total order.
    std::size_t dispatch_due(World& world, ScriptedGameplayRuntime& gameplay,
                             std::uint64_t tick, std::size_t budget = 4096u);

    [[nodiscard]] std::span<const OnActionDefinition> definitions() const noexcept {
        return definitions_;
    }
    [[nodiscard]] std::span<const ScheduledOnActionInvocation> queue() const noexcept {
        return queue_;
    }
    [[nodiscard]] std::uint64_t next_invocation_id() const noexcept {
        return next_invocation_id_;
    }

    void validate_state(std::span<const ScheduledOnActionInvocation> queue,
                        std::uint64_t next_invocation_id, const World& world) const;
    void restore_state(std::vector<ScheduledOnActionInvocation> queue,
                       std::uint64_t next_invocation_id);
    [[nodiscard]] std::uint64_t checksum_state(
        std::span<const ScheduledOnActionInvocation> queue,
        std::uint64_t next_invocation_id) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    static constexpr std::size_t max_invocations = 1'000'000u;
    [[nodiscard]] bool optional_scope_valid(const World& world, ScopeRef scope) const noexcept;
    [[nodiscard]] static bool ordered_before(const ScheduledOnActionInvocation& left,
                                             const ScheduledOnActionInvocation& right) noexcept;

    ScriptVm vm_;
    std::vector<OnActionDefinition> definitions_;
    std::vector<ScheduledOnActionInvocation> queue_;
    std::uint64_t next_invocation_id_ = 1u;
};

} // namespace thunder
