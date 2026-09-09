#pragma once
#include "thunder/scripting/ScriptProgram.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace thunder {
class World;
class ScriptRegistry;
class EconomyDefinitions;

struct AiActionDefinition {
    std::string key;
    ScopeType scope = ScopeType::Country;
    std::optional<ScriptProgram> valid;
    std::optional<ScriptedValueProgram> utility;
    std::optional<ScriptProgram> effect;
    double base_utility = 0.0;
    std::uint32_t cooldown_ticks = 0;
};

struct AiChoice {
    std::uint32_t action = 0;
    ScopeRef scope{};
    double utility = 0.0;
    bool valid = false;
};

struct AiActionState {
    std::uint32_t action = 0;
    ScopeRef scope{};
    std::uint64_t last_tick = 0;
};

// A strategic plan gives utility AI persistent intent across ticks. Actions remain
// generic script-driven operations; plans constrain which actions are considered
// until completion or invalidation so the AI does not oscillate between unrelated
// one-tick choices.
struct AiPlanDefinition {
    std::string key;
    ScopeType scope = ScopeType::Country;
    std::optional<ScriptProgram> valid;
    std::optional<ScriptedValueProgram> priority;
    std::optional<ScriptProgram> completion;
    std::vector<std::string> action_keys;
    double base_priority = 0.0;
    std::uint32_t commitment_ticks = 0;
};

struct AiPlanChoice {
    std::uint32_t plan = 0;
    ScopeRef scope{};
    double priority = 0.0;
    bool valid = false;
};

struct AiPlanState {
    std::uint32_t plan = 0;
    ScopeRef scope{};
    std::uint64_t started_tick = 0;
    std::uint64_t last_tick = 0;
};

class UtilityAiEngine {
public:
    explicit UtilityAiEngine(const ScriptRegistry& registry, const ScriptProgramDatabase* programs = nullptr)
        : vm_(registry, programs) {}
    void set_program_database(const ScriptProgramDatabase* programs) noexcept { vm_.set_program_database(programs); }
    void clear_content();
    std::uint32_t add_action(AiActionDefinition action);
    std::uint32_t add_plan(AiPlanDefinition plan);
    [[nodiscard]] AiChoice choose(const World& world, ScopeRef scope, std::uint64_t tick = 0) const;
    [[nodiscard]] AiChoice choose_for_plan(const World& world, ScopeRef scope, std::uint32_t plan,
                                           std::uint64_t tick = 0) const;
    [[nodiscard]] AiPlanChoice choose_plan(const World& world, ScopeRef scope, std::uint64_t tick = 0) const;
    bool execute_best(World& world, ScopeRef scope, std::uint64_t tick = 0);
    bool execute_planned_best(World& world, ScopeRef scope, std::uint64_t tick = 0);
    std::size_t run_all(World& world, ScopeType type = ScopeType::Country,
                        std::size_t action_budget = 4096u, std::uint64_t tick = 0);
    std::size_t run_plans(World& world, ScopeType type = ScopeType::Country,
                          std::size_t action_budget = 4096u, std::uint64_t tick = 0);
    [[nodiscard]] std::span<const AiActionDefinition> actions() const noexcept { return actions_; }
    [[nodiscard]] std::span<const AiPlanDefinition> plans() const noexcept { return plans_; }
    [[nodiscard]] std::span<const AiActionState> state() const noexcept { return state_; }
    [[nodiscard]] std::span<const AiPlanState> plan_state() const noexcept { return plan_state_; }
    void validate_state(std::span<const AiActionState> state, const World& world,
                        std::uint64_t current_tick) const;
    void validate_plan_state(std::span<const AiPlanState> state, const World& world,
                             std::uint64_t current_tick) const;
    void restore_state(std::vector<AiActionState> state);
    void restore_plan_state(std::vector<AiPlanState> state);
    [[nodiscard]] std::uint64_t checksum_state(std::span<const AiActionState> state,
                                               std::span<const AiPlanState> plan_state) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] bool on_cooldown(std::uint32_t action, ScopeRef scope, std::uint64_t tick) const noexcept;
    [[nodiscard]] bool plan_contains_action(const AiPlanDefinition& plan, std::string_view action_key) const noexcept;
    [[nodiscard]] AiPlanState* active_plan_state(ScopeRef scope) noexcept;
    [[nodiscard]] const AiPlanState* active_plan_state(ScopeRef scope) const noexcept;
    void mark_executed(std::uint32_t action, ScopeRef scope, std::uint64_t tick);

    ScriptVm vm_;
    std::vector<AiActionDefinition> actions_;
    std::vector<AiPlanDefinition> plans_;
    std::vector<AiActionState> state_;
    std::vector<AiPlanState> plan_state_;
};

enum class DiplomaticPlayAiStance : std::uint8_t {
    Yield = 0,
    Hold = 1,
    Escalate = 2,
    MobilizeAndSway = 3
};

class StrategicAiEvaluator {
public:
    [[nodiscard]] static DiplomaticPlayAiStance evaluate_diplomatic_play(const World& world, CountryId country, DiplomaticPlayId play);
    [[nodiscard]] static bool should_ai_back_down(const World& world, CountryId country, DiplomaticPlayId play);
    [[nodiscard]] static std::vector<CountryId> pick_sway_targets(const World& world, CountryId sponsor, DiplomaticPlayId play);
    [[nodiscard]] static std::vector<GoodId> evaluate_economic_shortages(
        const World& world, CountryId country, const EconomyDefinitions& definitions);
    static void auto_balance_frontlines(World& world, CountryId country, FrontId front);
};

} // namespace thunder

