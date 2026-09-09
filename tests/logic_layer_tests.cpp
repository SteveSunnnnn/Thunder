#include "thunder/content/definition/DefinitionDatabase.hpp"
#include "thunder/runtime/engine/ThunderEngine.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string_view>

using namespace thunder;

namespace {

struct Fixture {
    ThunderEngine engine{{0u, 0u, 0u}};
    GoodId grain{};
    NeedProfileId need{};
    CountryId country{};
    MarketId market{};
    StateId state{};
    ProvinceId province{};
    PopId first_pop{};
    PopId second_pop{};

    Fixture() {
        auto& definitions = engine.definitions();
        grain = definitions.add_good({"grain", 1000});
        const NeedFlow needs[]{{grain, 10}};
        need = definitions.add_need_profile("basic", needs);
        auto& world = engine.world();
        country = world.countries.create({"AAA", 1000.0, 500.0, 1000.0, 0.2});
        world.markets.resize(1u, definitions);
        market = MarketId{0u};
        world.markets.set_owner(market, country);
        state = world.geography.create_state({"state_a", country, market, {}});
        province = world.geography.create_province({"province_a", state, country, market, 0.0, 0.0, 10u});
        world.geography.set_state_capital(state, province);

        PopInit first{};
        first.market = market;
        first.size = 100;
        first.need_profile = need;
        first.province = province;
        first.literacy_permyriad = 6000;
        first.wealth_milli = 10000;
        first_pop = world.pops.create(first);

        PopInit second = first;
        second.size = 50;
        second.literacy_permyriad = 3000;
        second.wealth_milli = 12000;
        second_pop = world.pops.create(second);
    }
};

ScriptProgramDatabase compile_programs(SymbolTable& symbols, const ScriptRegistry& registry,
                                       std::string_view source) {
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(source, "logic_layer.thunder");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    const bool ok = compiler.compile(parsed, programs, diagnostics);
    if (!ok) {
        for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
    }
    assert(ok);
    assert(diagnostics.empty());
    return programs;
}

void test_scope_resolver_and_builtin_primitives() {
    Fixture fixture;
    auto& world = fixture.engine.world();
    auto& registry = fixture.engine.scripts();
    assert(ScopeResolver::owner(world, ScopeRef::pop(fixture.first_pop)).raw_id == fixture.country.value());
    assert(ScopeResolver::state(world, ScopeRef::pop(fixture.first_pop)).raw_id == fixture.state.value());
    assert(ScopeResolver::province(world, ScopeRef::pop(fixture.first_pop)).raw_id == fixture.province.value());
    assert(ScopeResolver::market(world, ScopeRef::pop(fixture.first_pop)).raw_id == fixture.market.value());
    assert(ScopeResolver::children(world, ScopeRef::country(fixture.country), ScopeType::Pop).size() == 2u);
    assert(registry.evaluate_trigger("state_population_above", world, ScopeRef::state(fixture.state), 100.0));
    assert(registry.evaluate_trigger("pop_literacy_above", world, ScopeRef::pop(fixture.first_pop), 0.5));
    registry.execute_effect("set_pop_wealth", world, ScopeRef::pop(fixture.first_pop), 20.0);
    assert(world.pops.wealth_milli(fixture.first_pop) == 20000);

    // Destroyed SoA rows must disappear from script traversal immediately;
    // otherwise a late event can resolve a dead POP and throw from an accessor.
    world.pops.destroy(fixture.second_pop);
    assert(!ScopeResolver::valid(world, ScopeRef::pop(fixture.second_pop)));
    assert(ScopeResolver::owner(world, ScopeRef::pop(fixture.second_pop)) == ScopeRef{});
    assert(ScopeResolver::children(world, ScopeRef::country(fixture.country), ScopeType::Pop).size() == 1u);
    assert(ScopeResolver::all(world, ScopeType::Pop).size() == 1u);
}

void test_thunderscript_2_scope_traversal_saved_scope_and_script_calls() {
    Fixture fixture;
    auto& world = fixture.engine.world();
    auto& registry = fixture.engine.scripts();
    SymbolTable symbols;
    const auto programs = compile_programs(symbols, registry, R"THUNDER(
        script literate_pop {
            scope = pop
            trigger = { pop_literacy_above = 0.5 }
            effect = { set_pop_wealth = 40 }
        }
        script country_scope_walk {
            scope = country
            trigger = {
                any_pop = { scripted_trigger = literate_pop }
                every_state = { state_population_above = 100 }
            }
            effect = {
                every_pop = {
                    save_scope_as = selected_pop
                    set_pop_wealth = 30
                    owner = { add_treasury = 1 }
                    saved:selected_pop = { set_pop_literacy = 0.75 }
                }
            }
        }
        script pop_prev_root {
            scope = pop
            trigger = {
                owner = {
                    treasury_above = 500
                    PREV = { pop_size_above = 10 }
                    ROOT = { pop_literacy_above = 0.5 }
                }
            }
        }
        script from_country {
            scope = country
            trigger = { FROM = { pop_literacy_above = 0.5 } }
        }
        script call_effects {
            scope = country
            effect = { every_pop = { scripted_effect = literate_pop } }
        }
        scripted_value pop_literacy_score {
            scope = pop
            source = literacy
            multiply = 2
            add = 0.1
        }
    )THUNDER");

    ScriptVm vm{registry, &programs};
    const auto* walk = programs.find_script(symbols.find("country_scope_walk"));
    assert(walk != nullptr);
    assert(!walk->scoped_conditions.empty());
    assert(!walk->scoped_effects.empty());
    assert(vm.execute_if(*walk, world, ScopeRef::country(fixture.country), {}, 77u));
    assert(world.pops.wealth_milli(fixture.first_pop) == 30000);
    assert(world.pops.wealth_milli(fixture.second_pop) == 30000);
    assert(world.pops.literacy_permyriad(fixture.first_pop) == 7500u);
    assert(world.pops.literacy_permyriad(fixture.second_pop) == 7500u);
    assert(std::abs(world.countries.treasury(fixture.country) - 1002.0) < 1e-9);

    const auto* prev_root = programs.find_script(symbols.find("pop_prev_root"));
    assert(prev_root != nullptr);
    assert(vm.evaluate(*prev_root, world, ScopeRef::pop(fixture.first_pop)));

    const auto* from_country = programs.find_script(symbols.find("from_country"));
    assert(from_country != nullptr);
    auto from_context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.country),
                                                        ScopeRef::pop(fixture.first_pop), 123u);
    assert(vm.evaluate(*from_country, world, from_context));

    world.pops.set_wealth_milli(fixture.first_pop, 10000);
    world.pops.set_wealth_milli(fixture.second_pop, 12000);
    const auto* calls = programs.find_script(symbols.find("call_effects"));
    assert(calls != nullptr);
    assert(vm.execute_if(*calls, world, ScopeRef::country(fixture.country)));
    assert(world.pops.wealth_milli(fixture.first_pop) == 40000);
    assert(world.pops.wealth_milli(fixture.second_pop) == 40000);

    const auto* value = programs.find_value(symbols.find("pop_literacy_score"));
    assert(value != nullptr);
    assert(std::abs(vm.evaluate(*value, world, ScopeRef::pop(fixture.first_pop)) - 1.6) < 1e-9);
}

void test_deterministic_random_iterator() {
    Fixture fixture;
    auto& world = fixture.engine.world();
    auto& registry = fixture.engine.scripts();
    SymbolTable symbols;
    const auto programs = compile_programs(symbols, registry, R"THUNDER(
        script random_pop_effect {
            scope = country
            effect = { random_pop = { set_pop_wealth = 77 } }
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};
    const auto* effect = programs.find_script(symbols.find("random_pop_effect"));
    assert(effect != nullptr);

    world.pops.set_wealth_milli(fixture.first_pop, 10000);
    world.pops.set_wealth_milli(fixture.second_pop, 10000);
    assert(vm.execute_if(*effect, world, ScopeRef::country(fixture.country), {}, 0x12345678u));
    const bool first_selected = world.pops.wealth_milli(fixture.first_pop) == 77000;
    const bool second_selected = world.pops.wealth_milli(fixture.second_pop) == 77000;
    assert(first_selected != second_selected);

    world.pops.set_wealth_milli(fixture.first_pop, 10000);
    world.pops.set_wealth_milli(fixture.second_pop, 10000);
    assert(vm.execute_if(*effect, world, ScopeRef::country(fixture.country), {}, 0x12345678u));
    assert((world.pops.wealth_milli(fixture.first_pop) == 77000) == first_selected);
    assert((world.pops.wealth_milli(fixture.second_pop) == 77000) == second_selected);
}

void test_content_defined_events_decisions_journals_and_ai() {
    Fixture fixture;
    auto& world = fixture.engine.world();
    auto& registry = fixture.engine.scripts();
    SymbolTable symbols;
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        script cash_ok {
            scope = country
            trigger = { treasury_above = 500 }
        }
        script rich_enough {
            scope = country
            trigger = { treasury_above = 2000 }
        }
        script add_five {
            scope = country
            effect = { add_treasury = 5 }
        }
        script add_seven {
            scope = country
            effect = { add_treasury = 7 }
        }
        scripted_value cash_utility {
            scope = country
            source = treasury
            multiply = -0.001
            add = 3
        }
        event cash_event {
            scope = country
            potential = cash_ok
            cooldown_ticks = 2
            option = {
                key = accept
                effect = add_seven
            }
        }
        decision raise_funds {
            scope = country
            allow = cash_ok
            effect = add_five
            cooldown_ticks = 4
        }
        journal become_rich {
            scope = country
            potential = cash_ok
            completion = rich_enough
            effect = add_five
        }
        ai_action save_cash {
            scope = country
            valid = cash_ok
            utility = cash_utility
            effect = add_five
            base_utility = 1
            cooldown_ticks = 2
        }
    )THUNDER", "gameplay.thunder");
    assert(parsed.ok());

    DefinitionDatabase definitions{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(definitions.compile_scripts(parsed, diagnostics));
    assert(definitions.ingest_gameplay(parsed, diagnostics));
    assert(diagnostics.empty());

    ScriptedGameplayRuntime gameplay{registry, &definitions.scripts()};
    UtilityAiEngine ai{registry, &definitions.scripts()};
    assert(definitions.bind_gameplay(gameplay, ai, diagnostics));
    assert(diagnostics.empty());
    assert(gameplay.definitions().size() == 3u);
    assert(ai.actions().size() == 1u);

    std::uint32_t event_id = 0u;
    std::uint32_t decision_id = 0u;
    std::uint32_t journal_id = 0u;
    for (std::uint32_t i = 0; i < gameplay.definitions().size(); ++i) {
        const auto& definition = gameplay.definitions()[i];
        if (definition.key == "cash_event") event_id = i;
        else if (definition.key == "raise_funds") decision_id = i;
        else if (definition.key == "become_rich") journal_id = i;
    }

    const auto scope = ScopeRef::country(fixture.country);
    assert(gameplay.fire(event_id, world, scope, 10u));
    assert(gameplay.instances().size() == 1u);
    assert(gameplay.instances()[0].awaiting_choice);
    assert(gameplay.choose_event_option(0u, 0u, world, 11u));
    assert(std::abs(world.countries.treasury(fixture.country) - 1007.0) < 1e-9);
    assert(gameplay.log().size() >= 3u);

    assert(gameplay.take_decision(decision_id, world, scope, 20u));
    assert(std::abs(world.countries.treasury(fixture.country) - 1012.0) < 1e-9);
    assert(!gameplay.take_decision(decision_id, world, scope, 21u));
    assert(gameplay.take_decision(decision_id, world, scope, 24u));
    assert(std::abs(world.countries.treasury(fixture.country) - 1017.0) < 1e-9);

    gameplay.update_journals(world, 30u);
    bool journal_open = false;
    for (const auto& instance : gameplay.instances()) {
        if (instance.definition == journal_id && instance.active) journal_open = true;
    }
    assert(journal_open);
    world.countries.add_treasury(fixture.country, 1000.0);
    gameplay.update_journals(world, 31u);
    bool journal_complete = false;
    std::size_t journal_instances = 0u;
    for (const auto& instance : gameplay.instances()) {
        if (instance.definition == journal_id) {
            ++journal_instances;
            journal_complete |= instance.completed;
        }
    }
    assert(journal_complete);
    assert(journal_instances == 1u);

    const auto before_ai = world.countries.treasury(fixture.country);
    assert(ai.execute_best(world, scope, 40u));
    assert(std::abs(world.countries.treasury(fixture.country) - (before_ai + 5.0)) < 1e-9);
    assert(!ai.execute_best(world, scope, 41u));
    assert(ai.execute_best(world, scope, 42u));
    assert(ai.checksum() != 0u);
    assert(gameplay.checksum() != 0u);
}


void test_content_defined_ai_strategic_plans() {
    Fixture fixture;
    auto& world = fixture.engine.world();
    auto& registry = fixture.engine.scripts();
    SymbolTable symbols;
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        script opportunistic_valid {
            scope = country
            trigger = { treasury_above = 1010 }
        }
        script never_complete {
            scope = country
            trigger = { treasury_above = 1000000 }
        }
        script add_five_plan {
            scope = country
            effect = { add_treasury = 5 }
        }
        script add_fifty_plan {
            scope = country
            effect = { add_treasury = 50 }
        }
        ai_action strategic_step {
            scope = country
            effect = add_five_plan
            base_utility = 1
        }
        ai_action distraction {
            scope = country
            effect = add_fifty_plan
            base_utility = 100
        }
        ai_plan steady_growth {
            scope = country
            completion = never_complete
            action = strategic_step
            base_priority = 10
            commitment_ticks = 10
        }
        ai_plan opportunistic_growth {
            scope = country
            valid = opportunistic_valid
            completion = never_complete
            action = distraction
            base_priority = 20
        }
    )THUNDER", "ai_plans.thunder");
    assert(parsed.ok());

    DefinitionDatabase definitions{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(definitions.compile_scripts(parsed, diagnostics));
    assert(definitions.ingest_gameplay(parsed, diagnostics));
    UtilityAiEngine ai{registry, &definitions.scripts()};
    ScriptedGameplayRuntime gameplay{registry, &definitions.scripts()};
    assert(definitions.bind_gameplay(gameplay, ai, diagnostics));
    assert(diagnostics.empty());
    assert(ai.actions().size() == 2u);
    assert(ai.plans().size() == 2u);

    const auto scope = ScopeRef::country(fixture.country);
    // Stateless utility AI takes the globally highest-utility distraction.
    assert(ai.execute_best(world, scope, 0u));
    assert(std::abs(world.countries.treasury(fixture.country) - 1050.0) < 1e-9);

    // Planned AI selects the higher-priority opportunistic plan because its valid
    // trigger is now true, then persists that strategic intent.
    assert(ai.execute_planned_best(world, scope, 1u));
    assert(std::abs(world.countries.treasury(fixture.country) - 1100.0) < 1e-9);
    assert(ai.plan_state().size() == 1u);
    assert(ai.plans()[ai.plan_state()[0].plan].key == "opportunistic_growth");

    // A second fixture starts below the opportunistic trigger. The committed
    // steady plan must not switch merely because the higher-priority plan becomes
    // valid a few ticks later.
    Fixture committed;
    auto& committed_world = committed.engine.world();
    UtilityAiEngine committed_ai{committed.engine.scripts(), &definitions.scripts()};
    ScriptedGameplayRuntime committed_gameplay{committed.engine.scripts(), &definitions.scripts()};
    // Programs were compiled against an equivalent built-in registry; bind content
    // against the second engine's registry via a new DefinitionDatabase.
    SymbolTable symbols2;
    ThunderScriptParser parser2{symbols2};
    const auto parsed2 = parser2.parse(R"THUNDER(
        script opportunistic_valid { scope = country trigger = { treasury_above = 1010 } }
        script never_complete { scope = country trigger = { treasury_above = 1000000 } }
        script add_five_plan { scope = country effect = { add_treasury = 5 } }
        script add_fifty_plan { scope = country effect = { add_treasury = 50 } }
        ai_action strategic_step { scope = country effect = add_five_plan base_utility = 1 }
        ai_action distraction { scope = country effect = add_fifty_plan base_utility = 100 }
        ai_plan steady_growth { scope = country completion = never_complete action = strategic_step base_priority = 10 commitment_ticks = 10 }
        ai_plan opportunistic_growth { scope = country valid = opportunistic_valid completion = never_complete action = distraction base_priority = 20 }
    )THUNDER", "ai_plans_commitment.thunder");
    assert(parsed2.ok());
    DefinitionDatabase definitions2{symbols2, committed.engine.scripts()};
    std::vector<ScriptCompileDiagnostic> diagnostics2;
    assert(definitions2.compile_scripts(parsed2, diagnostics2));
    assert(definitions2.ingest_gameplay(parsed2, diagnostics2));
    UtilityAiEngine ai2{committed.engine.scripts(), &definitions2.scripts()};
    ScriptedGameplayRuntime gameplay2{committed.engine.scripts(), &definitions2.scripts()};
    assert(definitions2.bind_gameplay(gameplay2, ai2, diagnostics2));
    const auto committed_scope = ScopeRef::country(committed.country);
    assert(ai2.execute_planned_best(committed_world, committed_scope, 1u));
    assert(ai2.plans()[ai2.plan_state()[0].plan].key == "steady_growth");
    for (std::uint64_t tick = 2u; tick <= 10u; ++tick) assert(ai2.execute_planned_best(committed_world, committed_scope, tick));
    assert(ai2.plans()[ai2.plan_state()[0].plan].key == "steady_growth");
    assert(ai2.execute_planned_best(committed_world, committed_scope, 11u));
    assert(ai2.plans()[ai2.plan_state()[0].plan].key == "opportunistic_growth");
}

} // namespace

void test_script_conditional_effects_if_else() {
    // `if = { limit = { ... } ... else = { ... } }` compiles into
    // ScopedEffectKind::If and executes exactly one branch, in scope.
    Fixture fixture;
    auto& world = fixture.engine.world();
    auto& registry = fixture.engine.scripts();
    SymbolTable symbols;
    const auto programs = compile_programs(symbols, registry, R"THUNDER(
        script wealth_branch {
            scope = country
            effect = {
                if = {
                    limit = { treasury_above = 5000 }
                    every_pop = { set_pop_wealth = 90 }
                }
                else = {
                    every_pop = { set_pop_wealth = 10 }
                }
            }
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};
    const auto* branch = programs.find_script(symbols.find("wealth_branch"));
    assert(branch != nullptr);
    assert(!branch->scoped_effects.empty());
    assert(branch->scoped_effects.front().kind == ScopedEffectKind::If);

    // Treasury starts at 1000 (below 5000): the else branch runs.
    assert(vm.execute_if(*branch, world, ScopeRef::country(fixture.country), {}, 91u));
    assert(world.pops.wealth_milli(fixture.first_pop) == 10000);
    assert(world.pops.wealth_milli(fixture.second_pop) == 10000);

    // Raise the treasury above the limit: the taken branch runs instead.
    registry.execute_effect("add_treasury", world, ScopeRef::country(fixture.country), 5000.0);
    assert(vm.execute_if(*branch, world, ScopeRef::country(fixture.country), {}, 92u));
    assert(world.pops.wealth_milli(fixture.first_pop) == 90000);
    assert(world.pops.wealth_milli(fixture.second_pop) == 90000);
}

int main() {
    test_scope_resolver_and_builtin_primitives();
    test_script_conditional_effects_if_else();
    test_thunderscript_2_scope_traversal_saved_scope_and_script_calls();
    test_deterministic_random_iterator();
    test_content_defined_events_decisions_journals_and_ai();
    test_content_defined_ai_strategic_plans();
    std::cout << "Thunder 1.0 logic-foundation tests passed\n";
    return 0;
}
