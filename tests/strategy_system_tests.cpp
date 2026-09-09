#include "thunder/simulation/grand_strategy/GrandStrategyStore.hpp"
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace thunder;

namespace {

void test_treaties_relations_and_backdown() {
    GrandStrategyStore strategy;
    const CountryId a{0u};
    const CountryId b{1u};

    const auto treaty = strategy.create_treaty(a, b, TreatyKind::Alliance, 0xA11u);
    assert(treaty.valid());
    assert(strategy.has_active_treaty(a, b, TreatyKind::Alliance));
    assert(strategy.relation_milli(a, b) == 5'000);
    assert(strategy.break_treaty(treaty));
    assert(!strategy.has_active_treaty(a, b, TreatyKind::Alliance));
    assert(!strategy.has_active_treaty(CountryId{}, b, TreatyKind::Alliance));
    assert(!strategy.has_active_treaty(a, a, TreatyKind::Alliance));
    assert(strategy.relation_milli(a, b) == 0);

    const auto play = strategy.start_diplomatic_play(a, b, 0xC0DEu);
    assert(play.valid());
    assert(strategy.relation_milli(a, b) == -20'000);
    assert(strategy.back_down(play, b));
    assert(strategy.diplomatic_plays()[play.value()].phase == DiplomaticPlayPhase::Resolved);
    assert(strategy.relation_milli(a, b) == -22'500);
}

void test_politics_and_law_enactment() {
    GrandStrategyStore strategy;
    const CountryId a{0u};
    strategy.add_interest_group({a, 0x101u, 600'000u, 20'000});
    strategy.add_interest_group({a, 0x102u, 400'000u, -10'000});
    strategy.add_political_party({a, 0x201u, 700'000u});
    strategy.add_political_party({a, 0x202u, 300'000u});

    const auto enactment = strategy.start_law_enactment(a, 0xBEEFu);
    assert(enactment.valid());
    strategy.run_weekly_reference_tick();

    const auto* government = strategy.government_for(a);
    assert(government != nullptr);
    assert(government->legitimacy_ppm == 700'000u);
    assert(government->stability_milli == 8'000);
    assert(strategy.law_enactments()[enactment.value()].support_ppm == 600'000u);

    for (int week = 0; week < 260 && strategy.law_enactments()[enactment.value()].active; ++week)
        strategy.run_weekly_reference_tick();
    const auto& result = strategy.law_enactments()[enactment.value()];
    assert(result.passed);
    assert(!result.active);
    assert(result.progress_ppm == 1'000'000u);
    bool enacted = false;
    for (const auto& law : strategy.laws()) {
        if (law.country == a && law.key_hash == 0xBEEFu && law.enacted) enacted = true;
    }
    assert(enacted);
}

void test_diplomatic_play_to_war_and_front_combat() {
    GrandStrategyStore strategy;
    const CountryId a{0u};
    const CountryId b{1u};
    const StateId border{0u};

    strategy.add_army({a, border, 20'000u, 1'000'000u});
    strategy.add_army({b, border, 10'000u, 1'000'000u});
    const auto play = strategy.start_diplomatic_play(a, b, 0x5151u);
    const auto front = strategy.create_front_for_play(play, border);
    assert(front.valid());

    for (int week = 0; week < 8; ++week) strategy.run_weekly_reference_tick();
    assert(strategy.diplomatic_plays()[play.value()].phase == DiplomaticPlayPhase::War);
    assert(strategy.wars().size() == 1u);
    assert(strategy.wars()[0].active);
    assert(strategy.wars()[0].weeks == 1u);
    assert(strategy.fronts()[front.value()].progress_milli > 0);
    assert(!strategy.battles().empty());
    assert(strategy.armys()[0].manpower < 20'000u);
    assert(strategy.armys()[1].manpower < 10'000u);
    assert(strategy.relation_milli(a, b) == -100'000);

    const auto checksum_before = strategy.checksum();
    strategy.run_weekly_reference_tick();
    assert(strategy.checksum() != checksum_before);
    assert(strategy.validate(2u, 0u, 0u, 1u, 0u, 0u));
}

void test_script_primitives_bridge_to_strategy_systems() {
    World world;
    const auto a = world.countries.create({"AAA", 1'000.0, 100.0, 1'000.0, 0.2});
    const auto b = world.countries.create({"BBB", 1'000.0, 100.0, 1'000.0, 0.2});
    auto registry = ScriptRegistry::make_builtin();
    const auto a_scope = ScopeRef::country(a);

    registry.execute_effect("form_alliance_with", world, a_scope, static_cast<double>(b.value()));
    assert(registry.evaluate_trigger("has_alliance_with", world, a_scope, static_cast<double>(b.value())));
    registry.execute_effect("damage_relations_with", world, a_scope, static_cast<double>(b.value()));
    assert(world.grand_strategy.relation_milli(a, b) == 0);

    registry.execute_effect("start_law_enactment", world, a_scope, 12345.0);
    assert(world.grand_strategy.law_enactments().size() == 1u);
    for (int week = 0; week < 210; ++week) world.grand_strategy.run_weekly_reference_tick();
    assert(registry.evaluate_trigger("has_enacted_law", world, a_scope, 12345.0));
    assert(registry.evaluate_trigger("government_legitimacy_above", world, a_scope, 0.49));

    registry.execute_effect("start_diplomatic_play_with", world, a_scope, static_cast<double>(b.value()));
    assert(registry.evaluate_trigger("has_diplomatic_play_with", world, a_scope, static_cast<double>(b.value())));
    registry.execute_effect("back_down_from_play_with", world, a_scope, static_cast<double>(b.value()));
    assert(!registry.evaluate_trigger("has_diplomatic_play_with", world, a_scope, static_cast<double>(b.value())));
}


void test_thunderscript_symbolic_entity_and_key_arguments() {
    World world;
    const auto a = world.countries.create({"AAA", 1'000.0, 100.0, 1'000.0, 0.2});
    const auto b = world.countries.create({"BBB", 1'000.0, 100.0, 1'000.0, 0.2});
    auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        script symbolic_setup {
            scope = country
            effect = {
                form_alliance_with = BBB
                start_law_enactment = universal_suffrage
            }
        }
        script symbolic_check {
            scope = country
            trigger = {
                has_alliance_with = BBB
            }
        }
    )THUNDER", "symbolic_strategy.thunder");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(compiler.compile(parsed, programs, diagnostics));
    assert(diagnostics.empty());

    ScriptVm vm{registry, &programs};
    const auto* setup = programs.find_script(symbols.find("symbolic_setup"));
    const auto* check = programs.find_script(symbols.find("symbolic_check"));
    assert(setup != nullptr && check != nullptr);
    assert(!setup->scoped_effects.empty());
    assert(!check->scoped_conditions.empty());
    assert(vm.execute_if(*setup, world, ScopeRef::country(a)));
    assert(world.grand_strategy.has_active_treaty(a, b, TreatyKind::Alliance));
    assert(world.grand_strategy.law_enactments().size() == 1u);
    assert(world.grand_strategy.law_enactments()[0].law_hash == script_symbol_hash("universal_suffrage"));
    assert(vm.evaluate(*check, world, ScopeRef::country(a)));

    const auto invalid = parser.parse(R"THUNDER(
        script invalid_symbol_for_numeric_primitive {
            scope = country
            trigger = { treasury_above = BBB }
        }
    )THUNDER", "invalid_symbol.thunder");
    assert(invalid.ok());
    ScriptProgramDatabase invalid_programs;
    std::vector<ScriptCompileDiagnostic> invalid_diagnostics;
    assert(!compiler.compile(invalid, invalid_programs, invalid_diagnostics));
    assert(!invalid_diagnostics.empty());
}

void test_deterministic_strategy_state() {
    GrandStrategyStore first;
    GrandStrategyStore second;
    const CountryId a{0u};
    const CountryId b{1u};
    const StateId border{0u};

    auto seed = [&](GrandStrategyStore& strategy) {
        strategy.add_interest_group({a, 1u, 1'000'000u, 5'000});
        strategy.add_political_party({a, 2u, 1'000'000u});
        strategy.add_army({a, border, 12'000u, 900'000u});
        strategy.add_army({b, border, 11'000u, 850'000u});
        const auto play = strategy.start_diplomatic_play(a, b, 3u);
        strategy.create_front_for_play(play, border);
        strategy.start_law_enactment(a, 4u);
    };
    seed(first);
    seed(second);
    for (int week = 0; week < 32; ++week) {
        first.run_weekly_reference_tick();
        second.run_weekly_reference_tick();
        assert(first.checksum() == second.checksum());
    }
}

void test_province_migration_attraction_and_flow() {
    World world;
    const auto countryA = world.countries.create({"CTRA", 1'000.0, 100.0, 1'000.0, 0.2});
    const auto countryB = world.countries.create({"CTRB", 1'000.0, 100.0, 1'000.0, 0.2});
    const MarketId market{0u};
    const auto stateA = world.geography.create_state({"StateA", countryA, market, ProvinceId{0u}});
    const auto stateB = world.geography.create_state({"StateB", countryB, market, ProvinceId{1u}});
    const auto provA1 = world.geography.create_province({"ProvA1", stateA, countryA, market, 0.0, 0.0, 100u});
    const auto provA2 = world.geography.create_province({"ProvA2", stateA, countryA, market, 10.0, 10.0, 100u});
    const auto provB1 = world.geography.create_province({"ProvB1", stateB, countryB, market, 20.0, 20.0, 100u});

    const CultureId primary_cult{1u};
    const CultureId foreign_cult{2u};

    // Country A config: MigrationControls. Accepts primary_cult, discriminates against foreign_cult
    world.grand_strategy.set_migration_policy(countryA, MigrationPolicy::MigrationControls);
    world.grand_strategy.set_cultural_acceptance(countryA, primary_cult, CulturalAcceptanceLevel::Primary);
    world.grand_strategy.set_cultural_acceptance(countryA, foreign_cult, CulturalAcceptanceLevel::Discriminated);

    // In provA1: factory with 5 levels (5,000 capacity) and high wage offer (250)
    const auto bA1 = world.buildings.create({market, BuildingTypeId{1u}, 5u, 250, 0, provA1, ProductionMethodId{}});
    world.buildings.set_employees(bA1, 1'000u); // 4,000 vacancies

    // In provA1: small employed pop
    const auto popA1 = world.pops.create({market, 1'000u, bA1, NeedProfileId{1u}, provA1, primary_cult, ReligionId{1u}, ProfessionId{1u}, InterestGroupId{1u}});

    // In provA2: domestic subsistence pop (10,000)
    const auto popA2 = world.pops.create({market, 10'000u, BuildingId{}, NeedProfileId{1u}, provA2, primary_cult, ReligionId{1u}, ProfessionId{1u}, InterestGroupId{1u}});

    // In provB1: foreign pops (one accepted culture, one discriminated culture)
    const auto popB_acc = world.pops.create({market, 5'000u, BuildingId{}, NeedProfileId{1u}, provB1, primary_cult, ReligionId{1u}, ProfessionId{1u}, InterestGroupId{1u}});
    const auto popB_disc = world.pops.create({market, 5'000u, BuildingId{}, NeedProfileId{1u}, provB1, foreign_cult, ReligionId{1u}, ProfessionId{1u}, InterestGroupId{1u}});
    (void)popB_disc;

    // 1. Province attraction checks
    const auto attrA1_primary = world.grand_strategy.calculate_province_attraction(world, provA1, primary_cult);
    const auto attrA1_discrim = world.grand_strategy.calculate_province_attraction(world, provA1, foreign_cult);
    assert(attrA1_primary.available_jobs == 4'000u);
    assert(attrA1_primary.avg_wage == 250);
    assert(attrA1_discrim.discrimination_penalty == 50);
    assert(attrA1_primary.score > attrA1_discrim.score); // Discrimination lowers attraction

    // 2. Domestic migration (provA2 -> provA1): unrestricted by international policy
    const auto total_domestic_before = world.pops.population(popA1) + world.pops.population(popA2);
    const auto dom_flow = world.grand_strategy.start_migration_flow(provA2, provA1, 2'000u, 1u);
    assert(dom_flow.valid());
    world.grand_strategy.update_migration_flows(world);
    assert(world.pops.population(popA2) == 8'000u);
    assert(world.pops.population(popA1) == 3'000u);
    assert(world.pops.population(popA1) + world.pops.population(popA2) == total_domestic_before);

    // 3. International migration: Discriminated culture is blocked under MigrationControls
    const auto disc_flow = world.grand_strategy.start_migration_flow(provB1, provA1, 1'000u, 1u);
    assert(disc_flow.valid());
    world.pops.set_culture(popB_acc, foreign_cult); // Set source pop to discriminated culture
    world.grand_strategy.update_migration_flows(world);
    assert(world.pops.population(popA1) == 3'000u); // Blocked, no pop moved

    // 4. International migration: Accepted culture is allowed under MigrationControls
    world.pops.set_culture(popB_acc, primary_cult); // Set source pop to accepted culture
    const auto acc_flow = world.grand_strategy.start_migration_flow(provB1, provA1, 1'000u, 1u);
    assert(acc_flow.valid());
    world.grand_strategy.update_migration_flows(world);
    assert(world.pops.population(popA1) == 4'000u); // Allowed and transferred

    // The normal world-aware weekly tick must resolve due flows as well; a
    // timer decrement without the transfer would silently strand population.
    (void)world.grand_strategy.start_migration_flow(provA2, provA1, 500u, 1u);
    world.grand_strategy.run_weekly_reference_tick(world);
    assert(world.pops.population(popA2) == 7'500u);
    assert(world.pops.population(popA1) == 4'500u);
}



void test_migration_preserves_identity_and_auto_flows() {
    // R7a: migrants keep their identity. Moving into an EMPTY province founds
    // a new POP with the source cohort's culture/religion/profession, and an
    // overlarge flow leaves a one-person remnant instead of erasing the cohort.
    {
        World world;
        const auto country = world.countries.create({"MIG", 1'000.0, 100.0, 1'000.0, 0.2});
        const MarketId market{0u};
        const auto state = world.geography.create_state({"MigState", country, market, ProvinceId{0u}});
        const auto src = world.geography.create_province({"MigSrc", state, country, market, 0.0, 0.0, 100u});
        const auto dst = world.geography.create_province({"MigDst", state, country, market, 10.0, 10.0, 100u});
        const CultureId settler_cult{7u};
        const auto src_pop = world.pops.create({market, 500u, BuildingId{}, NeedProfileId{1u}, src,
            settler_cult, ReligionId{3u}, ProfessionId{4u}, InterestGroupId{1u}});
        world.pops.set_cash(src_pop, 100'000); // savings travel per capita
        const auto pops_before = world.pops.size();
        (void)world.grand_strategy.start_migration_flow(src, dst, 200u, 1u);
        world.grand_strategy.update_migration_flows(world);
        // Found a new POP: same identity, 200 people, 40% of the savings.
        assert(world.pops.size() == pops_before + 1u);
        bool found = false;
        for (std::size_t i = 0; i < world.pops.size(); ++i) {
            const PopId id{static_cast<PopId::rep_type>(i)};
            if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
            if (world.pops.province(id) == dst) {
                assert(world.pops.population(id) == 200u);
                assert(world.pops.culture(id) == settler_cult);
                assert(world.pops.religion(id) == ReligionId{3u});
                assert(world.pops.profession(id) == ProfessionId{4u});
                assert(world.pops.cash(id) == 40'000); // 200 x (100'000/500)
                found = true;
            }
        }
        assert(found);
        assert(world.pops.population(src_pop) == 300u);
        assert(world.pops.cash(src_pop) == 60'000);

        // Overlarge flow: the cohort survives as a remnant of one.
        (void)world.grand_strategy.start_migration_flow(src, dst, 10'000u, 1u);
        world.grand_strategy.update_migration_flows(world);
        assert(world.pops.population(src_pop) == 1u);
    }

    // R7b: the weekly generator pushes small domestic flows from
    // unemployment-heavy provinces toward same-country vacancy provinces,
    // deterministically and bounded.
    {
        EconomyDefinitions definitions;
        World world;
        const auto country = world.countries.create({"MIG", 1'000.0, 100.0, 1'000.0, 0.2});
        const MarketId market{0u};
        const auto state = world.geography.create_state({"MigState", country, market, ProvinceId{0u}});
        const auto slum = world.geography.create_province({"Slum", state, country, market, 0.0, 0.0, 100u});
        const auto boom = world.geography.create_province({"Boom", state, country, market, 10.0, 10.0, 100u});
        // 5000 idle hands in Slum...
        world.pops.create({market, 5000u, BuildingId{}, NeedProfileId{0u}, slum});
        // ...and a level-5 mill (5000 jobs) standing empty in Boom.
        const auto mill_type = definitions.add_building_type("mill", 1000, {}, {});
        const auto mill = world.buildings.create({market, mill_type, 5u, 1000, 0, boom});
        (void)mill;
        world.grand_strategy.generate_autonomous_migration_flows(world, definitions);
        bool generated = false;
        for (const auto& flow : world.grand_strategy.migration_flows()) {
            if (flow.source == slum && flow.destination == boom && flow.population > 0u) {
                assert(flow.population == 250u); // 5% of 5000 idle, capped
                assert(flow.weeks_remaining == 4u);
                generated = true;
            }
        }
        assert(generated);
        // Second call in the same state creates no duplicate (src,dst) flow.
        const auto flows_before = world.grand_strategy.migration_flows().size();
        world.grand_strategy.generate_autonomous_migration_flows(world, definitions);
        assert(world.grand_strategy.migration_flows().size() == flows_before);
    }
}

void test_historic_elections_and_parliamentary_franchise() {
    World world;
    const auto country = world.countries.create({"VOT", 1'000.0, 100.0, 1'000.0, 0.2});

    // Parties: Conservative (Party A), Liberal (Party B), Labor (Party C)
    const std::uint64_t conservative_hash = 0x1111u;
    const std::uint64_t liberal_hash = 0x2222u;
    const std::uint64_t labor_hash = 0x3333u;

    world.grand_strategy.add_political_party({country, conservative_hash, 200'000u});
    world.grand_strategy.add_political_party({country, liberal_hash, 300'000u});
    world.grand_strategy.add_political_party({country, labor_hash, 500'000u});

    // Interest Group supporting Conservative party with high clout
    world.grand_strategy.add_interest_group({country, conservative_hash, 800'000u, 100});

    // 1. Autocracy Law: Elections disabled (elections_enabled = false) -> 100% seats to ruling establishment
    const std::uint64_t autocracy_law = 0xA111u;
    world.grand_strategy.configure_parliament_rules(country, autocracy_law, false);
    assert(world.grand_strategy.elections_enabled(country) == false);
    world.grand_strategy.run_parliamentary_election(country);
    const auto* parl_auto = world.grand_strategy.parliament_for(country);
    assert(parl_auto != nullptr);
    assert(parl_auto->ruling_party_seats == 100u);
    assert(parl_auto->opposition_seats == 0u);

    // 2. Aristocratic/Landed Law: High IG Clout weight dominates election outcome
    const std::uint64_t landed_voting_law = 0xB222u;
    world.grand_strategy.configure_parliament_rules(country, landed_voting_law, true, 100'000u, 1'000'000u);
    assert(world.grand_strategy.elections_enabled(country) == true);
    world.grand_strategy.run_parliamentary_election(country);
    const auto* parl_landed = world.grand_strategy.parliament_for(country);
    assert(parl_landed->ruling_party_hash == conservative_hash); // Conservative wins via high IG clout

    // 3. Universal Suffrage Law: Pure POP voting power (pop_weight = 100%, ig_clout = 0%)
    const std::uint64_t universal_suffrage_law = 0xC333u;
    world.grand_strategy.configure_parliament_rules(country, universal_suffrage_law, true, 1'000'000u, 0u);
    world.grand_strategy.run_parliamentary_election(country);
    const auto* parl_univ = world.grand_strategy.parliament_for(country);
    assert(parl_univ->ruling_party_hash == labor_hash);
    assert(parl_univ->ruling_party_seats == 50u);
    assert(parl_univ->opposition_seats == 50u);

    // 4. Migration Policy checks
    world.grand_strategy.set_migration_policy(country, MigrationPolicy::ClosedBorders);
    assert(world.grand_strategy.migration_policy(country) == MigrationPolicy::ClosedBorders);
    world.grand_strategy.set_migration_policy(country, MigrationPolicy::OpenBorders);
    assert(world.grand_strategy.migration_policy(country) == MigrationPolicy::OpenBorders);

    // 5. Automatic election cycle weekly progression
    auto* parl_mut = world.grand_strategy.parliament_for_mut(country);
    parl_mut->weeks_to_next_election = 1u;
    world.grand_strategy.run_weekly_reference_tick();
    assert(world.grand_strategy.parliament_for(country)->weeks_to_next_election == 208u); // Reset for 4-year term
}



void test_multilateral_diplomatic_plays_and_sway() {
    World world;
    const auto countryA = world.countries.create({"CTRA", 1'000.0, 100.0, 1'000.0, 0.2});
    const auto countryB = world.countries.create({"CTRB", 1'000.0, 100.0, 1'000.0, 0.2});
    const auto countryC = world.countries.create({"CTRC", 1'000.0, 100.0, 1'000.0, 0.2});

    const auto play = world.grand_strategy.start_diplomatic_play(countryA, countryB, 0x1111u);
    assert(play.valid());

    // 1. Add primary and secondary war goals
    const auto wg1 = world.grand_strategy.add_war_goal({play, countryA, countryB, WarGoalType::ConquerState, StateId{0u}, true, false});
    const auto wg2 = world.grand_strategy.add_war_goal({play, countryA, countryB, WarGoalType::WarReparations, StateId{}, false, false});
    assert(wg1.valid() && wg2.valid());

    // 2. Sway third party nation (Country C) to join country A
    const auto sway = world.grand_strategy.sway_nation(play, countryA, countryC, SwayOfferType::DiplomaticObligation, 0x9999u);
    assert(sway.valid());
    assert(world.grand_strategy.diplomatic_sways()[sway.value()].accepted == false);

    const bool accepted = world.grand_strategy.accept_sway(sway);
    assert(accepted);
    assert(world.grand_strategy.diplomatic_sways()[sway.value()].accepted == true);
    assert(world.grand_strategy.relation_milli(countryA, countryC) > 0);

    // 3. Country B backs down under diplomatic pressure, resulting in peaceful resolution and war goal enforcement
    const bool backed_down = world.grand_strategy.back_down_diplomatic_play(play, countryB);
    assert(backed_down);
    assert(world.grand_strategy.diplomatic_plays()[play.value()].phase == DiplomaticPlayPhase::Resolved);
    assert(world.grand_strategy.war_goals()[wg1.value()].enforced == true);
    assert(world.grand_strategy.war_goals()[wg2.value()].enforced == true);
    assert(world.grand_strategy.has_active_treaty(countryA, countryB, TreatyKind::PeaceTreaty));
}

void test_frontline_tactics_and_commanders() {
    World world;
    const auto countryA = world.countries.create({"ATK", 1'000.0, 100.0, 1'000.0, 0.2});
    const auto countryB = world.countries.create({"DEF", 1'000.0, 100.0, 1'000.0, 0.2});
    const MarketId market{0u};
    const auto state = world.geography.create_state({"FrontState", countryB, market, ProvinceId{0u}});

    // Armies: Attacker with 20,000 manpower, Defender with 10,000 manpower
    world.grand_strategy.add_army({countryA, state, 20'000u, 1'000'000u});
    world.grand_strategy.add_army({countryB, state, 10'000u, 1'000'000u});

    // Commanders: DefensiveMaster on defender side
    const auto cmd = world.grand_strategy.add_commander({countryB, state, CommanderTrait::DefensiveMaster, 3u});
    assert(cmd.valid());

    const auto play = world.grand_strategy.start_diplomatic_play(countryA, countryB, 0x1111u);
    const auto front = world.grand_strategy.create_front_for_play(play, state);
    assert(front.valid());

    const auto war = world.grand_strategy.add_war({play, countryA, countryB, 0, 0u, true});
    assert(war.valid());

    // Advance 4 weeks of tactical front combat
    for (int w = 0; w < 4; ++w) {
        world.grand_strategy.run_weekly_reference_tick();
    }

    const auto& b = world.grand_strategy.battles();
    assert(!b.empty());
    assert(world.grand_strategy.armys()[0].manpower < 20'000u); // Attacker took casualties
    assert(world.grand_strategy.armys()[1].manpower < 10'000u); // Defender took casualties
}

void test_naval_warfare_and_sea_zone_blockades() {

    World world;
    const auto countryA = world.countries.create({"BLKA", 1'000.0, 100.0, 1'000.0, 0.2});
    const auto countryB = world.countries.create({"BLKB", 1'000.0, 100.0, 1'000.0, 0.2});
    const MarketId market{0u};
    const auto state = world.geography.create_state({"CoastState", countryB, market, ProvinceId{0u}});

    // 1. Create Sea Zone
    const auto sea_zone = world.grand_strategy.add_sea_zone({0x5345415A55u, countryA, 0u});
    assert(sea_zone.valid());

    // 2. Create Navy for Country A with 5,000 sailors and assign BlockadePort mission
    const auto navy = world.grand_strategy.add_navy({countryA, state, 5'000u, 1'000'000u, ShipDesignId{}, NavalMission::None, SeaZoneId{}});
    assert(navy.valid());
    world.grand_strategy.assign_navy_mission(navy, NavalMission::BlockadePort, sea_zone);

    // 3. Advance weekly tick
    world.grand_strategy.run_weekly_reference_tick();

    // 4. Verify blockade efficiency
    const auto blockade_eff = world.grand_strategy.sea_zone_blockade_level(sea_zone);
    assert(blockade_eff > 0u);
    assert(blockade_eff == 500'000u); // 5,000 * 100 = 500,000 PPM (50%)
}

void test_civilian_war_casualties_and_continuous_reinforcements() {
    World world;
    const auto countryA = world.countries.create({"QNG", 100'000.0, 100.0, 100'000.0, 0.2});
    const auto countryB = world.countries.create({"TPT", 50'000.0, 100.0, 50'000.0, 0.2});
    const MarketId market{0u};
    const auto state = world.geography.create_state({"Jiangnan", countryA, market, ProvinceId{0u}});
    const auto prov = world.geography.create_province({"Nanking", state, countryA, market, 0.0, 0.0, 1000});
    world.geography.set_state_capital(state, prov);

    // Create civilian POP in Jiangnan
    PopInit pi{};
    pi.market = market;
    pi.province = prov;
    pi.size = 100'000u;
    const auto pop_id = world.pops.create(pi);
    world.pops.set_standard_of_living_milli(pop_id, 12'000);
    const auto initial_pop = world.pops.population(pop_id);
    assert(initial_pop == 100'000u);

    // Create Army for Country A with deficit (5,000 manpower out of 25,000 target)
    const auto army = world.grand_strategy.add_army({countryA, state, 5'000u, 800'000u});
    const auto initial_manpower = world.grand_strategy.armys()[army.value()].manpower;

    // Start war and create front in Jiangnan
    const auto play = world.grand_strategy.start_diplomatic_play(countryA, countryB, 0x1851u);
    const auto front = world.grand_strategy.create_front_for_play(play, state);
    assert(front.valid());

    // Advance to war
    for (int week = 0; week < 8; ++week) {
        world.grand_strategy.run_weekly_reference_tick(world);
    }
    assert(world.grand_strategy.wars()[0].active);

    // 1. Verify civilian population casualties in warzone (Taiping Rebellion depopulation realism)
    const auto pop_during_war = world.pops.population(pop_id);
    assert(pop_during_war < initial_pop); // Civilians perished / fled warzone
    const auto sol_during_war = world.pops.standard_of_living_milli(pop_id);
    assert(sol_during_war < 12'000); // Standard of living dropped due to devastation

    // 2. Verify army continuous replenishment / recruitment from domestic POPs
    const auto army_manpower_after = world.grand_strategy.armys()[army.value()].manpower;
    assert(army_manpower_after > initial_manpower); // Army reinforced continuously from domestic recruits!

    // 3. Verify military wages were paid from treasury
    const auto treasury_after = world.countries.treasury_milli(countryA);
    assert(treasury_after < 100'000LL * 1000LL);
}

} // namespace

int main() {
    test_treaties_relations_and_backdown();
    test_politics_and_law_enactment();
    test_diplomatic_play_to_war_and_front_combat();
    test_script_primitives_bridge_to_strategy_systems();
    test_thunderscript_symbolic_entity_and_key_arguments();
    test_deterministic_strategy_state();
    test_province_migration_attraction_and_flow();
    test_migration_preserves_identity_and_auto_flows();
    test_historic_elections_and_parliamentary_franchise();
    test_multilateral_diplomatic_plays_and_sway();
    test_frontline_tactics_and_commanders();
    test_naval_warfare_and_sea_zone_blockades();
    test_civilian_war_casualties_and_continuous_reinforcements();
    std::cout << "Thunder 1.0 strategy system tests: PASS\n";
    return 0;
}

