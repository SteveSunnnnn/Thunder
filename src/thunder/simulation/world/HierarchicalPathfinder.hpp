#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/world/GeographyStore.hpp"
#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace thunder {

struct Portal {
    std::uint32_t id = 0;
    ProvinceId local_province{};
    ProvinceId remote_province{};
    StateId local_state{};
    StateId remote_state{};
    std::uint16_t transition_cost_q8 = 256u;
    std::uint16_t flags = 0u;
};

struct IntraStateEdge {
    std::uint32_t target_portal_id = 0;
    std::uint32_t cost_q8 = 0;
    std::vector<ProvinceId> intermediate_path; // Local provinces from portal A to portal B
};

struct PathResult {
    std::vector<ProvinceId> path;
    std::uint32_t total_cost_q8 = 0;
    std::size_t nodes_explored = 0;
    bool success = false;
};

struct SupplySource {
    ProvinceId province{};
    std::uint32_t capacity = 1000u;
    std::uint32_t max_range_q8 = 100'000u; // Cost range limit
};

struct SupplyNetworkResult {
    std::vector<std::uint32_t> supply_level;
    std::vector<std::uint32_t> distance_q8;
    std::vector<ProvinceId> primary_source;
    std::vector<bool> is_supplied;
    std::size_t supplied_province_count = 0;
};

// Abstract Level 1 Graph for HPA*
class HierarchicalMapGraph {
public:
    void build(const ProvinceAdjacencyGraph& adjacency,
               std::span<const StateId> province_states,
               std::span<const double> province_center_x = {},
               std::span<const double> province_center_y = {});

    [[nodiscard]] std::size_t portal_count() const noexcept { return portals_.size(); }
    [[nodiscard]] std::span<const Portal> portals() const noexcept { return portals_; }
    [[nodiscard]] std::span<const std::uint32_t> state_portals(StateId state) const noexcept;
    [[nodiscard]] std::span<const IntraStateEdge> portal_intra_edges(std::uint32_t portal_id) const noexcept;

    [[nodiscard]] const ProvinceAdjacencyGraph* adjacency() const noexcept { return adjacency_; }
    [[nodiscard]] std::span<const StateId> province_states() const noexcept { return province_states_; }
    [[nodiscard]] std::uint32_t province_count() const noexcept {
        return static_cast<std::uint32_t>(province_states_.size());
    }

    [[nodiscard]] double province_x(ProvinceId p) const noexcept;
    [[nodiscard]] double province_y(ProvinceId p) const noexcept;

    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    void extract_portals();
    void build_intra_state_edges();

    const ProvinceAdjacencyGraph* adjacency_ = nullptr;
    std::vector<StateId> province_states_;
    std::vector<double> province_center_x_;
    std::vector<double> province_center_y_;

    std::vector<Portal> portals_;
    std::vector<std::uint32_t> state_portal_offsets_;
    std::vector<std::uint32_t> state_portal_indices_;

    std::vector<std::vector<IntraStateEdge>> intra_edges_;
};

// Hierarchical Pathfinding Agent (HPA*) and Supply Network Solver
class HierarchicalPathfinder {
public:
    explicit HierarchicalPathfinder(const HierarchicalMapGraph& graph);

    // Standard low-level A* (Level 0 search)
    [[nodiscard]] PathResult find_path_low_level(
        ProvinceId start, ProvinceId goal,
        std::uint16_t impassable_mask = AdjacencyImpassable,
        StateId constrained_state = {}) const;

    // Hierarchical Pathfinding (HPA* Level 1 + Level 0 expansion)
    [[nodiscard]] PathResult find_path_hpa(
        ProvinceId start, ProvinceId goal,
        std::uint16_t impassable_mask = AdjacencyImpassable) const;

    // Supply Network Solver: multi-source reachability and throughput propagation
    [[nodiscard]] SupplyNetworkResult compute_supply_network(
        std::span<const SupplySource> sources,
        std::uint16_t impassable_mask = AdjacencyImpassable) const;

private:
    const HierarchicalMapGraph& graph_;
};

} // namespace thunder
