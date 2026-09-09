#pragma once
#include "thunder/simulation/ai/UtilityAi.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace thunder {

struct GameplayOptionSpec {
    SymbolId key{};
    SymbolId allow{};
    SymbolId effect{};
    std::uint32_t line = 0;
};

struct GameplayDefinitionSpec {
    SymbolId key{};
    GameplayItemKind kind = GameplayItemKind::Event;
    ScopeType scope = ScopeType::None;
    SymbolId potential{};
    SymbolId allow{};
    SymbolId effect{};
    SymbolId completion{};
    std::vector<GameplayOptionSpec> options;
    std::uint32_t cooldown_ticks = 0;
    bool auto_trigger = false;
    std::uint32_t line = 0;
};

struct AiActionSpec {
    SymbolId key{};
    ScopeType scope = ScopeType::None;
    SymbolId valid{};
    SymbolId utility{};
    SymbolId effect{};
    double base_utility = 0.0;
    std::uint32_t cooldown_ticks = 0;
    std::uint32_t line = 0;
};

struct AiPlanSpec {
    SymbolId key{};
    ScopeType scope = ScopeType::None;
    SymbolId valid{};
    SymbolId priority{};
    SymbolId completion{};
    std::vector<SymbolId> actions;
    double base_priority = 0.0;
    std::uint32_t commitment_ticks = 0;
    std::uint32_t line = 0;
};

class GameplayContentDatabase {
public:
    explicit GameplayContentDatabase(SymbolTable& symbols);

    bool ingest(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool bind(const ScriptProgramDatabase& programs, ScriptedGameplayRuntime& gameplay,
              UtilityAiEngine& ai, std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    [[nodiscard]] std::span<const GameplayDefinitionSpec> gameplay() const noexcept { return gameplay_; }
    [[nodiscard]] std::span<const AiActionSpec> ai_actions() const noexcept { return ai_actions_; }
    [[nodiscard]] std::span<const AiPlanSpec> ai_plans() const noexcept { return ai_plans_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] ScopeType parse_scope(const ScriptNode& node) const noexcept;
    [[nodiscard]] bool parse_bool(const ScriptNode& node, bool& value) const;
    [[nodiscard]] bool parse_u32(const ScriptNode& node, std::uint32_t& value) const noexcept;
    bool parse_gameplay_object(const ScriptObject& object, GameplayItemKind kind,
                               GameplayDefinitionSpec& out,
                               std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool parse_ai_object(const ScriptObject& object, AiActionSpec& out,
                         std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool parse_ai_plan_object(const ScriptObject& object, AiPlanSpec& out,
                              std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    SymbolTable& symbols_;
    SymbolId sym_event_{};
    SymbolId sym_decision_{};
    SymbolId sym_journal_{};
    SymbolId sym_ai_action_{};
    SymbolId sym_ai_plan_{};
    SymbolId sym_scope_{};
    SymbolId sym_potential_{};
    SymbolId sym_allow_{};
    SymbolId sym_effect_{};
    SymbolId sym_completion_{};
    SymbolId sym_cooldown_ticks_{};
    SymbolId sym_auto_trigger_{};
    SymbolId sym_option_{};
    SymbolId sym_key_{};
    SymbolId sym_valid_{};
    SymbolId sym_utility_{};
    SymbolId sym_base_utility_{};
    SymbolId sym_priority_{};
    SymbolId sym_base_priority_{};
    SymbolId sym_commitment_ticks_{};
    SymbolId sym_action_{};

    std::vector<GameplayDefinitionSpec> gameplay_;
    std::vector<AiActionSpec> ai_actions_;
    std::vector<AiPlanSpec> ai_plans_;
    std::unordered_map<std::uint32_t, std::uint32_t> gameplay_lookup_;
    std::unordered_map<std::uint32_t, std::uint32_t> ai_lookup_;
    std::unordered_map<std::uint32_t, std::uint32_t> ai_plan_lookup_;
};

} // namespace thunder
