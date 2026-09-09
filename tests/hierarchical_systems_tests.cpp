#include "thunder/simulation/kernel/HierarchicalModifierGraph.hpp"
#include "thunder/simulation/world/HierarchicalPathfinder.hpp"
#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace thunder;

static void test_hierarchical_modifiers_basic() {
    HierarchicalModifierGraph graph;

    const CountryId country{1};
    const StateId state{10};
    const ProvinceId province{100};
    const BuildingId building{1000};

    graph.set_state_parent(state, country);
    graph.set_province_parent(province, state);
    graph.set_building_parent(building, province);

    assert(graph.building_parent(building) == province);
    assert(graph.province_parent(province) == state);
    assert(graph.state_parent(state) == country);

    // Add modifiers at different levels:
    // Global: +10 flat
    // Country: +20 flat
    // State: +20% mult (0.20)
    // Province: +10% mult (0.10)
    // Building: +5 flat
    graph.add_modifier(ModifierScopeLevel::Global, 0, "tax_output", ModifierOp::Add, 10.0);
    graph.add_modifier(ModifierScopeLevel::Country, country.value(), "tax_output", ModifierOp::Add, 20.0);
    graph.add_modifier(ModifierScopeLevel::State, state.value(), "tax_output", ModifierOp::Multiply, 0.20);
    graph.add_modifier(ModifierScopeLevel::Province, province.value(), "tax_output", ModifierOp::Multiply, 0.10);
    graph.add_modifier(ModifierScopeLevel::Building, building.value(), "tax_output", ModifierOp::Add, 5.0);

    // Base = 100
    // Total Add = 10 (Global) + 20 (Country) + 5 (Building) = 35
    // Total Mult = 0.20 (State) + 0.10 (Province) = 0.30 -> Factor = 1.30
    // Expected = (100 + 35) * 1.30 = 135 * 1.30 = 175.5
    const double val = graph.evaluate(ModifierScopeLevel::Building, building.value(), "tax_output", 100.0);
    assert(std::abs(val - 175.5) < 1e-6);

    // Query state level:
    // Base = 100
    // Total Add = 10 (Global) + 20 (Country) = 30
    // Total Mult = 0.20 (State) -> Factor = 1.20
    // Expected = (100 + 30) * 1.20 = 130 * 1.20 = 156.0
    const double state_val = graph.evaluate(ModifierScopeLevel::State, state.value(), "tax_output", 100.0);
    assert(std::abs(state_val - 156.0) < 1e-6);

    std::cout << "[PASS] Hierarchical modifier basic accumulation\n";
}

static void test_hierarchical_modifiers_clamping_and_bool() {
    HierarchicalModifierGraph graph;
    const CountryId country{2};
    const StateId state{20};
    const ProvinceId province{200};
    graph.set_state_parent(state, country);
    graph.set_province_parent(province, state);

    graph.add_modifier(ModifierScopeLevel::Country, country.value(), "rail_access", ModifierOp::BoolFlag, 1.0);
    assert(graph.evaluate_bool(ModifierScopeLevel::Province, province.value(), "rail_access") == true);
    assert(graph.evaluate_bool(ModifierScopeLevel::Global, 0, "rail_access") == false);

    // Test min/max clamping
    graph.add_modifier(ModifierScopeLevel::State, state.value(), "wage_rate", ModifierOp::Min, 50.0);
    graph.add_modifier(ModifierScopeLevel::Country, country.value(), "wage_rate", ModifierOp::Max, 200.0);

    const double clamped_low = graph.evaluate(ModifierScopeLevel::Province, province.value(), "wage_rate", 10.0);
    assert(clamped_low == 50.0);

    const double clamped_high = graph.evaluate(ModifierScopeLevel::Province, province.value(), "wage_rate", 500.0);
    assert(clamped_high == 200.0);

    std::cout << "[PASS] Hierarchical modifier clamping and boolean flags\n";
}

static void test_hierarchical_modifiers_revision_and_ppm() {
    HierarchicalModifierGraph graph;
    const CountryId country{3};
    const StateId state{30};
    graph.set_state_parent(state, country);

    const auto rev_before = graph.revision(ModifierScopeLevel::Country, country.value());
    graph.add_modifier(ModifierScopeLevel::Country, country.value(), "efficiency", ModifierOp::Multiply, 0.15, 42);
    const auto rev_after = graph.revision(ModifierScopeLevel::Country, country.value());
    assert(rev_after > rev_before);

    // PPM Evaluation: base 1,000,000 PPM (1.0) with +15% -> 1,150,000 PPM
    const auto ppm = graph.evaluate_ppm(ModifierScopeLevel::State, state.value(), "efficiency", 1'000'000);
    assert(ppm == 1'150'000);

    // Remove modifier by source ID
    const bool removed = graph.remove_modifier_by_source(ModifierScopeLevel::Country, country.value(), "efficiency", 42);
    assert(removed);
    const auto ppm_reset = graph.evaluate_ppm(ModifierScopeLevel::State, state.value(), "efficiency", 1'000'000);
    assert(ppm_reset == 1'000'000);

    assert(graph.checksum() != 0);

    std::cout << "[PASS] Hierarchical modifier revision tracking and PPM\n";
}

static void test_hierarchical_pathfinder_and_hpa() {
    // Setup a 4-state topology:
    // State 0: Provinces 0, 1, 2
    // State 1: Provinces 3, 4, 5
    // State 2: Provinces 6, 7, 8
    // State 3: Provinces 9, 10, 11
    // Topology layout:
    // S0: [0] - [1] - [2]
    //                  | (border portal 2 <-> 3)
    // S1:             [3] - [4] - [5]
    //                              | (border portal 5 <-> 6)
    // S2:                         [6] - [7] - [8]
    //                                          | (border portal 8 <-> 9)
    // S3:                                     [9] - [10] - [11]

    ProvinceAdjacencyGraph adj;
    std::vector<ProvinceAdjacencyInput> edges = {
        // State 0 internal
        {ProvinceId{0}, ProvinceId{1}, AdjacencyLand, 256u},
        {ProvinceId{1}, ProvinceId{2}, AdjacencyLand, 256u},
        // S0 <-> S1 portal
        {ProvinceId{2}, ProvinceId{3}, AdjacencyLand, 300u},
        // State 1 internal
        {ProvinceId{3}, ProvinceId{4}, AdjacencyLand, 256u},
        {ProvinceId{4}, ProvinceId{5}, AdjacencyLand, 256u},
        // S1 <-> S2 portal
        {ProvinceId{5}, ProvinceId{6}, AdjacencyRiver, 400u},
        // State 2 internal
        {ProvinceId{6}, ProvinceId{7}, AdjacencyLand, 256u},
        {ProvinceId{7}, ProvinceId{8}, AdjacencyLand, 256u},
        // S2 <-> S3 portal
        {ProvinceId{8}, ProvinceId{9}, AdjacencyLand, 256u},
        // State 3 internal
        {ProvinceId{9}, ProvinceId{10}, AdjacencyLand, 256u},
        {ProvinceId{10}, ProvinceId{11}, AdjacencyLand, 256u},
    };
    adj.build(12, edges);

    std::vector<StateId> states = {
        StateId{0}, StateId{0}, StateId{0},
        StateId{1}, StateId{1}, StateId{1},
        StateId{2}, StateId{2}, StateId{2},
        StateId{3}, StateId{3}, StateId{3}
    };

    std::vector<double> xs(12, 0.0);
    std::vector<double> ys(12, 0.0);
    for (std::size_t i = 0; i < 12; ++i) {
        xs[i] = static_cast<double>(i) * 1000.0;
        ys[i] = 0.0;
    }

    HierarchicalMapGraph map_graph;
    map_graph.build(adj, states, xs, ys);

    assert(map_graph.portal_count() == 6); // 3 pairs of bidirectional portals = 6 directed portals
    assert(map_graph.checksum() != 0);

    HierarchicalPathfinder pathfinder(map_graph);

    // 1. Same-state test (Province 0 to 2)
    const auto same_state_res = pathfinder.find_path_hpa(ProvinceId{0}, ProvinceId{2});
    assert(same_state_res.success);
    assert(same_state_res.path.size() == 3);
    assert(same_state_res.path[0] == ProvinceId{0});
    assert(same_state_res.path[1] == ProvinceId{1});
    assert(same_state_res.path[2] == ProvinceId{2});
    assert(same_state_res.total_cost_q8 == 512u);

    // 2. Long distance cross-state test (Province 0 to Province 11)
    const auto hpa_res = pathfinder.find_path_hpa(ProvinceId{0}, ProvinceId{11});
    assert(hpa_res.success);
    assert(hpa_res.path.size() == 12);
    assert(hpa_res.path.front() == ProvinceId{0});
    assert(hpa_res.path.back() == ProvinceId{11});

    // Check step-by-step continuity
    for (std::size_t i = 1; i < hpa_res.path.size(); ++i) {
        assert(adj.adjacent(hpa_res.path[i - 1], hpa_res.path[i]));
    }

    // Compare with low-level search
    const auto low_res = pathfinder.find_path_low_level(ProvinceId{0}, ProvinceId{11});
    assert(low_res.success);
    assert(low_res.total_cost_q8 == hpa_res.total_cost_q8);

    std::cout << "[PASS] Hierarchical pathfinding (HPA*) and portal graph\n";
}

static void test_supply_network_solver() {
    ProvinceAdjacencyGraph adj;
    std::vector<ProvinceAdjacencyInput> edges = {
        {ProvinceId{0}, ProvinceId{1}, AdjacencyLand, 200u},
        {ProvinceId{1}, ProvinceId{2}, AdjacencyLand, 300u},
        {ProvinceId{2}, ProvinceId{3}, AdjacencyImpassable, 1000u}, // Blocked mountain pass
        {ProvinceId{1}, ProvinceId{4}, AdjacencyLand, 250u},
        {ProvinceId{4}, ProvinceId{5}, AdjacencyLand, 250u}
    };
    adj.build(6, edges);

    std::vector<StateId> states(6, StateId{0});
    HierarchicalMapGraph map_graph;
    map_graph.build(adj, states);

    HierarchicalPathfinder pathfinder(map_graph);

    std::vector<SupplySource> sources = {
        {ProvinceId{0}, 1000u, 1000u} // Supply depot at 0 with capacity 1000 and range 1000 Q8
    };

    const auto supply = pathfinder.compute_supply_network(sources, AdjacencyImpassable);

    assert(supply.is_supplied[0] == true);
    assert(supply.supply_level[0] == 1000u);

    // Province 1 is distance 200 -> supply fraction = 1 - 200/1000 = 0.8 -> 800
    assert(supply.is_supplied[1] == true);
    assert(supply.distance_q8[1] == 200u);
    assert(supply.supply_level[1] == 800u);

    // Province 2 is distance 500 -> supply fraction = 1 - 500/1000 = 0.5 -> 500
    assert(supply.is_supplied[2] == true);
    assert(supply.distance_q8[2] == 500u);
    assert(supply.supply_level[2] == 500u);

    // Province 3 is blocked by AdjacencyImpassable -> not supplied
    assert(supply.is_supplied[3] == false);
    assert(supply.supply_level[3] == 0u);

    // Province 5 is distance 200 + 250 + 250 = 700 -> supply = 300
    assert(supply.is_supplied[5] == true);
    assert(supply.distance_q8[5] == 700u);
    assert(supply.supply_level[5] == 300u);

    assert(supply.supplied_province_count == 5); // 0, 1, 2, 4, 5

    std::cout << "[PASS] Supply network reachability and capacity solver\n";
}

int main() {
    std::cout << "Running Hierarchical Systems tests...\n";
    test_hierarchical_modifiers_basic();
    test_hierarchical_modifiers_clamping_and_bool();
    test_hierarchical_modifiers_revision_and_ppm();
    test_hierarchical_pathfinder_and_hpa();
    test_supply_network_solver();
    std::cout << "All Hierarchical Systems tests passed successfully!\n";
    return 0;
}
