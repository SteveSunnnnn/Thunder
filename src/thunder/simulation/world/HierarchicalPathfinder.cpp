#include "thunder/simulation/world/HierarchicalPathfinder.hpp"

#include "thunder/foundation/base/Hash.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace thunder {

namespace {

struct AStarNode {
    std::uint32_t province = 0;
    std::uint32_t g_cost = 0;
    std::uint32_t f_cost = 0;

    bool operator>(const AStarNode& other) const noexcept {
        return f_cost > other.f_cost;
    }
};

struct PortalSearchNode {
    std::uint32_t portal_id = 0;
    std::uint32_t g_cost = 0;
    std::uint32_t f_cost = 0;

    bool operator>(const PortalSearchNode& other) const noexcept {
        return f_cost > other.f_cost;
    }
};

[[nodiscard]] std::uint32_t heuristic_distance(
    ProvinceId a, ProvinceId b,
    const HierarchicalMapGraph& graph) noexcept {
    const double x1 = graph.province_x(a);
    const double y1 = graph.province_y(a);
    const double x2 = graph.province_x(b);
    const double y2 = graph.province_y(b);
    if (x1 == 0.0 && y1 == 0.0 && x2 == 0.0 && y2 == 0.0) return 0u;
    const double dx = x1 - x2;
    const double dy = y1 - y2;
    const double dist_m = std::sqrt(dx * dx + dy * dy);
    // Convert meters to approximate Q8 cost (e.g. 1000m approx 256 Q8)
    return static_cast<std::uint32_t>(dist_m * 0.256);
}

} // namespace

void HierarchicalMapGraph::build(const ProvinceAdjacencyGraph& adjacency,
                                 std::span<const StateId> province_states,
                                 std::span<const double> province_center_x,
                                 std::span<const double> province_center_y) {
    adjacency_ = &adjacency;
    province_states_.assign(province_states.begin(), province_states.end());
    if (!province_center_x.empty()) {
        province_center_x_.assign(province_center_x.begin(), province_center_x.end());
    } else {
        province_center_x_.assign(province_states_.size(), 0.0);
    }
    if (!province_center_y.empty()) {
        province_center_y_.assign(province_center_y.begin(), province_center_y.end());
    } else {
        province_center_y_.assign(province_states_.size(), 0.0);
    }

    extract_portals();
    build_intra_state_edges();
}

void HierarchicalMapGraph::extract_portals() {
    portals_.clear();
    state_portal_offsets_.clear();
    state_portal_indices_.clear();

    if (!adjacency_ || province_states_.empty()) return;

    std::uint32_t max_state_id = 0;
    for (const auto s : province_states_) {
        if (s.valid() && s.value() < 1'000'000u) max_state_id = std::max(max_state_id, s.value());
    }

    const auto p_count = static_cast<std::uint32_t>(province_states_.size());
    for (std::uint32_t pi = 0; pi < p_count; ++pi) {
        const ProvinceId p{pi};
        const auto s_local = province_states_[pi];
        // Must use the same cap as the max_state_id scan above: state_portal_
        // offsets_ is sized from max_state_id, so a state id at or above the
        // cap would index past the end of that array (here and later in
        // state_portals()).
        if (!s_local.valid() || s_local.value() >= 1'000'000u) continue;

        for (const auto& neighbor : adjacency_->neighbors(p)) {
            const auto ni = neighbor.province;
            if (ni >= p_count) continue;
            const auto s_remote = province_states_[ni];
            if (!s_remote.valid() || s_remote == s_local || s_remote.value() >= 1'000'000u) continue;
            if ((neighbor.flags & AdjacencyImpassable) != 0) continue;

            Portal portal;
            portal.id = static_cast<std::uint32_t>(portals_.size());
            portal.local_province = p;
            portal.remote_province = ProvinceId{ni};
            portal.local_state = s_local;
            portal.remote_state = s_remote;
            portal.transition_cost_q8 = neighbor.base_cost_q8;
            portal.flags = neighbor.flags;
            portals_.push_back(portal);
        }
    }

    state_portal_offsets_.assign(static_cast<std::size_t>(max_state_id) + 2u, 0u);
    for (const auto& portal : portals_) {
        ++state_portal_offsets_[static_cast<std::size_t>(portal.local_state.value()) + 1u];
    }
    for (std::size_t i = 1; i < state_portal_offsets_.size(); ++i) {
        state_portal_offsets_[i] += state_portal_offsets_[i - 1u];
    }

    state_portal_indices_.resize(portals_.size());
    std::vector<std::uint32_t> current_offsets = state_portal_offsets_;
    for (const auto& portal : portals_) {
        const auto s_idx = static_cast<std::size_t>(portal.local_state.value());
        state_portal_indices_[current_offsets[s_idx]++] = portal.id;
    }
}

void HierarchicalMapGraph::build_intra_state_edges() {
    intra_edges_.clear();
    intra_edges_.resize(portals_.size());
    if (portals_.empty()) return;

    HierarchicalPathfinder pathfinder(*this);

    // For each state, compute shortest paths between all pairs of portals in that state
    const auto num_states = static_cast<std::uint32_t>(state_portal_offsets_.size() > 1 ? state_portal_offsets_.size() - 1 : 0);
    for (std::uint32_t s = 0; s < num_states; ++s) {
        const auto p_ids = state_portals(StateId{s});
        for (std::size_t i = 0; i < p_ids.size(); ++i) {
            const auto p1_id = p_ids[i];
            const auto& p1 = portals_[p1_id];
            for (std::size_t j = 0; j < p_ids.size(); ++j) {
                if (i == j) continue;
                const auto p2_id = p_ids[j];
                const auto& p2 = portals_[p2_id];

                // Low-level search restricted to state S
                const auto res = pathfinder.find_path_low_level(
                    p1.local_province, p2.local_province,
                    AdjacencyImpassable, StateId{s});

                if (res.success) {
                    IntraStateEdge edge;
                    edge.target_portal_id = p2_id;
                    edge.cost_q8 = res.total_cost_q8;
                    edge.intermediate_path = res.path;
                    intra_edges_[p1_id].push_back(std::move(edge));
                }
            }
        }
    }
}

std::span<const std::uint32_t> HierarchicalMapGraph::state_portals(StateId state) const noexcept {
    if (!state.valid()) return {};
    const auto s = static_cast<std::size_t>(state.value());
    if (s + 1u >= state_portal_offsets_.size()) return {};
    const auto begin = state_portal_offsets_[s];
    const auto end = state_portal_offsets_[s + 1u];
    return std::span<const std::uint32_t>{state_portal_indices_.data() + begin, static_cast<std::size_t>(end - begin)};
}

std::span<const IntraStateEdge> HierarchicalMapGraph::portal_intra_edges(std::uint32_t portal_id) const noexcept {
    if (portal_id >= intra_edges_.size()) return {};
    return intra_edges_[portal_id];
}

double HierarchicalMapGraph::province_x(ProvinceId p) const noexcept {
    if (!p.valid() || p.value() >= province_center_x_.size()) return 0.0;
    return province_center_x_[p.value()];
}

double HierarchicalMapGraph::province_y(ProvinceId p) const noexcept {
    if (!p.valid() || p.value() >= province_center_y_.size()) return 0.0;
    return province_center_y_[p.value()];
}

std::size_t HierarchicalMapGraph::memory_bytes() const noexcept {
    std::size_t total = sizeof(*this);
    total += province_states_.capacity() * sizeof(StateId);
    total += province_center_x_.capacity() * sizeof(double);
    total += province_center_y_.capacity() * sizeof(double);
    total += portals_.capacity() * sizeof(Portal);
    total += state_portal_offsets_.capacity() * sizeof(std::uint32_t);
    total += state_portal_indices_.capacity() * sizeof(std::uint32_t);
    total += intra_edges_.capacity() * sizeof(std::vector<IntraStateEdge>);
    for (const auto& edges : intra_edges_) {
        total += edges.capacity() * sizeof(IntraStateEdge);
        for (const auto& e : edges) {
            total += e.intermediate_path.capacity() * sizeof(ProvinceId);
        }
    }
    return total;
}

std::uint64_t HierarchicalMapGraph::checksum() const noexcept {
    Fnv1a64 h;
    h.add(static_cast<std::uint64_t>(portals_.size()));
    for (const auto& p : portals_) {
        h.add(p.id);
        h.add(p.local_province.value());
        h.add(p.remote_province.value());
        h.add(p.local_state.value());
        h.add(p.remote_state.value());
        h.add(p.transition_cost_q8);
        h.add(p.flags);
    }
    for (const auto& edges : intra_edges_) {
        h.add(static_cast<std::uint64_t>(edges.size()));
        for (const auto& e : edges) {
            h.add(e.target_portal_id);
            h.add(e.cost_q8);
            h.add(static_cast<std::uint64_t>(e.intermediate_path.size()));
        }
    }
    return h.value();
}

HierarchicalPathfinder::HierarchicalPathfinder(const HierarchicalMapGraph& graph)
    : graph_(graph) {}

PathResult HierarchicalPathfinder::find_path_low_level(
    ProvinceId start, ProvinceId goal,
    std::uint16_t impassable_mask,
    StateId constrained_state) const {
    PathResult result;
    if (!start.valid() || !goal.valid()) return result;
    if (start == goal) {
        result.path.push_back(start);
        result.total_cost_q8 = 0;
        result.success = true;
        return result;
    }

    const auto* adj = graph_.adjacency();
    if (!adj) return result;
    const auto p_count = adj->province_count();
    if (start.value() >= p_count || goal.value() >= p_count) return result;

    const auto states = graph_.province_states();
    if (constrained_state.valid()) {
        if (start.value() >= states.size() || states[start.value()] != constrained_state) return result;
        if (goal.value() >= states.size() || states[goal.value()] != constrained_state) return result;
    }

    std::vector<std::uint32_t> g_score(p_count, std::numeric_limits<std::uint32_t>::max());
    std::vector<std::uint32_t> came_from(p_count, 0xffffffffu);
    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> open_set;

    g_score[start.value()] = 0;
    const auto h_start = heuristic_distance(start, goal, graph_);
    open_set.push({start.value(), 0, h_start});

    std::size_t explored = 0;

    while (!open_set.empty()) {
        const auto current = open_set.top();
        open_set.pop();
        ++explored;

        if (current.province == goal.value()) {
            // Reconstruct path
            result.success = true;
            result.total_cost_q8 = g_score[goal.value()];
            result.nodes_explored = explored;

            auto curr_p = goal.value();
            while (curr_p != 0xffffffffu) {
                result.path.push_back(ProvinceId{curr_p});
                curr_p = came_from[curr_p];
            }
            std::reverse(result.path.begin(), result.path.end());
            return result;
        }

        if (current.g_cost > g_score[current.province]) continue;

        for (const auto& neighbor : adj->neighbors(ProvinceId{current.province})) {
            if ((neighbor.flags & impassable_mask) != 0) continue;
            const auto np = neighbor.province;
            if (np >= p_count) continue;

            if (constrained_state.valid()) {
                if (np >= states.size() || states[np] != constrained_state) continue;
            }

            const auto tentative_g = current.g_cost + neighbor.base_cost_q8;
            if (tentative_g < g_score[np]) {
                g_score[np] = tentative_g;
                came_from[np] = current.province;
                const auto h = heuristic_distance(ProvinceId{np}, goal, graph_);
                open_set.push({np, tentative_g, tentative_g + h});
            }
        }
    }

    result.nodes_explored = explored;
    return result;
}

PathResult HierarchicalPathfinder::find_path_hpa(
    ProvinceId start, ProvinceId goal,
    std::uint16_t impassable_mask) const {
    PathResult result;
    if (!start.valid() || !goal.valid()) return result;
    if (start == goal) {
        result.path.push_back(start);
        result.total_cost_q8 = 0;
        result.success = true;
        return result;
    }

    const auto states = graph_.province_states();
    if (start.value() >= states.size() || goal.value() >= states.size()) return result;
    const auto start_state = states[start.value()];
    const auto goal_state = states[goal.value()];

    // Fast path: start and goal in same state
    if (start_state.valid() && start_state == goal_state) {
        const auto local_res = find_path_low_level(start, goal, impassable_mask, start_state);
        if (local_res.success) return local_res;
    }

    const auto total_portals = graph_.portal_count();
    if (total_portals == 0) {
        // Fallback to global low-level search if no portals
        return find_path_low_level(start, goal, impassable_mask);
    }

    // Abstract Graph Search
    const auto start_portals = graph_.state_portals(start_state);
    const auto goal_portals = graph_.state_portals(goal_state);

    if (start_portals.empty() || goal_portals.empty()) {
        return find_path_low_level(start, goal, impassable_mask);
    }

    // Portal search structures
    std::vector<std::uint32_t> g_score(total_portals, std::numeric_limits<std::uint32_t>::max());
    std::vector<std::uint32_t> came_from_portal(total_portals, 0xffffffffu);
    std::vector<std::vector<ProvinceId>> intra_step_path(total_portals);
    std::priority_queue<PortalSearchNode, std::vector<PortalSearchNode>, std::greater<PortalSearchNode>> open_set;

    std::size_t explored = 0;

    // Seed start portals
    for (const auto p_id : start_portals) {
        const auto& portal = graph_.portals()[p_id];
        const auto to_portal = find_path_low_level(start, portal.local_province, impassable_mask, start_state);
        if (to_portal.success) {
            g_score[p_id] = to_portal.total_cost_q8;
            intra_step_path[p_id] = to_portal.path;
            const auto h = heuristic_distance(portal.local_province, goal, graph_);
            open_set.push({p_id, g_score[p_id], g_score[p_id] + h});
        }
    }

    std::uint32_t best_goal_portal = 0xffffffffu;
    std::uint32_t best_total_cost = std::numeric_limits<std::uint32_t>::max();
    std::vector<ProvinceId> best_goal_path;

    while (!open_set.empty()) {
        const auto current = open_set.top();
        open_set.pop();
        ++explored;

        if (current.g_cost > g_score[current.portal_id]) continue;

        const auto& p_curr = graph_.portals()[current.portal_id];

        // If this portal's remote province is in goal state, test direct path to goal
        if (p_curr.remote_state == goal_state) {
            const auto to_goal = find_path_low_level(p_curr.remote_province, goal, impassable_mask, goal_state);
            if (to_goal.success) {
                const auto final_cost = current.g_cost + p_curr.transition_cost_q8 + to_goal.total_cost_q8;
                if (final_cost < best_total_cost) {
                    best_total_cost = final_cost;
                    best_goal_portal = current.portal_id;
                    best_goal_path = to_goal.path;
                }
            }
        }

        // Expand across border: transition to portal's remote province
        const auto cost_after_transition = current.g_cost + p_curr.transition_cost_q8;
        const auto remote_portals = graph_.state_portals(p_curr.remote_state);

        for (const auto next_pid : remote_portals) {
            if (next_pid == current.portal_id) continue;
            const auto& p_next = graph_.portals()[next_pid];

            // Path inside remote state from p_curr.remote_province to p_next.local_province
            const auto intra = find_path_low_level(p_curr.remote_province, p_next.local_province,
                                                   impassable_mask, p_curr.remote_state);
            if (!intra.success) continue;

            const auto tentative_g = cost_after_transition + intra.total_cost_q8;
            if (tentative_g < g_score[next_pid]) {
                g_score[next_pid] = tentative_g;
                came_from_portal[next_pid] = current.portal_id;
                intra_step_path[next_pid] = intra.path;
                const auto h = heuristic_distance(p_next.local_province, goal, graph_);
                open_set.push({next_pid, tentative_g, tentative_g + h});
            }
        }
    }

    if (best_goal_portal == 0xffffffffu) {
        // Fallback to low level if HPA* failed to find connection
        return find_path_low_level(start, goal, impassable_mask);
    }

    // Reconstruct full path
    std::vector<std::uint32_t> portal_chain;
    auto curr_pid = best_goal_portal;
    while (curr_pid != 0xffffffffu) {
        portal_chain.push_back(curr_pid);
        curr_pid = came_from_portal[curr_pid];
    }
    std::reverse(portal_chain.begin(), portal_chain.end());

    result.success = true;
    result.total_cost_q8 = best_total_cost;
    result.nodes_explored = explored;

    // Stitch segments together seamlessly
    for (std::size_t i = 0; i < portal_chain.size(); ++i) {
        const auto pid = portal_chain[i];
        const auto& seg = intra_step_path[pid];
        for (const auto p : seg) {
            if (result.path.empty() || result.path.back() != p) {
                result.path.push_back(p);
            }
        }
        const auto& portal = graph_.portals()[pid];
        if (result.path.empty() || result.path.back() != portal.remote_province) {
            result.path.push_back(portal.remote_province);
        }
    }
    for (const auto p : best_goal_path) {
        if (result.path.empty() || result.path.back() != p) {
            result.path.push_back(p);
        }
    }

    return result;
}

SupplyNetworkResult HierarchicalPathfinder::compute_supply_network(
    std::span<const SupplySource> sources,
    std::uint16_t impassable_mask) const {
    SupplyNetworkResult result;
    const auto* adj = graph_.adjacency();
    if (!adj) return result;

    const auto p_count = adj->province_count();
    result.supply_level.assign(p_count, 0u);
    result.distance_q8.assign(p_count, std::numeric_limits<std::uint32_t>::max());
    result.primary_source.assign(p_count, ProvinceId{});
    result.is_supplied.assign(p_count, false);

    struct SupplyNode {
        std::uint32_t province = 0;
        std::uint32_t dist_q8 = 0;
        std::uint32_t source_idx = 0;

        bool operator>(const SupplyNode& other) const noexcept {
            return dist_q8 > other.dist_q8;
        }
    };

    std::priority_queue<SupplyNode, std::vector<SupplyNode>, std::greater<SupplyNode>> pq;

    for (std::size_t s_idx = 0; s_idx < sources.size(); ++s_idx) {
        const auto& src = sources[s_idx];
        if (!src.province.valid() || src.province.value() >= p_count || src.max_range_q8 == 0) continue;
        const auto pi = src.province.value();
        result.distance_q8[pi] = 0;
        result.primary_source[pi] = src.province;
        result.is_supplied[pi] = true;
        result.supply_level[pi] = src.capacity;
        pq.push({pi, 0, static_cast<std::uint32_t>(s_idx)});
    }

    while (!pq.empty()) {
        const auto curr = pq.top();
        pq.pop();

        if (curr.dist_q8 > result.distance_q8[curr.province]) continue;

        const auto& src = sources[curr.source_idx];
        const auto max_range = std::max<std::uint32_t>(1u, src.max_range_q8);

        for (const auto& neighbor : adj->neighbors(ProvinceId{curr.province})) {
            if ((neighbor.flags & impassable_mask) != 0) continue;
            const auto np = neighbor.province;
            if (np >= p_count) continue;

            const auto new_dist = curr.dist_q8 + neighbor.base_cost_q8;
            if (new_dist <= src.max_range_q8 && new_dist < result.distance_q8[np]) {
                result.distance_q8[np] = new_dist;
                result.primary_source[np] = src.province;
                result.is_supplied[np] = true;

                // Supply level falls linearly with distance
                const double fraction_remaining = 1.0 - (static_cast<double>(new_dist) / static_cast<double>(max_range));
                const auto supply_here = static_cast<std::uint32_t>(std::max(0.0, src.capacity * fraction_remaining));
                result.supply_level[np] = std::max(result.supply_level[np], supply_here);

                pq.push({np, new_dist, curr.source_idx});
            }
        }
    }

    std::size_t supplied_count = 0;
    for (const auto sup : result.is_supplied) {
        if (sup) ++supplied_count;
    }
    result.supplied_province_count = supplied_count;

    return result;
}

} // namespace thunder
