#include "thunder/content/definition/DefinitionDatabase.hpp"
#include "thunder/simulation/research/ResearchSystem.hpp"
#include "thunder/runtime/engine/ThunderEngine.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

using namespace thunder;

namespace {

constexpr std::string_view research_content = R"THUNDER(
    script research_available {
        scope = country
        trigger = { treasury_above = 0 }
    }
    script research_completed {
        scope = country
        effect = { add_treasury = 7 }
    }
    research_rules default {
        base_innovation_milli = 10000
        innovation_per_million_literate_population_milli = 10000
        max_innovation_milli = 100000
    }
    technology empiricism {
        category = society
        era = 1
        cost = 20000
        potential = research_available
        on_researched = research_completed
        unlocks = { feature = universities }
    }
    technology rail_transport {
        category = production
        era = 2
        cost = 20000
        prerequisites = { technology = empiricism }
        unlocks = { building = railway }
    }
)THUNDER";

struct BoundResearchContent {
    explicit BoundResearchContent(const ScriptRegistry& registry)
        : definitions(symbols, registry) {}

    bool load_and_bind(std::string_view text, ResearchSystem& research,
                       std::vector<ScriptCompileDiagnostic>& diagnostics) {
        ThunderScriptParser parser{symbols};
        const auto parsed = parser.parse(text, "research_test.thunder");
        bool ok = definitions.ingest(parsed, diagnostics);
        ok &= definitions.compile_scripts(parsed, diagnostics);
        ok &= definitions.bind_research(research, diagnostics);
        return ok;
    }

    SymbolTable symbols;
    DefinitionDatabase definitions;
};

CountryId seed_research_world(World& world) {
    const auto country = world.countries.create({"AAA", 2'000'000.0, 100.0, 10.0, 0.2});
    const auto state = world.geography.create_state({"alpha", country, {}, {}});
    const auto province = world.geography.create_province({"alpha.1", state, country, {}, 0.0, 0.0, 100u});
    world.geography.set_state_capital(state, province);
    PopInit pop;
    pop.size = 2'000'000u;
    pop.province = province;
    pop.literacy_permyriad = 5'000u;
    (void)world.pops.create(pop);
    return country;
}

void add_simple_definitions(ResearchSystem& research, bool reverse = false) {
    research.set_rules({10'000u, 0u, 100'000u});
    TechnologyDefinition first;
    first.key = "first";
    first.cost_milli = 40'000u;
    TechnologyDefinition second;
    second.key = "second";
    second.cost_milli = 40'000u;
    second.prerequisites = {script_symbol_hash("first")};
    if (reverse) {
        (void)research.add_or_replace_definition(std::move(second));
        (void)research.add_or_replace_definition(std::move(first));
    } else {
        (void)research.add_or_replace_definition(std::move(first));
        (void)research.add_or_replace_definition(std::move(second));
    }
    std::vector<std::string> diagnostics;
    assert(research.finalize_definitions(diagnostics));
    assert(diagnostics.empty());
}

void test_content_driven_queue_prerequisites_and_completion_effect() {
    const auto registry = ScriptRegistry::make_builtin();
    ResearchSystem research{registry};
    BoundResearchContent content{registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(content.load_and_bind(research_content, research, diagnostics));
    assert(diagnostics.empty());
    assert(research.definitions().size() == 2u);
    assert(research.rules().base_innovation_milli == 10'000u);

    World world;
    const auto country = seed_research_world(world);
    const auto empiricism = script_symbol_hash("empiricism");
    const auto rail = script_symbol_hash("rail_transport");

    // Queue a dependent item first. It remains stable in the authoritative
    // queue while the first eligible prerequisite becomes active.
    assert(research.enqueue(world, country, rail));
    assert(research.enqueue(world, country, empiricism));
    assert(!research.enqueue(world, country, empiricism));
    const auto active = research.active_research(world, country);
    assert(active.valid());
    assert(world.grand_strategy.technologys()[active.value()].key_hash == empiricism);

    const auto checksum_before = world.checksum();
    const auto first_tick = research.run_weekly(world);
    assert(first_tick.countries_with_research == 1u);
    assert(first_tick.completed_technologies == 1u);
    assert(research.innovation_milli().size() == 1u);
    // 2M population at 50% literacy contributes one million literate people:
    // 10k base + 10k configured population innovation completes a 20k tech.
    assert(research.innovation_milli()[0] == 20'000u);
    assert(research.completed(world, country, empiricism));
    assert(world.countries.treasury(country) == 17.0);
    assert(world.checksum() != checksum_before);

    const auto second_tick = research.run_weekly(world);
    assert(second_tick.completed_technologies == 1u);
    assert(research.completed(world, country, rail));
    assert(!research.active_research(world, country).valid());
    assert(research.validate_state(world));

    const auto* rail_definition = research.find("rail_transport");
    assert(rail_definition != nullptr);
    assert(rail_definition->unlock_keys.size() == 1u);
    assert(rail_definition->unlock_keys[0] == script_symbol_hash("railway"));
}

void test_definition_validation_rejects_cycles_and_unknown_state() {
    const auto registry = ScriptRegistry::make_builtin();
    ResearchSystem cyclic{registry};
    TechnologyDefinition a;
    a.key = "a";
    a.prerequisites = {script_symbol_hash("b")};
    TechnologyDefinition b;
    b.key = "b";
    b.prerequisites = {script_symbol_hash("a")};
    (void)cyclic.add_or_replace_definition(std::move(a));
    (void)cyclic.add_or_replace_definition(std::move(b));
    std::vector<std::string> graph_diagnostics;
    assert(!cyclic.finalize_definitions(graph_diagnostics));
    assert(!graph_diagnostics.empty());

    ResearchSystem research{registry};
    add_simple_definitions(research);
    World world;
    const auto country = world.countries.create({"AAA", 1.0, 1.0, 1.0, 0.1});
    world.grand_strategy.add_technology({country, script_symbol_hash("missing"), 0u, false});
    assert(!research.validate_state(world));
}

void test_authoritative_research_save_load_and_determinism() {
    ThunderEngine source{{0u, 0xABCDu, 0x9876u}};
    add_simple_definitions(source.research(), false);
    const auto country = source.world().countries.create({"AAA", 1.0, 1.0, 1.0, 0.1});
    assert(source.research().enqueue(source.world(), country, "second"));
    assert(source.research().enqueue(source.world(), country, "first"));
    source.advance_ticks(28u);
    assert(source.world().grand_strategy.technologys().size() == 2u);
    assert(source.world().grand_strategy.technologys()[1].progress_ppm == 250'000u);
    const auto checksum_before = source.engine_checksum();
    const auto save = source.make_save();

    ThunderEngine restored{{0u, 0xABCDu, 0x9876u}};
    // Definition registration order is not authoritative; stable hashes bind
    // saved state to the same content after a mod loader reorder.
    add_simple_definitions(restored.research(), true);
    restored.restore(save.bytes);
    assert(restored.engine_checksum() == checksum_before);
    assert(restored.validate_world());
    assert(restored.research().queued(restored.world(), CountryId{0u}, script_symbol_hash("first")));

    for (std::uint32_t week = 0; week < 12u; ++week) {
        source.advance_ticks(28u);
        restored.advance_ticks(28u);
        assert(source.engine_checksum() == restored.engine_checksum());
    }
    assert(source.research().completed(source.world(), CountryId{0u}, script_symbol_hash("first")));
    assert(source.research().completed(source.world(), CountryId{0u}, script_symbol_hash("second")));
}

void test_tech_eras_and_natural_spread() {
    ResearchSystem research{ScriptRegistry::make_builtin()};
    ResearchRules rules{};
    rules.base_innovation_milli = 10'000u;
    rules.tech_spread_rate_ppm = 200'000u;
    rules.tech_spread_base_chance_ppm = 350'000u;
    rules.min_era_techs_required = 1u;
    research.set_rules(rules);

    TechnologyDefinition era0_tech{"traditional_farming", script_symbol_hash("traditional_farming"), TechnologyCategory::Production, 0u, 10'000u, {}, {}, {}, {}};
    TechnologyDefinition era1_tech{"steam_engine", script_symbol_hash("steam_engine"), TechnologyCategory::Production, 1u, 10'000u, {script_symbol_hash("traditional_farming")}, {}, {}, {}};
    research.add_or_replace_definition(era0_tech);
    research.add_or_replace_definition(era1_tech);
    std::vector<std::string> diags;
    assert(research.finalize_definitions(diags));

    World world;
    const auto countryA = world.countries.create({"A", 100.0, 10.0, 100.0, 0.2});
    const auto countryB = world.countries.create({"B", 100.0, 10.0, 100.0, 0.2});

    // Country A unlocks both era 0 and era 1 techs
    world.grand_strategy.add_technology({countryA, era0_tech.key_hash, 1'000'000u, true});
    world.grand_strategy.add_technology({countryA, era1_tech.key_hash, 1'000'000u, true});
    assert(research.completed(world, countryA, era0_tech.key_hash));
    assert(research.completed(world, countryA, era1_tech.key_hash));

    // Country B cannot actively research era 1 tech until era 0 tech is unlocked
    assert(research.is_era_unlocked(world, countryB, 1u) == false);
    assert(research.enqueue(world, countryB, era1_tech.key_hash) == true);
    assert(!research.active_research(world, countryB).valid());

    // Friendly relations between A and B facilitate natural tech spread
    world.grand_strategy.adjust_relation(countryA, countryB, 50'000);

    // Run tech spread for 180 weeks.
    // Verify that era1_tech NEVER spreads while traditional_farming is not completed!
    for (int w = 0; w < 180; ++w) {
        research.run_tech_spread_weekly(world);
        if (!research.completed(world, countryB, era0_tech.key_hash)) {
            for (const auto& r : world.grand_strategy.technologys()) {
                if (r.country == countryB && r.key_hash == era1_tech.key_hash) {
                    assert(r.progress_ppm == 0u);
                }
            }
        }
    }

    // Country B should have accumulated tech spread and completed traditional_farming
    assert(world.grand_strategy.technologys().size() >= 2);
    assert(research.completed(world, countryB, era0_tech.key_hash) == true);
    assert(research.is_era_unlocked(world, countryB, 1u) == true);
}

void test_probabilistic_tech_spread_and_prerequisite_blocking() {
    ResearchSystem research{ScriptRegistry::make_builtin()};
    ResearchRules rules{};
    rules.tech_spread_rate_ppm = 100'000u;
    rules.tech_spread_base_chance_ppm = 200'000u; // 20% base probability
    rules.min_era_techs_required = 0u;
    research.set_rules(rules);

    const auto h_foundational = script_symbol_hash("foundational_math");
    const auto h_advanced = script_symbol_hash("advanced_calculus");

    TechnologyDefinition foundational{"foundational_math", h_foundational, TechnologyCategory::Society, 0u, 20'000u, {}, {}, {}, {}};
    TechnologyDefinition advanced{"advanced_calculus", h_advanced, TechnologyCategory::Society, 0u, 20'000u, {h_foundational}, {}, {}, {}};

    research.add_or_replace_definition(foundational);
    research.add_or_replace_definition(advanced);
    std::vector<std::string> diags;
    assert(research.finalize_definitions(diags));

    World world;
    const auto leader = world.countries.create({"Leader", 100.0, 10.0, 100.0, 0.2});
    const auto learner = world.countries.create({"Learner", 100.0, 10.0, 100.0, 0.2});

    // Leader country has both technologies unlocked
    world.grand_strategy.add_technology({leader, h_foundational, 1'000'000u, true});
    world.grand_strategy.add_technology({leader, h_advanced, 1'000'000u, true});

    // Active trade agreement boosts spread probability
    world.grand_strategy.add_treaty({leader, learner, TreatyKind::TradeAgreement, 0u, true});

    std::uint32_t ticks_with_spread = 0u;
    std::uint32_t ticks_without_spread = 0u;
    std::uint32_t previous_progress = 0u;

    // Simulate 40 weeks of natural interaction
    for (int w = 0; w < 40; ++w) {
        research.run_tech_spread_weekly(world);

        // Verify: Advanced Calculus MUST NOT receive any spread progress while foundational_math is incomplete
        if (!research.completed(world, learner, h_foundational)) {
            for (const auto& r : world.grand_strategy.technologys()) {
                if (r.country == learner && r.key_hash == h_advanced) {
                    assert(r.progress_ppm == 0u);
                }
            }
        }

        // Track spread progress for foundational_math
        std::uint32_t current_progress = 0u;
        for (const auto& r : world.grand_strategy.technologys()) {
            if (r.country == learner && r.key_hash == h_foundational) {
                current_progress = r.progress_ppm;
                break;
            }
        }

        if (current_progress > previous_progress) {
            ++ticks_with_spread;
            previous_progress = current_progress;
        } else {
            ++ticks_without_spread;
        }
    }

    // Verify probabilistic behavior: spread occurs on some weeks, but not all weeks
    assert(ticks_with_spread > 0u);
    assert(ticks_without_spread > 0u);
    assert(previous_progress > 0u);
}

void test_thunder_engine_research_spread_survives_save_boundary() {
    ThunderEngine source{{0u, 0xA11CEu, 0xBEEFu}};
    add_simple_definitions(source.research());
    source.research().set_rules({10'000u, 0u, 100'000u, 1'000'000u, 1'000'000u, 0u});
    const auto leader = source.world().countries.create({"LEA", 1.0, 1.0, 1.0, 0.2});
    const auto learner = source.world().countries.create({"LRN", 1.0, 1.0, 1.0, 0.2});
    const auto first = script_symbol_hash("first");
    source.world().grand_strategy.add_technology({leader, first, 1'000'000u, true});
    source.world().grand_strategy.add_treaty(
        {leader, learner, TreatyKind::TradeAgreement, 0u, true});

    // The engine must run the data-driven spread phase at the weekly boundary.
    source.advance_ticks(28u);
    assert(source.world().grand_strategy.technologys().size() == 2u);
    assert(source.world().grand_strategy.technologys()[1].progress_ppm == 25'000u);
    const auto save = source.make_save();

    ThunderEngine restored{{0u, 0xA11CEu, 0xBEEFu}};
    add_simple_definitions(restored.research());
    restored.research().set_rules({10'000u, 0u, 100'000u, 1'000'000u, 1'000'000u, 0u});
    restored.restore(save.bytes);
    for (std::uint32_t week = 0; week < 45u; ++week) {
        source.advance_ticks(28u);
        restored.advance_ticks(28u);
        assert(source.engine_checksum() == restored.engine_checksum());
    }
    assert(source.world().grand_strategy.technologys()[1].unlocked);
    assert(restored.world().grand_strategy.technologys()[1].unlocked);
}

} // namespace

int main() {
    test_content_driven_queue_prerequisites_and_completion_effect();
    test_definition_validation_rejects_cycles_and_unknown_state();
    test_authoritative_research_save_load_and_determinism();
    test_tech_eras_and_natural_spread();
    test_probabilistic_tech_spread_and_prerequisite_blocking();
    test_thunder_engine_research_spread_survives_save_boundary();
    std::cout << "Thunder 1.0 research system tests: PASS\n";
    return 0;
}
