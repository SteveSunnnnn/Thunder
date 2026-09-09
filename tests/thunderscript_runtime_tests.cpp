#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/DynamicVariables.hpp"
#include "thunder/scripting/ScriptContext.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

using namespace thunder;

namespace {

ScriptProgramDatabase compile(SymbolTable& symbols, const ScriptRegistry& registry,
                              std::string_view source) {
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(source, "thunderscript_runtime.thunder");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    const bool ok = compiler.compile(parsed, programs, diagnostics);
    const bool linked = ok && programs.validate_links(symbols, diagnostics);
    if (!linked) {
        for (const auto& diagnostic : diagnostics)
            std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
    }
    assert(linked);
    assert(diagnostics.empty());
    return programs;
}

void test_context_frames_stable_bindings_collections_and_checksum() {
    const auto alpha = script_stable_key("alpha");
    const auto beta = script_stable_key("beta");
    const auto target_a = script_stable_key("target_a");
    const auto target_b = script_stable_key("target_b");
    const auto scopes = script_stable_key("scopes");
    const auto duplicates = script_stable_key("duplicates");

    auto first = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}), {}, 77u);
    first.set_parameter(beta, ScriptArgument::numeric(2.0));
    first.set_parameter(alpha, ScriptArgument::numeric(1.0));
    first.set_variable(beta, ScriptArgument::boolean(true));
    first.set_variable(alpha, ScriptArgument::symbol(script_stable_key("value")));
    first.save_event_target(target_b, ScopeRef::country(CountryId{1u}));
    first.save_event_target(target_a, ScopeRef::country(CountryId{0u}));
    assert(first.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{0u}))));
    assert(first.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{1u}))));
    assert(first.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{1u}))));
    assert(first.collection(scopes).size() == 2u);
    assert(!first.add_to_collection(scopes, ScriptArgument::numeric(1.0)));
    assert(!first.add_to_collection(scopes,
        ScriptArgument::scope(ScopeRef::pop(PopId{0u}))));
    assert(first.collection_scope(scopes) == ScopeType::Country);
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), false));
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), false));
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), true));
    assert(first.collection(duplicates).size() == 2u);
    assert(first.remove_from_collection(duplicates, ScriptArgument::numeric(4.0)));
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), true));
    assert(first.collection(duplicates).size() == 1u);

    auto second = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}), {}, 77u);
    second.set_parameter(alpha, ScriptArgument::numeric(1.0));
    second.set_parameter(beta, ScriptArgument::numeric(2.0));
    second.set_variable(alpha, ScriptArgument::symbol(script_stable_key("value")));
    second.set_variable(beta, ScriptArgument::boolean(true));
    second.save_event_target(target_a, ScopeRef::country(CountryId{0u}));
    second.save_event_target(target_b, ScopeRef::country(CountryId{1u}));
    assert(second.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{0u}))));
    assert(second.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{1u}))));
    assert(second.add_to_collection(duplicates, ScriptArgument::numeric(4.0)));
    assert(first.checksum() == second.checksum());

    const std::array nested_parameters{ScriptNamedValue{alpha, ScriptArgument::numeric(9.0)}};
    first.push_call_frame(nested_parameters);
    assert(first.call_depth() == 2u);
    assert(first.parameter(alpha) == ScriptArgument::numeric(9.0));
    assert(first.variable(beta) == ScriptArgument::boolean(true));
    first.set_variable(beta, ScriptArgument::boolean(false));
    assert(first.variable(beta) == ScriptArgument::boolean(false));
    first.pop_call_frame();
    assert(first.call_depth() == 1u);
    assert(first.parameter(alpha) == ScriptArgument::numeric(1.0));
    assert(first.variable(beta) == ScriptArgument::boolean(true));

    first.enter(ScopeRef::state(StateId{3u}));
    assert(first.prev() == ScopeRef::country(CountryId{0u}));
    first.leave();
    assert(first.current == ScopeRef::country(CountryId{0u}));

    auto positive_zero = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}));
    auto negative_zero = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}));
    positive_zero.set_variable(alpha, ScriptArgument::numeric(0.0));
    negative_zero.set_variable(alpha, ScriptArgument::numeric(-0.0));
    assert(positive_zero.checksum() == negative_zero.checksum());
    assert(!ScriptArgument::numeric(std::numeric_limits<double>::infinity()).valid());
}

struct RuntimeFixture {
    World world;
    CountryId first{};
    CountryId second{};
    StateId state{};
    ProvinceId province{};
    PopId first_pop{};
    PopId second_pop{};

    RuntimeFixture() {
        first = world.countries.create({"AAA", 1'000.0, 100.0, 100.0, 0.2});
        second = world.countries.create({"BBB", 1'000.0, 100.0, 100.0, 0.2});
        state = world.geography.create_state({"state", first, {}, {}});
        province = world.geography.create_province({"province", state, first, {}, 0.0, 0.0, 1u});
        world.geography.set_state_capital(state, province);
        PopInit init{};
        init.province = province;
        init.size = 100u;
        first_pop = world.pops.create(init);
        init.size = 200u;
        second_pop = world.pops.create(init);
    }
};

void test_parameterized_calls_event_targets_variables_and_values() {
    RuntimeFixture fixture;
    auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script parameter_gate {
            scope = country
            trigger = {
                treasury_above = arg:min_cash
                has_alliance_with = arg:ally
                has_variable = external_flag
                variable_equals = { name = external_flag value = yes }
            }
        }
        script parameter_grant {
            scope = country
            effect = {
                set_variable = { name = local_grant value = arg:amount }
                add_treasury = var:local_grant
                save_event_target_as = recipient
                add_to_collection = { name = visited value = THIS }
            }
        }
        script orchestrator {
            scope = country
            trigger = {
                scripted_trigger = {
                    name = parameter_gate
                    parameters = {
                        min_cash = arg:threshold
                        ally = event_target:ally
                    }
                }
            }
            effect = {
                scripted_effect = {
                    name = parameter_grant
                    amount = arg:grant
                }
            }
        }
        scripted_value parameter_value {
            scope = country
            source = arg:base
            multiply = 2
            add = 1
        }
        scripted_value nested_value {
            scope = country
            source = value:parameter_value
            multiply = 3
        }
        script value_gate {
            scope = country
            trigger = { treasury_above = value:nested_value }
        }
    )THUNDER");

    const auto alliance = registry.find_effect("form_alliance_with");
    registry.execute_effect(alliance, fixture.world, ScopeRef::country(fixture.first),
                            ScriptArgument::scope(ScopeRef::country(fixture.second)));

    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    context.set_parameter(script_stable_key("threshold"), ScriptArgument::numeric(50.0));
    context.set_parameter(script_stable_key("grant"), ScriptArgument::numeric(7.0));
    context.set_parameter(script_stable_key("base"), ScriptArgument::numeric(10.0));
    context.set_variable(script_stable_key("external_flag"), ScriptArgument::boolean(true));
    context.save_event_target(script_stable_key("ally"), ScopeRef::country(fixture.second));

    ScriptVm vm{registry, &programs};
    const auto* orchestrator = programs.find_script(symbols.find("orchestrator"));
    assert(orchestrator != nullptr);
    assert(vm.execute_if(*orchestrator, fixture.world, context));
    assert(std::abs(fixture.world.countries.treasury(fixture.first) - 107.0) < 1e-9);
    assert(!context.has_variable(script_stable_key("local_grant")));
    assert(context.event_target(script_stable_key("recipient")) == ScopeRef::country(fixture.first));
    const auto visited = context.collection(script_stable_key("visited"));
    assert(visited.size() == 1u);
    assert(visited[0] == ScriptArgument::scope(ScopeRef::country(fixture.first)));
    assert(context.call_depth() == 1u);

    const auto* nested = programs.find_value(symbols.find("nested_value"));
    assert(nested != nullptr);
    assert(std::abs(vm.evaluate(*nested, fixture.world, context) - 63.0) < 1e-9);
    const std::array value_arguments{
        ScriptNamedValue{script_stable_key("base"), ScriptArgument::numeric(4.0)}};
    assert(std::abs(vm.evaluate_value(symbols.find("nested_value"), fixture.world, context,
                                      value_arguments) - 27.0) < 1e-9);

    const auto* value_gate = programs.find_script(symbols.find("value_gate"));
    assert(value_gate != nullptr);
    assert(vm.evaluate(*value_gate, fixture.world, context));

    auto missing = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    missing.set_variable(script_stable_key("external_flag"), ScriptArgument::boolean(true));
    missing.save_event_target(script_stable_key("ally"), ScopeRef::country(fixture.second));
    assert(!vm.evaluate(*orchestrator, fixture.world, missing));
}

void test_scope_collections_and_event_target_selectors() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script collect_and_apply {
            scope = country
            effect = {
                every_pop = {
                    add_to_collection = selected_pops
                    save_event_target_as = last_pop
                }
                every_in:selected_pops = { set_pop_wealth = 25 }
                event_target:last_pop = { set_pop_literacy = 0.8 }
            }
        }
        script all_collected_are_large {
            scope = country
            trigger = {
                every_in:selected_pops = { pop_size_above = 50 }
            }
        }
        script deterministic_collection_pick {
            scope = country
            effect = {
                random_in:selected_pops = { set_pop_literacy = 0.9 }
            }
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first), {}, 0x1234u);
    const auto* collect = programs.find_script(symbols.find("collect_and_apply"));
    assert(collect != nullptr && vm.execute_if(*collect, fixture.world, context));
    const auto selected = context.collection(script_stable_key("selected_pops"));
    assert(selected.size() == 2u);
    assert(selected[0] == ScriptArgument::scope(ScopeRef::pop(fixture.first_pop)));
    assert(selected[1] == ScriptArgument::scope(ScopeRef::pop(fixture.second_pop)));
    assert(fixture.world.pops.wealth_milli(fixture.first_pop) == 25'000);
    assert(fixture.world.pops.wealth_milli(fixture.second_pop) == 25'000);
    assert(fixture.world.pops.literacy_permyriad(fixture.first_pop) == 0u);
    assert(fixture.world.pops.literacy_permyriad(fixture.second_pop) == 8'000u);

    const auto* all_large = programs.find_script(symbols.find("all_collected_are_large"));
    assert(all_large != nullptr && vm.evaluate(*all_large, fixture.world, context));

    const auto* pick = programs.find_script(symbols.find("deterministic_collection_pick"));
    assert(pick != nullptr);
    fixture.world.pops.set_literacy_permyriad(fixture.first_pop, 0u);
    fixture.world.pops.set_literacy_permyriad(fixture.second_pop, 0u);
    assert(vm.execute_if(*pick, fixture.world, context));
    const bool first_picked = fixture.world.pops.literacy_permyriad(fixture.first_pop) == 9'000u;
    const bool second_picked = fixture.world.pops.literacy_permyriad(fixture.second_pop) == 9'000u;
    assert(first_picked != second_picked);
    assert(context.random_draws.size() == 1u && context.random_draws.front().count == 1u);

    fixture.world.pops.set_literacy_permyriad(fixture.first_pop, 0u);
    fixture.world.pops.set_literacy_permyriad(fixture.second_pop, 0u);
    assert(vm.execute_if(*pick, fixture.world, context));
    assert((fixture.world.pops.literacy_permyriad(fixture.first_pop) == 9'000u) !=
           (fixture.world.pops.literacy_permyriad(fixture.second_pop) == 9'000u));
    assert(context.random_draws.front().count == 2u);
}

void test_mixed_backends_empty_trigger_and_stable_callsites() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    constexpr std::string_view source = R"THUNDER(
        script mixed_backend {
            scope = country
            trigger = { treasury_above = 50 }
            trigger = { has_variable = enabled }
            effect = { add_treasury = 3 }
            effect = { set_variable = { name = ran value = yes } }
        }
        script empty_gate {
            scope = country
            trigger = { }
        }
        script random_one {
            scope = country
            effect = { random_pop = { set_pop_literacy = 0.4 } }
        }
        script random_two {
            scope = country
            effect = { random_pop = { set_pop_literacy = 0.5 } }
        }
    )THUNDER";
    const auto programs = compile(symbols, registry, source);
    ScriptVm vm{registry, &programs};
    const auto* mixed = programs.find_script(symbols.find("mixed_backend"));
    const auto* empty = programs.find_script(symbols.find("empty_gate"));
    assert(mixed != nullptr && empty != nullptr);
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    context.set_variable(script_stable_key("enabled"), ScriptArgument::boolean(true));
    fixture.world.countries.set_treasury(fixture.first, 40.0);
    assert(!vm.execute_if(*mixed, fixture.world, context));
    assert(!context.has_variable(script_stable_key("ran")));
    fixture.world.countries.set_treasury(fixture.first, 100.0);
    assert(vm.execute_if(*mixed, fixture.world, context));
    assert(fixture.world.countries.treasury(fixture.first) == 103.0);
    assert(context.variable(script_stable_key("ran")) == ScriptArgument::boolean(true));
    assert(vm.evaluate(*empty, fixture.world, ScopeRef::country(fixture.first)));

    const auto* random_one = programs.find_script(symbols.find("random_one"));
    const auto* random_two = programs.find_script(symbols.find("random_two"));
    assert(random_one != nullptr && random_two != nullptr);
    assert(random_one->scoped_effects.front().salt != random_two->scoped_effects.front().salt);

    SymbolTable reordered_symbols;
    (void)reordered_symbols.intern("unrelated_symbol_inserted_before_content");
    const auto reordered = compile(reordered_symbols, registry, source);
    const auto* reordered_one = reordered.find_script(reordered_symbols.find("random_one"));
    assert(reordered_one != nullptr);
    assert(random_one->scoped_effects.front().salt == reordered_one->scoped_effects.front().salt);
}

void test_parser_resource_and_non_finite_limits() {
    SymbolTable symbols;
    ThunderScriptParser parser{symbols};
    const auto non_finite = parser.parse("script invalid { scope = country trigger = { treasury_above = 1e999 } }");
    assert(!non_finite.ok());

    std::string deeply_nested = "script deep { scope = country trigger = { ";
    for (std::size_t i = 0; i < 140u; ++i) deeply_nested += "all = { ";
    deeply_nested += "treasury_above = 1 ";
    for (std::size_t i = 0; i < 140u; ++i) deeply_nested += "} ";
    deeply_nested += "} }";
    const auto depth_limited = parser.parse(deeply_nested);
    assert(!depth_limited.ok());
}

void test_static_argument_type_diagnostics() {
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        script invalid_boolean_for_number {
            scope = country
            trigger = { treasury_above = yes }
        }
    )THUNDER");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(!compiler.compile(parsed, programs, diagnostics));
    assert(!diagnostics.empty());
}

void test_typed_script_signatures_defaults_linking_and_runtime() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script typed_grant {
            scope = country
            parameters = {
                amount = number
                recipient = country
                bonus = { type = number required = no default = 2 }
            }
            effect = {
                add_treasury = arg:amount
                add_treasury = arg:bonus
            }
        }
        script typed_caller {
            scope = country
            parameters = { recipient = country }
            effect = {
                scripted_effect = {
                    name = typed_grant
                    amount = 5
                    recipient = arg:recipient
                }
            }
        }
        scripted_value typed_capacity {
            scope = country
            parameters = {
                base = number
                offset = { type = number required = no default = 1 }
            }
            source = arg:base
            multiply = 2
            add = 1
        }
    )THUNDER");
    const auto* grant = programs.find_script(symbols.find("typed_grant"));
    const auto* caller = programs.find_script(symbols.find("typed_caller"));
    assert(grant != nullptr && caller != nullptr);
    assert(grant->parameters.size() == 3u);
    ScriptVm vm{registry, &programs};

    auto missing = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    assert(!vm.execute_if(*grant, fixture.world, missing));

    auto wrong = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    wrong.set_parameter(script_stable_key("amount"), ScriptArgument::boolean(true));
    wrong.set_parameter(script_stable_key("recipient"),
                        ScriptArgument::scope(ScopeRef::country(fixture.second)));
    assert(!vm.execute_if(*grant, fixture.world, wrong));

    auto direct = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    direct.set_parameter(script_stable_key("amount"), ScriptArgument::numeric(5.0));
    direct.set_parameter(script_stable_key("recipient"),
                         ScriptArgument::scope(ScopeRef::country(fixture.second)));
    assert(vm.execute_if(*grant, fixture.world, direct));
    assert(fixture.world.countries.treasury(fixture.first) == 107.0);
    assert(direct.parameter(script_stable_key("bonus")) == ScriptArgument::numeric(2.0));

    auto caller_context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    caller_context.set_parameter(script_stable_key("recipient"),
                                 ScriptArgument::scope(ScopeRef::country(fixture.second)));
    assert(vm.execute_if(*caller, fixture.world, caller_context));
    assert(fixture.world.countries.treasury(fixture.first) == 114.0);

    const auto typed_capacity = symbols.find("typed_capacity");
    const std::array value_arguments{
        ScriptNamedValue{script_stable_key("base"), ScriptArgument::numeric(4.0)}};
    assert(vm.evaluate_value(typed_capacity, fixture.world,
                             ScriptExecutionContext::rooted(ScopeRef::country(fixture.first)),
                             value_arguments) == 9.0);
    const std::array wrong_value_arguments{
        ScriptNamedValue{script_stable_key("base"), ScriptArgument::boolean(true)}};
    bool value_rejected = false;
    try {
        (void)vm.evaluate_value(typed_capacity, fixture.world,
                                ScriptExecutionContext::rooted(ScopeRef::country(fixture.first)),
                                wrong_value_arguments);
    } catch (const std::runtime_error&) {
        value_rejected = true;
    }
    assert(value_rejected);

    SymbolTable invalid_symbols;
    ThunderScriptParser parser{invalid_symbols};
    const auto invalid = parser.parse(R"THUNDER(
        script target {
            scope = country
            parameters = { amount = number recipient = country }
        }
        script missing_argument {
            scope = country
            effect = { scripted_effect = { name = target amount = 1 } }
        }
        script wrong_argument {
            scope = country
            effect = {
                scripted_effect = { name = target amount = yes recipient = THIS }
            }
        }
        script cycle_a {
            scope = country
            effect = { scripted_effect = cycle_b }
        }
        script cycle_b {
            scope = country
            effect = { scripted_effect = cycle_a }
        }
    )THUNDER");
    assert(invalid.ok());
    ScriptProgramDatabase invalid_programs;
    ScriptCompiler compiler{invalid_symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(compiler.compile(invalid, invalid_programs, diagnostics));
    assert(!invalid_programs.validate_links(invalid_symbols, diagnostics));
    assert(diagnostics.size() >= 3u);
}

void test_deterministic_execution_budget() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    ScriptProgram oversized;
    oversized.scope = ScopeType::Country;
    oversized.condition.resize(
        static_cast<std::size_t>(ScriptExecutionContext::default_work_budget + 1u));

    ScriptVm vm{registry};
    bool rejected = false;
    try {
        (void)vm.evaluate(oversized, fixture.world, ScopeRef::country(fixture.first));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()} == "ThunderScript execution budget exceeded";
    }
    assert(rejected);

    auto counter = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    counter.begin_execution(2u);
    assert(counter.consume_work());
    assert(counter.consume_work());
    assert(!counter.consume_work());
}

void test_dynamic_variable_map() {

    DynamicVariableMap dvars;
    dvars.set_int("treasury", 42000);
    dvars.set_double("tax_rate", 0.15);
    dvars.set_bool("is_mobilized", true);
    dvars.set_id("capital_state", 101);

    assert(dvars.size() == 4);
    assert(dvars.has("treasury"));
    assert(dvars.get_int("treasury") == 42000);
    assert(dvars.get_double("tax_rate") == 0.15);
    assert(dvars.get_bool("is_mobilized") == true);
    assert(dvars.get_id(DynamicVariableMap::hash_key("capital_state")) == 101);

    assert(dvars.checksum() != 0);

    assert(dvars.remove("treasury") == true);
    assert(dvars.has("treasury") == false);
    assert(dvars.size() == 3);
}

void test_weighted_random_list() {
    WeightedRandomList list;
    list.entries.push_back({100u, 1u, 0});      // Weight 100
    list.entries.push_back({100u, 2u, 500000}); // Weight 100 * 1.5 = 150
    list.entries.push_back({50u, 3u, -500000}); // Weight 50 * 0.5 = 25
    // Total weight = 275

    assert(list.sample(0) == 0);   // in [0..99]
    assert(list.sample(50) == 0);
    assert(list.sample(100) == 1); // in [100..249]
    assert(list.sample(240) == 1);
    assert(list.sample(250) == 2); // in [250..274]
}

void test_script_profiler() {
    ScriptProfiler profiler;
    profiler.record(0xABCD1234ULL, 1500u);
    profiler.record(0xABCD1234ULL, 3500u);
    profiler.record(0xDEADBEEFULL, 2000u);

    assert(profiler.records().size() == 2);
    assert(profiler.records()[0].invocations == 2);
    assert(profiler.records()[0].total_nanoseconds == 5000u);
    assert(profiler.records()[0].max_nanoseconds == 3500u);

    const auto json = profiler.dump_flamegraph_json();
    assert(!json.empty());
    assert(json.find("0x") != std::string::npos);
}

void test_ordered_iterator_condition_every_semantics() {
    // `ordered_X` under a trigger must evaluate the block against *every*
    // element in the sorted/limited window (matching the effect side), not
    // short-circuit on the first match. Regression guard for the condition/effect
    // semantic inconsistency.
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script ordered_all_match {
            scope = country
            trigger = {
                ordered_pop = {
                    limit = 2
                    pop_size_above = 50
                }
            }
        }
        script ordered_window_has_nonmatch {
            scope = country
            trigger = {
                ordered_pop = {
                    limit = 2
                    pop_size_above = 150
                }
            }
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    const auto* all_match = programs.find_script(symbols.find("ordered_all_match"));
    const auto* nonmatch = programs.find_script(symbols.find("ordered_window_has_nonmatch"));
    assert(all_match != nullptr && nonmatch != nullptr);

    // Both pops (sizes 100 and 200) exceed 50, so the whole ordered window
    // matches -> condition TRUE.
    assert(vm.evaluate(*all_match, fixture.world, context));

    // pop_size_above = 150: the size-100 pop does NOT match, so the window is
    // not universally matching. Under "every" semantics this is FALSE; the old
    // "any" behaviour would have returned TRUE (the size-200 pop did match).
    assert(!vm.evaluate(*nonmatch, fixture.world, context));
}

void test_iterator_limit_condition_filtering() {
    // `limit = { <conditions> }` candidate gating (Clausewitz-style): failing
    // candidates are excluded from every/any/ordered iteration on both the
    // effect and trigger sides. Distinct from the numeric `limit = N` cap.
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script gated_grant {
            scope = country
            effect = {
                every_country = {
                    limit = { treasury_above = 150 }
                    add_treasury = 10
                }
            }
        }
        script ordered_top_up {
            scope = country
            effect = {
                ordered_country = {
                    limit = { treasury_above = 150 }
                    order_by = order_wealth
                    limit = 1
                    add_treasury = 100
                }
            }
        }
        script every_gated {
            scope = country
            trigger = {
                every_country = {
                    limit = { treasury_above = 150 }
                    treasury_above = 190
                }
            }
        }
        script any_gated {
            scope = country
            trigger = {
                any_country = {
                    limit = { treasury_above = 150 }
                    treasury_above = 250
                }
            }
        }
        script any_gated_empty {
            scope = country
            trigger = {
                any_country = {
                    limit = { treasury_above = 99999 }
                    treasury_above = 0
                }
            }
        }
        scripted_value order_wealth {
            scope = country
            source = treasury
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};
    auto effect_of = [&](std::string_view name) {
        return programs.find_script(symbols.find(name));
    };

    // Baseline: both countries sit at 100 milli-treasury units, so the gate
    // excludes everyone and the grant never fires.
    const double first_before = fixture.world.countries.treasury(fixture.first);
    const double second_before = fixture.world.countries.treasury(fixture.second);
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    (void)vm.execute_if(*effect_of("gated_grant"), fixture.world, context);
    assert(fixture.world.countries.treasury(fixture.first) == first_before);
    assert(fixture.world.countries.treasury(fixture.second) == second_before);

    // Raise country 2 above the gate: only it receives the grant.
    fixture.world.countries.set_treasury_milli(
        fixture.second, static_cast<EconomyAmount>(200.0 * 1000.0));
    context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    (void)vm.execute_if(*effect_of("gated_grant"), fixture.world, context);
    assert(fixture.world.countries.treasury(fixture.first) == first_before);
    assert(std::abs(fixture.world.countries.treasury(fixture.second) - 210.0) < 1e-9);

    // Trigger side (country 2 sits at 210): every gated country must satisfy
    // the body (210 >= 190), any gated country must satisfy 250 (false), and a
    // gate that filters out every candidate makes any_* false.
    context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    assert(vm.evaluate(*effect_of("every_gated"), fixture.world, context));
    assert(!vm.evaluate(*effect_of("any_gated"), fixture.world, context));
    assert(!vm.evaluate(*effect_of("any_gated_empty"), fixture.world, context));

    // Ordered + block filter + numeric cap: the top filtered country gains 100.
    context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    (void)vm.execute_if(*effect_of("ordered_top_up"), fixture.world, context);
    assert(fixture.world.countries.treasury(fixture.first) == first_before);
    assert(std::abs(fixture.world.countries.treasury(fixture.second) - 310.0) < 1e-9);
}

void test_random_list_weighted_branches() {
    // `random_list = { <weight> = { <effects> } ... }`: numeric branch keys are
    // weights, the branch draw is the keyed deterministic RNG, and malformed
    // weights are compile diagnostics.
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();

    // Malformed weight must be a compile-time diagnostic.
    {
        SymbolTable bad_symbols;
        ThunderScriptParser bad_parser{bad_symbols};
        const auto bad_parsed = bad_parser.parse(R"THUNDER(
            script bad {
                scope = country
                effect = {
                    random_list = {
                        heavy = { add_treasury = 1 }
                    }
                }
            }
        )THUNDER", "bad_random_list.thunder");
        assert(bad_parsed.ok()); // numeric-key parse accepts symbol keys too
        ScriptProgramDatabase bad_programs;
        ScriptCompiler bad_compiler{bad_symbols, registry};
        std::vector<ScriptCompileDiagnostic> bad_diagnostics;
        assert(!bad_compiler.compile(bad_parsed, bad_programs, bad_diagnostics));
        assert(!bad_diagnostics.empty());
        assert(bad_diagnostics.front().message.find("numeric weight") != std::string::npos);
    }

    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script weighted_bonus {
            scope = country
            effect = {
                random_list = {
                    10 = { add_treasury = 10 }
                    90 = { add_treasury = 1 }
                }
            }
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};
    const auto* script = programs.find_script(symbols.find("weighted_bonus"));
    assert(script != nullptr);

    // Determinism: the same seed must pick the same branch every time.
    const auto apply_once = [&](std::uint64_t seed) {
        const double before = fixture.world.countries.treasury(fixture.first);
        auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first), {}, seed);
        (void)vm.execute_if(*script, fixture.world, context);
        return fixture.world.countries.treasury(fixture.first) - before;
    };
    const auto first_run = apply_once(42u);
    const auto repeat_run = apply_once(42u);
    assert(first_run == repeat_run);

    // Both branches are reachable (10/90 weights; with 100 draws the 10-weight
    // branch is missed with probability ~2.7e-5).
    bool saw_major = false;
    bool saw_minor = false;
    for (std::uint64_t seed = 0u; seed < 100u; ++seed) {
        const auto delta = apply_once(seed);
        if (delta == 1.0) saw_major = true;
        if (delta == 10.0) saw_minor = true;
    }
    assert(saw_major);
    assert(saw_minor);
}

void test_value_expressions_and_comparisons() {
    // Value expression grammar: `expression = (...)` scripted values lower to
    // ScriptValueBytecode (PushSource/PushScriptedValue/PushVariable), and
    // trigger blocks accept infix comparisons (`pop_size > 150`,
    // `value:income - 50 >= 40`). Constant-only expressions still fold at
    // parse time.
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();

    // Unknown operand must be a compile-time diagnostic.
    {
        SymbolTable bad_symbols;
        ThunderScriptParser bad_parser{bad_symbols};
        const auto bad_parsed = bad_parser.parse(R"THUNDER(
            scripted_value broken {
                scope = country
                expression = (no_such_operand + 1)
            }
        )THUNDER", "broken_expression.thunder");
        assert(bad_parsed.ok());
        ScriptProgramDatabase bad_programs;
        ScriptCompiler bad_compiler{bad_symbols, registry};
        std::vector<ScriptCompileDiagnostic> bad_diagnostics;
        assert(!bad_compiler.compile(bad_parsed, bad_programs, bad_diagnostics));
        assert(!bad_diagnostics.empty());
        assert(bad_diagnostics.front().message.find("unknown value expression operand") !=
               std::string::npos);
    }

    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        scripted_value levy {
            scope = country
            expression = (population * 0.5 + gdp * 2) / 1000
        }
        scripted_value folded_constant {
            scope = country
            expression = (900 - 100) / 4 * 10
        }
        scripted_value income {
            scope = country
            source = treasury
        }
        script pop_gate {
            scope = pop
            trigger = { pop_size > 150 }
        }
        script pop_gate_at_most {
            scope = pop
            trigger = { pop_size <= 100 }
        }
        script balance_check {
            scope = country
            trigger = { value:income - 50 >= 40 }
        }
        script balance_check_high {
            scope = country
            trigger = { value:income - 50 >= 60 }
        }
        script savings_check {
            scope = country
            trigger = { var:savings > 10 }
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};

    // Expression scripted value against live world state:
    // (1000 * 0.5 + 100 * 2) / 1000 = 0.7
    const auto* levy = programs.find_value(symbols.find("levy"));
    assert(levy != nullptr && levy->uses_bytecode);
    const double levy_value = vm.evaluate(*levy, fixture.world, ScopeRef::country(fixture.first));
    assert(std::abs(levy_value - 0.7) < 1e-9);

    // Constant-only expressions fold exactly as the legacy folder did.
    const auto* folded = programs.find_value(symbols.find("folded_constant"));
    assert(folded != nullptr);
    assert(std::abs(vm.evaluate(*folded, fixture.world, ScopeRef::country(fixture.first)) - 2000.0) < 1e-9);

    // Infix comparisons on the trigger side, both directions.
    const auto evaluate_on_pop = [&](std::string_view name, PopId pop) {
        auto context = ScriptExecutionContext::rooted(ScopeRef::pop(pop));
        return vm.evaluate(*programs.find_script(symbols.find(name)), fixture.world, std::move(context));
    };
    assert(evaluate_on_pop("pop_gate", fixture.second_pop));
    assert(!evaluate_on_pop("pop_gate", fixture.first_pop));
    assert(evaluate_on_pop("pop_gate_at_most", fixture.first_pop));
    assert(!evaluate_on_pop("pop_gate_at_most", fixture.second_pop));

    const auto evaluate_on_country = [&](std::string_view name) {
        auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
        return vm.evaluate(*programs.find_script(symbols.find(name)), fixture.world, std::move(context));
    };
    // treasury(100) - 50 = 50
    assert(evaluate_on_country("balance_check"));
    assert(!evaluate_on_country("balance_check_high"));

    // Variable operands participate in expressions.
    auto savings_context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    savings_context.set_variable(script_stable_key("savings"), ScriptArgument::numeric(42.0));
    assert(vm.evaluate(*programs.find_script(symbols.find("savings_check")),
                       fixture.world, std::move(savings_context)));
}

void test_while_loops_and_else_if_chains() {
    // `while = { limit = { ... } ... }`: bounded looping driven by variables,
    // with the work budget guaranteeing termination for a never-failing limit.
    // `else_if` chains attach to the innermost preceding if and short-circuit
    // like Clausewitz content expects.
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script count_up {
            scope = country
            effect = {
                set_variable = { name = iterations value = 0 }
                set_variable = { name = total value = 0 }
                while = {
                    limit = { variable_below = { name = iterations value = 5 } }
                    change_variable = { name = iterations value = 1 }
                    change_variable = { name = total value = 10 }
                }
            }
        }
        script infinite {
            scope = country
            effect = {
                set_variable = { name = spins value = 0 }
                while = {
                    limit = { variable_at_most = { name = spins value = 999999999 } }
                    change_variable = { name = spins value = 1 }
                }
            }
        }
        script classify {
            scope = country
            effect = {
                set_variable = { name = verdict value = 0 }
                if = {
                    limit = { variable_above = { name = wealth value = 100 } }
                    set_variable = { name = verdict value = 3 }
                }
                else_if = {
                    limit = { variable_above = { name = wealth value = 50 } }
                    set_variable = { name = verdict value = 2 }
                }
                else_if = {
                    limit = { variable_above = { name = wealth value = 0 } }
                    set_variable = { name = verdict value = 1 }
                }
                else = {
                    set_variable = { name = verdict value = -1 }
                }
            }
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};

    // The loop body runs exactly five times.
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    (void)vm.execute_if(*programs.find_script(symbols.find("count_up")), fixture.world, context);
    assert(context.variable(script_stable_key("iterations")).number == 5.0);
    assert(context.variable(script_stable_key("total")).number == 50.0);

    // A never-failing limit must terminate with the budget error.
    bool rejected = false;
    try {
        auto runaway = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
        (void)vm.execute_if(*programs.find_script(symbols.find("infinite")), fixture.world, runaway);
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()} == "ThunderScript execution budget exceeded";
    }
    assert(rejected);

    // else_if chain: first match wins, later branches are skipped.
    const auto classify = [&](double wealth) {
        auto ctx = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
        ctx.set_variable(script_stable_key("wealth"), ScriptArgument::numeric(wealth));
        (void)vm.execute_if(*programs.find_script(symbols.find("classify")), fixture.world, ctx);
        return ctx.variable(script_stable_key("verdict")).number;
    };
    assert(classify(150.0) == 3.0);
    assert(classify(80.0) == 2.0);
    assert(classify(10.0) == 1.0);
    assert(classify(-5.0) == -1.0);
}

void test_building_scope_and_primitives() {
    // Building scope (first spec-04 expansion domain): every_/ordered_ building
    // iterators from country/market/province scopes, dedicated triggers and
    // effects, and building value sources inside expressions.
    RuntimeFixture fixture;
    EconomyDefinitions definitions;
    fixture.world.markets.resize(1u, definitions);
    const MarketId market{0u};
    fixture.world.markets.set_owner(market, fixture.first);
    BuildingInit init{};
    init.market = market;
    init.type = BuildingTypeId{0u};
    init.level = 2u;
    const auto first_building = fixture.world.buildings.create(init);
    fixture.world.buildings.set_employees(first_building, 30u);
    init.level = 7u;
    const auto second_building = fixture.world.buildings.create(init);
    fixture.world.buildings.set_employees(second_building, 90u);
    (void)first_building;
    (void)second_building;

    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script small_workshops {
            scope = country
            effect = {
                every_building = {
                    limit = { building_level_above = 3 }
                    set_building_employees = 111
                }
            }
        }
        script boost_largest {
            scope = market
            effect = {
                ordered_building = {
                    limit = { building_level_above = 3 }
                    limit = 1
                    order_by = order_level
                    set_building_level = 9
                    building_add_cash = 5
                }
            }
        }
        script understaffed {
            scope = building
            trigger = {
                building_employees_above = 0
                building_level * 10 < building_employees
            }
        }
        scripted_value staffing_ratio {
            scope = building
            expression = (building_employees / (building_level * 20))
        }
        scripted_value order_level {
            scope = building
            source = building_level
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};

    // every_building + limit gate: only the level-7 building is touched.
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    (void)vm.execute_if(*programs.find_script(symbols.find("small_workshops")), fixture.world, context);
    assert(fixture.world.buildings.employees(BuildingId{0u}) == 30u);
    assert(fixture.world.buildings.employees(BuildingId{1u}) == 111u);

    // ordered_building from a market scope with expression order_by: the top
    // match is the level-7 (now 111-employee) building; level and cash set.
    context = ScriptExecutionContext::rooted(ScopeRef::market(market));
    (void)vm.execute_if(*programs.find_script(symbols.find("boost_largest")), fixture.world, context);
    assert(fixture.world.buildings.level(BuildingId{1u}) == 9u);
    assert(fixture.world.buildings.cash(BuildingId{1u}) == 5000);
    assert(fixture.world.buildings.level(BuildingId{0u}) == 2u);

    // Building-scoped trigger with an infix arithmetic comparison:
    // building 0: 2 * 10 = 20 < 30 -> true; building 1: 9 * 10 = 90 < 111 -> true.
    auto building_context = ScriptExecutionContext::rooted(ScopeRef::building(BuildingId{0u}));
    assert(vm.evaluate(*programs.find_script(symbols.find("understaffed")),
                       fixture.world, std::move(building_context)));

    // Expression scripted value on the building scope: 111 / (9 * 20) = 0.6166…
    const auto* staffing = programs.find_value(symbols.find("staffing_ratio"));
    assert(staffing != nullptr && staffing->uses_bytecode);
    const double ratio = vm.evaluate(*staffing, fixture.world, ScopeRef::building(BuildingId{1u}));
    assert(std::abs(ratio - 111.0 / 180.0) < 1e-9);
}

void test_war_front_army_scopes_and_primitives() {
    // Military/diplomatic domains (spec 04 batch two): armies under their
    // country, fronts under their war (pair match in either order), wars under
    // an active-only country view, plus primitives and value sources.
    RuntimeFixture fixture;
    auto& strategy = fixture.world.grand_strategy;

    ArmyRecord first_army{};
    first_army.country = fixture.first;
    first_army.location = fixture.state;
    first_army.manpower = 1000u;
    first_army.organization_ppm = 500'000u;
    strategy.add_army(first_army);
    ArmyRecord second_army{};
    second_army.country = fixture.first;
    second_army.location = fixture.state;
    second_army.manpower = 2000u;
    second_army.organization_ppm = 900'000u;
    strategy.add_army(second_army);
    ArmyRecord enemy_army{};
    enemy_army.country = fixture.second;
    enemy_army.manpower = 500u;
    strategy.add_army(enemy_army);

    WarRecord war{};
    war.attacker = fixture.first;
    war.defender = fixture.second;
    war.war_score_milli = -60'000;
    war.weeks = 10u;
    war.active = true;
    const auto active_war = strategy.add_war(war);

    FrontRecord first_front{};
    first_front.first = fixture.first;
    first_front.second = fixture.second;
    first_front.state = fixture.state;
    first_front.progress_milli = -20'000;
    strategy.add_front(first_front);
    FrontRecord second_front{};
    second_front.first = fixture.second;
    second_front.second = fixture.first;
    second_front.state = fixture.state;
    second_front.progress_milli = 30'000;
    strategy.add_front(second_front);

    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"THUNDER(
        script mobilize_reserves {
            scope = country
            effect = {
                every_army = {
                    limit = { army_organization_above = 0.2 }
                    army_add_manpower = 100
                }
            }
        }
        script push_fronts {
            scope = war
            effect = {
                every_front = {
                    limit = { front_progress_above = -100 }
                    shift_front_progress = 5
                }
                shift_war_score = 3
            }
        }
        script losing_war {
            scope = war
            trigger = {
                war_active = yes
                war_score < -50
            }
        }
        scripted_value front_urgency {
            scope = front
            expression = (front_progress * -1 + 100)
        }
    )THUNDER");
    ScriptVm vm{registry, &programs};

    // Gated army iteration touches only the executing country's units.
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    (void)vm.execute_if(*programs.find_script(symbols.find("mobilize_reserves")), fixture.world, context);
    assert(strategy.armys()[0].manpower == 1100u);
    assert(strategy.armys()[1].manpower == 2100u);
    assert(strategy.armys()[2].manpower == 500u);

    // War scope drives its fronts (pair match in either order) and its score.
    context = ScriptExecutionContext::rooted(ScopeRef::war(active_war));
    (void)vm.execute_if(*programs.find_script(symbols.find("push_fronts")), fixture.world, context);
    assert(strategy.fronts()[0].progress_milli == -15'000);
    assert(strategy.fronts()[1].progress_milli == 35'000);
    assert(strategy.wars()[0].war_score_milli == -57'000);

    // Infix comparison plus the boolean trigger on the war scope.
    context = ScriptExecutionContext::rooted(ScopeRef::war(active_war));
    assert(vm.evaluate(*programs.find_script(symbols.find("losing_war")), fixture.world, std::move(context)));

    // Expression value on the front scope (after push_fronts): -(-15) + 100 = 115.
    const auto* urgency = programs.find_value(symbols.find("front_urgency"));
    assert(urgency != nullptr && urgency->uses_bytecode);
    const double value = vm.evaluate(*urgency, fixture.world, ScopeRef::front(FrontId{0u}));
    assert(std::abs(value - 115.0) < 1e-9);
}

void test_script_doc_and_schema_export() {
    const auto registry = ScriptRegistry::make_builtin();
    const auto docs = registry.export_markdown_docs();
    const auto schema = registry.export_schema_json();
    assert(!docs.empty());
    assert(!schema.empty());
    assert(docs.find("# ThunderScript API Documentation") != std::string::npos);
    assert(schema.find("\"triggers\":") != std::string::npos);
    assert(schema.find("\"effects\":") != std::string::npos);
}

} // namespace

int main() {
    test_context_frames_stable_bindings_collections_and_checksum();
    test_parameterized_calls_event_targets_variables_and_values();
    test_scope_collections_and_event_target_selectors();
    test_mixed_backends_empty_trigger_and_stable_callsites();
    test_parser_resource_and_non_finite_limits();
    test_static_argument_type_diagnostics();
    test_typed_script_signatures_defaults_linking_and_runtime();
    test_deterministic_execution_budget();
    test_dynamic_variable_map();
    test_weighted_random_list();
    test_script_profiler();
    test_ordered_iterator_condition_every_semantics();
    test_iterator_limit_condition_filtering();
    test_random_list_weighted_branches();
    test_value_expressions_and_comparisons();
    test_while_loops_and_else_if_chains();
    test_building_scope_and_primitives();
    test_war_front_army_scopes_and_primitives();
    test_script_doc_and_schema_export();
    std::cout << "ThunderScript runtime tests passed\n";
}

