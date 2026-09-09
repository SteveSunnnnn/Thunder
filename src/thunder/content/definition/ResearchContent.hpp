#pragma once

#include "thunder/simulation/research/ResearchSystem.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace thunder {

struct TechnologyDefinitionSpec {
    SymbolId key{};
    SymbolId category{};
    std::uint16_t era = 0u;
    std::uint32_t cost_milli = 100'000u;
    std::vector<SymbolId> prerequisites;
    std::vector<SymbolId> unlock_keys;
    SymbolId potential{};
    SymbolId on_researched{};
    std::uint32_t line = 0u;
};

class ResearchContentDatabase {
public:
    explicit ResearchContentDatabase(SymbolTable& symbols);

    bool ingest(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool bind(const ScriptProgramDatabase& programs, ResearchSystem& research,
              std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    [[nodiscard]] std::span<const TechnologyDefinitionSpec> technologies() const noexcept {
        return technologies_;
    }
    [[nodiscard]] const ResearchRules& rules() const noexcept { return rules_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] bool parse_u16(const ScriptNode& node, std::uint16_t& value) const noexcept;
    [[nodiscard]] bool parse_u32(const ScriptNode& node, std::uint32_t& value) const noexcept;
    bool parse_technology(const ScriptObject& object, TechnologyDefinitionSpec& out,
                          std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool parse_rules(const ScriptObject& object, ResearchRules& out,
                     std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    SymbolTable& symbols_;
    SymbolId sym_technology_{};
    SymbolId sym_research_rules_{};
    SymbolId sym_category_{};
    SymbolId sym_era_{};
    SymbolId sym_cost_{};
    SymbolId sym_prerequisites_{};
    SymbolId sym_prerequisite_{};
    SymbolId sym_unlocks_{};
    SymbolId sym_potential_{};
    SymbolId sym_on_researched_{};
    SymbolId sym_base_innovation_{};
    SymbolId sym_literate_innovation_{};
    SymbolId sym_max_innovation_{};
    SymbolId sym_tech_spread_rate_{};
    SymbolId sym_tech_spread_base_chance_{};

    ResearchRules rules_{};
    std::vector<TechnologyDefinitionSpec> technologies_;
    std::unordered_map<std::uint32_t, std::uint32_t> technology_lookup_;
};

} // namespace thunder
