#include "thunder/content/definition/WorldContentBinder.hpp"

#include <string>

namespace thunder {

WorldContentBinder::WorldContentBinder(const DefinitionDatabase& definitions)
    : definitions_(definitions) {
    runtime_country_lookup_.reserve(512u);
}

void WorldContentBinder::instantiate_countries(World& world) {
    runtime_country_lookup_.clear();
    const auto& symbols = definitions_.symbols();
    world.countries.reserve(world.countries.size() + definitions_.countries().size());
    for (const auto& definition : definitions_.countries()) {
        const auto id = world.countries.create({std::string{symbols.text(definition.tag)},
                                                 definition.population,
                                                 definition.gdp,
                                                 definition.treasury,
                                                 definition.tax_rate});
        runtime_country_lookup_.emplace(definition.tag.value(), id);
    }
}

bool WorldContentBinder::hydrate_countries(
    World& world, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    runtime_country_lookup_.clear();
    const auto& symbols = definitions_.symbols();
    bool ok = true;
    for (std::size_t index = 0; index < world.countries.size(); ++index) {
        const auto id = CountryId{static_cast<CountryId::rep_type>(index)};
        const auto tag = world.countries.tag(id);
        const auto symbol = symbols.find(tag);
        const auto* definition = definitions_.find_country(symbol);
        if (definition == nullptr) {
            diagnostics.push_back({"world pack country has no content definition: " + std::string{tag}, 0});
            ok = false;
            continue;
        }
        world.countries.set_population(id, definition->population);
        world.countries.set_gdp(id, definition->gdp);
        world.countries.set_treasury(id, definition->treasury);
        world.countries.set_tax_rate(id, definition->tax_rate);
        runtime_country_lookup_.emplace(symbol.value(), id);
    }
    return ok;
}

bool WorldContentBinder::instantiate_entities(
    World& world, const EconomyDefinitions& economy,
    std::vector<ScriptCompileDiagnostic>& diagnostics) {
    // Placement of authoritative buildings and POPs is intentionally owned by
    // the world-pack/content composition stage.  The current DefinitionDatabase
    // exposes economy *types* only; silently inventing entities here would make
    // a content load depend on a renderer/map fallback.  Keep this hook as a
    // validated no-op until the placed-entity schema is added to both sides.
    (void)world;
    (void)economy;
    (void)diagnostics;
    return true;
}

void WorldContentBinder::apply_history(std::int32_t yyyymmdd, World& world) {
    ScriptVm vm{definitions_.script_registry(), &definitions_.scripts()};
    for (const auto& patch : definitions_.scripts().history()) {
        if (patch.yyyymmdd > yyyymmdd) continue;
        const auto id = runtime_country(patch.target);
        if (!id.valid()) continue;
        vm.apply(patch.effects, world, ScopeRef::country(id));
    }
}

CountryId WorldContentBinder::runtime_country(SymbolId tag) const noexcept {
    const auto it = runtime_country_lookup_.find(tag.value());
    return it == runtime_country_lookup_.end() ? CountryId{} : it->second;
}

} // namespace thunder
