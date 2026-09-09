#pragma once
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/Scope.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace thunder {
class World;
class ScriptRegistry;

using GameplayInstanceId = StrongId<struct GameplayInstanceTag, std::uint64_t>;

enum class GameplayItemKind : std::uint8_t { Event, Decision, Journal };

enum class GameplayLogKind : std::uint8_t {
    EventOpened,
    EventOptionTaken,
    EventResolved,
    DecisionTaken,
    JournalOpened,
    JournalCompleted
};

struct GameplayOptionDefinition {
    std::string key;
    std::optional<ScriptProgram> allow;
    std::optional<ScriptProgram> effect;
};

struct GameplayDefinition {
    std::string key;
    GameplayItemKind kind = GameplayItemKind::Event;
    ScopeType scope = ScopeType::Country;
    std::optional<ScriptProgram> potential;
    std::optional<ScriptProgram> allow;
    std::optional<ScriptProgram> effect;
    std::optional<ScriptProgram> completion;
    std::vector<GameplayOptionDefinition> options;
    std::uint32_t cooldown_ticks = 0;
    bool auto_trigger = false;
};

struct GameplayInstance {
    static constexpr std::uint32_t no_option = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t definition = 0;
    ScopeRef scope{};
    std::uint64_t opened_tick = 0;
    std::uint64_t last_action_tick = 0;
    std::uint32_t selected_option = no_option;
    bool active = true;
    bool completed = false;
    bool awaiting_choice = false;
    GameplayInstanceId id{};
    // Only live event/journal chains retain context. Completed instances release it,
    // while save/checksum preserve it whenever a later script can observe it.
    std::optional<ScriptExecutionContext> context;
};

struct GameplayLogEntry {
    std::uint64_t tick = 0;
    GameplayLogKind kind = GameplayLogKind::EventOpened;
    std::uint32_t definition = 0;
    ScopeRef scope{};
    std::uint32_t option = GameplayInstance::no_option;
};

class ScriptedGameplayRuntime {
public:
    explicit ScriptedGameplayRuntime(const ScriptRegistry& registry,
                                     const ScriptProgramDatabase* programs = nullptr);
    void set_program_database(const ScriptProgramDatabase* programs) noexcept { vm_.set_program_database(programs); }
    void clear_content();
    std::uint32_t add_definition(GameplayDefinition definition);
    [[nodiscard]] bool available(std::uint32_t definition, const World& world, ScopeRef scope,
                                 std::uint64_t tick, ScopeRef from = {}) const;
    bool fire(std::uint32_t definition, World& world, ScopeRef scope, std::uint64_t tick,
              ScopeRef from = {});
    bool take_decision(std::uint32_t definition, World& world, ScopeRef scope, std::uint64_t tick);
    bool choose_event_option(std::uint32_t instance, std::uint32_t option, World& world,
                             std::uint64_t tick);
    bool choose_event_option(GameplayInstanceId instance, std::uint32_t option, World& world,
                             std::uint64_t tick);
    void update_journals(World& world, std::uint64_t tick);
    std::size_t update_auto_events(World& world, std::uint64_t tick, std::size_t budget = 4096u);
    void update(World& world, std::uint64_t tick, std::size_t auto_event_budget = 4096u);
    [[nodiscard]] std::span<const GameplayDefinition> definitions() const noexcept { return definitions_; }
    [[nodiscard]] std::span<const GameplayInstance> instances() const noexcept { return instances_; }
    [[nodiscard]] std::span<const GameplayLogEntry> log() const noexcept { return log_; }
    [[nodiscard]] std::uint64_t next_instance_id() const noexcept { return next_instance_id_; }
    [[nodiscard]] const GameplayInstance* find_instance(GameplayInstanceId id) const noexcept;
    void validate_state(std::span<const GameplayInstance> instances, std::span<const GameplayLogEntry> log,
                        const World& world, std::uint64_t current_tick,
                        std::uint64_t next_instance_id = 0u) const;
    void restore_state(std::vector<GameplayInstance> instances, std::vector<GameplayLogEntry> log,
                       std::uint64_t next_instance_id = 0u);
    [[nodiscard]] std::uint64_t checksum_state(std::span<const GameplayInstance> instances,
                                               std::span<const GameplayLogEntry> log,
                                               std::uint64_t next_instance_id = 0u) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] bool passes(const std::optional<ScriptProgram>& program, const World& world,
                              ScopeRef scope, ScopeRef from = {}, std::uint64_t seed = 0) const;
    bool apply(const std::optional<ScriptProgram>& program, World& world, ScopeRef scope,
               ScopeRef from, std::uint64_t seed) const;
    [[nodiscard]] bool passes(const std::optional<ScriptProgram>& program, const World& world,
                              ScriptExecutionContext context) const;
    bool apply(const std::optional<ScriptProgram>& program, World& world,
               ScriptExecutionContext& context) const;
    void append_log(GameplayLogKind kind, std::uint32_t definition, ScopeRef scope,
                    std::uint64_t tick, std::uint32_t option = GameplayInstance::no_option);

    ScriptVm vm_;
    std::vector<GameplayDefinition> definitions_;
    std::vector<GameplayInstance> instances_;
    std::vector<GameplayLogEntry> log_;
    std::uint64_t next_instance_id_ = 1u;
};

} // namespace thunder
