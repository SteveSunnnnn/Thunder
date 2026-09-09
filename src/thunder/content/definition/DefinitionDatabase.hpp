#pragma once
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include "thunder/content/definition/LocalizationDatabase.hpp"
#include "thunder/content/definition/GameplayContent.hpp"
#include "thunder/content/definition/NotificationContent.hpp"
#include "thunder/content/definition/OnActionContent.hpp"
#include "thunder/content/definition/ResearchContent.hpp"
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace thunder {

struct CountryDefinition {
    SymbolId tag{};
    double population = 0.0;
    double gdp = 0.0;
    double treasury = 0.0;
    double tax_rate = 0.20;
    std::array<std::uint8_t, 4> map_color{160u, 160u, 160u, 255u};
};

struct EconomyFlowSpec {
    SymbolId good{};
    std::int64_t quantity_milli = 0;
};

struct GoodContentDefinition {
    SymbolId key{};
    std::int64_t base_price_milli = 1000;
    SymbolId category{}; // staple / luxury / military / industrial / currency; empty = staple
    SymbolId monetary_metal{}; // gold / silver; only valid with category = currency
};

struct BuildingTypeContentDefinition {
    SymbolId key{};
    SymbolId visual{};
    std::uint32_t workers_per_level = 1000;
    std::vector<EconomyFlowSpec> inputs;
    std::vector<EconomyFlowSpec> outputs;
};

struct ProductionMethodContentDefinition {
    SymbolId key{};
    SymbolId building_type{};
    std::int32_t throughput_ppm = 1'000'000;
    std::vector<EconomyFlowSpec> inputs;
    std::vector<EconomyFlowSpec> outputs;
};

struct NeedProfileContentDefinition {
    SymbolId key{};
    std::vector<EconomyFlowSpec> needs;
};

struct BuildingContentDefinition {
    SymbolId key{};
    SymbolId type{};
    SymbolId province{};
    SymbolId state{};
    SymbolId production_method{};
    std::uint16_t level = 1u;
};

struct PopContentDefinition {
    SymbolId key{};
    SymbolId province{};
    SymbolId state{};
    SymbolId employer{};
    SymbolId need_profile{};
    PopulationCount size = 0u;
    std::uint16_t literacy_permyriad = 0u;
    std::uint16_t qualification_permyriad = 0u;
    std::int32_t wealth_milli = 0;
    std::uint32_t political_strength_milli = 0u;
};

class DefinitionDatabase {
public:
    DefinitionDatabase(SymbolTable& symbols, const ScriptRegistry& registry);

    bool ingest(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool compile_scripts(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool ingest_gameplay(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool bind_gameplay(ScriptedGameplayRuntime& gameplay, UtilityAiEngine& ai,
                       std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool bind_research(ResearchSystem& research,
                       std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool bind_notifications(NotificationRuntime& notifications,
                            std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool bind_on_actions(const ScriptedGameplayRuntime& gameplay, OnActionRuntime& on_actions,
                         std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool bind_economy(EconomyDefinitions& economy,
                      std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    void instantiate_world(World& world);
    void apply_history(std::int32_t yyyymmdd, World& world);

    [[nodiscard]] const CountryDefinition* find_country(SymbolId tag) const noexcept;
    [[nodiscard]] CountryId runtime_country(SymbolId tag) const noexcept;
    [[nodiscard]] std::span<const CountryDefinition> countries() const noexcept { return countries_; }
    [[nodiscard]] bool country_map_color(std::string_view tag,
                                          std::array<std::uint8_t, 4>& out) const noexcept;
    [[nodiscard]] const ScriptProgramDatabase& scripts() const noexcept { return scripts_; }
    [[nodiscard]] ScriptProgramDatabase& scripts() noexcept { return scripts_; }
    [[nodiscard]] const SymbolTable& symbols() const noexcept { return symbols_; }
    [[nodiscard]] const ScriptRegistry& script_registry() const noexcept { return registry_; }
    [[nodiscard]] const LocalizationDatabase& localization() const noexcept { return localization_; }
    [[nodiscard]] LocalizationDatabase& localization() noexcept { return localization_; }
    [[nodiscard]] const GameplayContentDatabase& gameplay_content() const noexcept { return gameplay_content_; }
    [[nodiscard]] const ResearchContentDatabase& research_content() const noexcept { return research_content_; }
    [[nodiscard]] const NotificationContentDatabase& notification_content() const noexcept {
        return notification_content_;
    }
    [[nodiscard]] const OnActionContentDatabase& on_action_content() const noexcept {
        return on_action_content_;
    }
    [[nodiscard]] std::span<const GoodContentDefinition> goods() const noexcept { return goods_; }
    [[nodiscard]] std::span<const BuildingTypeContentDefinition> building_types() const noexcept { return building_types_; }
    [[nodiscard]] std::span<const ProductionMethodContentDefinition> production_methods() const noexcept { return production_methods_; }
    [[nodiscard]] std::span<const NeedProfileContentDefinition> need_profiles() const noexcept { return need_profiles_; }
    [[nodiscard]] std::span<const BuildingContentDefinition> buildings() const noexcept { return buildings_; }
    [[nodiscard]] std::span<const PopContentDefinition> pops() const noexcept { return pops_; }
    [[nodiscard]] std::size_t immutable_bytes() const noexcept;

public:
    // Generic schema registration: engine provides storage/mechanics,
    // content provides keys/rules/thresholds. This is the extension point
    // for future economy/politics/diplomacy/warfare definition categories
    // without hard-coding game-specific tables into C++.
    enum class DefinitionMergePolicy : std::uint8_t { Replace, Patch, Extend, Remove };
    struct GenericDefinitionSchema {
        std::string category; // stable key e.g. "building", "law_group", "goods"
        DefinitionMergePolicy default_policy = DefinitionMergePolicy::Replace;
        // Validator receives parsed object; returns false + diagnostic on error.
        std::function<bool(const struct ScriptParseResult& parsed,
                           std::vector<ScriptCompileDiagnostic>& diagnostics)> ingest;
        std::function<std::size_t()> immutable_bytes;
    };
    void register_schema(GenericDefinitionSchema schema);
    [[nodiscard]] std::span<const GenericDefinitionSchema> generic_schemas() const noexcept { return generic_schemas_; }
    [[nodiscard]] bool has_schema(std::string_view category) const noexcept;

private:
    SymbolTable& symbols_;
    const ScriptRegistry& registry_;
    ScriptCompiler compiler_;
    ScriptProgramDatabase scripts_;
    LocalizationDatabase localization_;
    GameplayContentDatabase gameplay_content_;
    ResearchContentDatabase research_content_;
    NotificationContentDatabase notification_content_;
    OnActionContentDatabase on_action_content_;
    std::vector<CountryDefinition> countries_;
    std::vector<GoodContentDefinition> goods_;
    std::vector<BuildingTypeContentDefinition> building_types_;
    std::vector<ProductionMethodContentDefinition> production_methods_;
    std::vector<NeedProfileContentDefinition> need_profiles_;
    std::unordered_map<std::uint32_t, std::uint32_t> country_lookup_;
    std::unordered_map<std::uint32_t, CountryId> runtime_country_lookup_;
    std::vector<GenericDefinitionSchema> generic_schemas_;
    SymbolId sym_country_{};
    SymbolId sym_population_{};
    SymbolId sym_gdp_{};
    SymbolId sym_treasury_{};
    SymbolId sym_tax_rate_{};
    SymbolId sym_good_{};
    SymbolId sym_building_type_{};
    SymbolId sym_production_method_{};
    SymbolId sym_need_profile_{};
    SymbolId sym_base_price_milli_{};
    SymbolId sym_workers_per_level_{};
    SymbolId sym_throughput_ppm_{};
    SymbolId sym_input_{};
    SymbolId sym_output_{};
    SymbolId sym_need_{};
    SymbolId sym_quantity_milli_{};
    SymbolId sym_category_{};
    SymbolId sym_staple_{};
    SymbolId sym_luxury_{};
    SymbolId sym_military_{};
    SymbolId sym_industrial_{};
    SymbolId sym_currency_{};
    SymbolId sym_map_color_{};
    SymbolId sym_red_{};
    SymbolId sym_green_{};
    SymbolId sym_blue_{};
    SymbolId sym_alpha_{};
    SymbolId sym_building_{};
    SymbolId sym_pop_{};
    SymbolId sym_type_{};
    SymbolId sym_province_{};
    SymbolId sym_state_{};
    SymbolId sym_level_{};
    SymbolId sym_size_{};
    SymbolId sym_employer_{};
    SymbolId sym_literacy_permyriad_{};
    SymbolId sym_qualification_permyriad_{};
    SymbolId sym_wealth_milli_{};
    SymbolId sym_political_strength_milli_{};
    SymbolId sym_visual_{};
    SymbolId sym_monetary_metal_{};
    SymbolId sym_gold_{};
    SymbolId sym_silver_{};
    SymbolId sym_construction_goods_{};
    std::vector<BuildingContentDefinition> buildings_;
    std::vector<PopContentDefinition> pops_;
    std::vector<EconomyFlowSpec> construction_goods_inputs_;
};

} // namespace thunder
