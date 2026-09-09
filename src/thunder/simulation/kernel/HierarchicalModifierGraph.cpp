#include "thunder/simulation/kernel/HierarchicalModifierGraph.hpp"

#include <algorithm>
#include <cmath>

namespace thunder {

namespace {

[[nodiscard]] std::uint64_t hash_key(std::string_view key) noexcept {
    Fnv1a64 h;
    h.add(key);
    return h.value();
}

} // namespace

HierarchicalModifierGraph::HierarchicalModifierGraph() {
    // Global level always has at least 1 node (id 0)
    (void)levels_[static_cast<std::size_t>(ModifierScopeLevel::Global)].ensure(0);
}

void HierarchicalModifierGraph::reserve(ModifierScopeLevel level, std::size_t count) {
    const auto idx = static_cast<std::size_t>(level);
    if (idx < static_cast<std::size_t>(ModifierScopeLevel::Count)) {
        levels_[idx].nodes.reserve(count);
    }
}

void HierarchicalModifierGraph::set_building_parent(BuildingId building, ProvinceId province) {
    if (!building.valid()) return;
    const auto bid = static_cast<std::size_t>(building.value());
    auto& store = levels_[static_cast<std::size_t>(ModifierScopeLevel::Building)];
    if (!store.ensure(bid)) return;
    store.nodes[bid].parent_id = province.valid() ? province.value() : 0xffffffffu;
    ++store.nodes[bid].revision;
}

void HierarchicalModifierGraph::set_province_parent(ProvinceId province, StateId state) {
    if (!province.valid()) return;
    const auto pid = static_cast<std::size_t>(province.value());
    auto& store = levels_[static_cast<std::size_t>(ModifierScopeLevel::Province)];
    if (!store.ensure(pid)) return;
    store.nodes[pid].parent_id = state.valid() ? state.value() : 0xffffffffu;
    ++store.nodes[pid].revision;
}

void HierarchicalModifierGraph::set_state_parent(StateId state, CountryId country) {
    if (!state.valid()) return;
    const auto sid = static_cast<std::size_t>(state.value());
    auto& store = levels_[static_cast<std::size_t>(ModifierScopeLevel::State)];
    if (!store.ensure(sid)) return;
    store.nodes[sid].parent_id = country.valid() ? country.value() : 0xffffffffu;
    ++store.nodes[sid].revision;
}

ProvinceId HierarchicalModifierGraph::building_parent(BuildingId building) const noexcept {
    if (!building.valid()) return {};
    const auto bid = static_cast<std::size_t>(building.value());
    const auto& store = levels_[static_cast<std::size_t>(ModifierScopeLevel::Building)];
    if (bid >= store.nodes.size() || store.nodes[bid].parent_id == 0xffffffffu) return {};
    return ProvinceId{store.nodes[bid].parent_id};
}

StateId HierarchicalModifierGraph::province_parent(ProvinceId province) const noexcept {
    if (!province.valid()) return {};
    const auto pid = static_cast<std::size_t>(province.value());
    const auto& store = levels_[static_cast<std::size_t>(ModifierScopeLevel::Province)];
    if (pid >= store.nodes.size() || store.nodes[pid].parent_id == 0xffffffffu) return {};
    return StateId{store.nodes[pid].parent_id};
}

CountryId HierarchicalModifierGraph::state_parent(StateId state) const noexcept {
    if (!state.valid()) return {};
    const auto sid = static_cast<std::size_t>(state.value());
    const auto& store = levels_[static_cast<std::size_t>(ModifierScopeLevel::State)];
    if (sid >= store.nodes.size() || store.nodes[sid].parent_id == 0xffffffffu) return {};
    return CountryId{store.nodes[sid].parent_id};
}

std::uint32_t HierarchicalModifierGraph::get_parent(ModifierScopeLevel level, std::uint32_t scope_id) const noexcept {
    const auto idx = static_cast<std::size_t>(level);
    if (idx >= static_cast<std::size_t>(ModifierScopeLevel::Count) || idx == 0) return 0xffffffffu;
    const auto& store = levels_[idx];
    if (scope_id >= store.nodes.size()) return 0xffffffffu;
    return store.nodes[scope_id].parent_id;
}

void HierarchicalModifierGraph::add_modifier(ModifierScopeLevel level, std::uint32_t scope_id,
                                             std::string_view key, ModifierOp op, double value,
                                             std::uint32_t source_id) {
    add_modifier(level, scope_id, hash_key(key), op, value, source_id);
}

void HierarchicalModifierGraph::add_modifier(ModifierScopeLevel level, std::uint32_t scope_id,
                                             std::uint64_t key_hash, ModifierOp op, double value,
                                             std::uint32_t source_id) {
    const auto idx = static_cast<std::size_t>(level);
    if (idx >= static_cast<std::size_t>(ModifierScopeLevel::Count)) return;
    auto& store = levels_[idx];
    if (!store.ensure(scope_id)) return;
    store.nodes[scope_id].entries.push_back({key_hash, value, op, source_id});
    ++store.nodes[scope_id].revision;
    if (level == ModifierScopeLevel::Global) {
        ++global_revision_;
    }
}

bool HierarchicalModifierGraph::remove_modifier_by_source(ModifierScopeLevel level, std::uint32_t scope_id,
                                                          std::string_view key, std::uint32_t source_id) {
    return remove_modifier_by_source(level, scope_id, hash_key(key), source_id);
}

bool HierarchicalModifierGraph::remove_modifier_by_source(ModifierScopeLevel level, std::uint32_t scope_id,
                                                          std::uint64_t key_hash, std::uint32_t source_id) {
    const auto idx = static_cast<std::size_t>(level);
    if (idx >= static_cast<std::size_t>(ModifierScopeLevel::Count)) return false;
    auto& store = levels_[idx];
    if (scope_id >= store.nodes.size()) return false;
    auto& entries = store.nodes[scope_id].entries;
    const auto it = std::remove_if(entries.begin(), entries.end(), [=](const ModifierEntry& e) {
        return e.key_hash == key_hash && e.source_id == source_id;
    });
    if (it != entries.end()) {
        entries.erase(it, entries.end());
        ++store.nodes[scope_id].revision;
        if (level == ModifierScopeLevel::Global) ++global_revision_;
        return true;
    }
    return false;
}

void HierarchicalModifierGraph::clear_modifiers(ModifierScopeLevel level, std::uint32_t scope_id) {
    const auto idx = static_cast<std::size_t>(level);
    if (idx >= static_cast<std::size_t>(ModifierScopeLevel::Count)) return;
    auto& store = levels_[idx];
    if (scope_id < store.nodes.size()) {
        if (!store.nodes[scope_id].entries.empty()) {
            store.nodes[scope_id].entries.clear();
            ++store.nodes[scope_id].revision;
            if (level == ModifierScopeLevel::Global) ++global_revision_;
        }
    }
}

void HierarchicalModifierGraph::clear_all() {
    for (auto& store : levels_) {
        for (auto& node : store.nodes) {
            node.entries.clear();
            ++node.revision;
        }
    }
    ++global_revision_;
}

std::uint64_t HierarchicalModifierGraph::revision(ModifierScopeLevel level, std::uint32_t scope_id) const noexcept {
    const auto idx = static_cast<std::size_t>(level);
    if (idx >= static_cast<std::size_t>(ModifierScopeLevel::Count)) return 0;
    const auto& store = levels_[idx];
    if (scope_id >= store.nodes.size()) return 0;
    return store.nodes[scope_id].revision;
}

void HierarchicalModifierGraph::collect_chain(
    ModifierScopeLevel level, std::uint32_t scope_id,
    ScopeChain& out_chain) const noexcept {
    out_chain.count = 0;
    auto cur_level = level;
    auto cur_id = scope_id;

    while (out_chain.count < 5) {
        out_chain.nodes[out_chain.count++] = {cur_level, cur_id};
        if (cur_level == ModifierScopeLevel::Global) break;

        const auto parent = get_parent(cur_level, cur_id);
        const auto parent_level_idx = static_cast<std::uint8_t>(cur_level) - 1u;
        cur_level = static_cast<ModifierScopeLevel>(parent_level_idx);
        cur_id = (cur_level == ModifierScopeLevel::Global) ? 0u : parent;
        if (cur_level != ModifierScopeLevel::Global && cur_id == 0xffffffffu) {
            // Unattached parent jumps straight to Global (0)
            if (out_chain.count < 5) {
                out_chain.nodes[out_chain.count++] = {ModifierScopeLevel::Global, 0u};
            }
            break;
        }
    }
}

ModifierBreakdown HierarchicalModifierGraph::breakdown(
    ModifierScopeLevel level, std::uint32_t scope_id,
    std::string_view key, double base_value) const {
    return breakdown(level, scope_id, hash_key(key), base_value);
}

ModifierBreakdown HierarchicalModifierGraph::breakdown(
    ModifierScopeLevel level, std::uint32_t scope_id,
    std::uint64_t key_hash, double base_value) const {
    ModifierBreakdown b;
    b.base = base_value;

    ScopeChain chain;
    collect_chain(level, scope_id, chain);

    for (std::size_t c_idx = 0; c_idx < chain.count; ++c_idx) {
        const auto lvl = chain.nodes[c_idx].level;
        const auto id = chain.nodes[c_idx].id;
        const auto l_idx = static_cast<std::size_t>(lvl);
        if (l_idx >= static_cast<std::size_t>(ModifierScopeLevel::Count)) continue;
        const auto& store = levels_[l_idx];
        if (id >= store.nodes.size()) continue;

        for (const auto& entry : store.nodes[id].entries) {
            if (entry.key_hash != key_hash) continue;
            switch (entry.op) {
                case ModifierOp::Add:
                    b.flat_add += entry.value;
                    break;
                case ModifierOp::Multiply:
                    b.percent_mult += entry.value;
                    break;
                case ModifierOp::Min:
                    b.min_bound = std::max(b.min_bound, entry.value);
                    break;
                case ModifierOp::Max:
                    b.max_bound = std::min(b.max_bound, entry.value);
                    break;
                case ModifierOp::BoolFlag:
                    if (entry.value > 0.5) b.bool_flag = true;
                    break;
            }
        }
    }

    const double subtotal = (b.base + b.flat_add) * std::max(0.0, 1.0 + b.percent_mult);
    // Min and Max accumulators are independent, so authored content can leave
    // min_bound above max_bound. std::clamp requires !(hi < lo) and is
    // undefined otherwise; resolve the conflict deterministically instead.
    b.final_value = b.min_bound <= b.max_bound
                        ? std::clamp(subtotal, b.min_bound, b.max_bound)
                        : std::max(subtotal, b.min_bound);
    return b;
}

double HierarchicalModifierGraph::evaluate(ModifierScopeLevel level, std::uint32_t scope_id,
                                           std::string_view key, double base_value) const {
    return evaluate(level, scope_id, hash_key(key), base_value);
}

double HierarchicalModifierGraph::evaluate(ModifierScopeLevel level, std::uint32_t scope_id,
                                           std::uint64_t key_hash, double base_value) const {
    return breakdown(level, scope_id, key_hash, base_value).final_value;
}

double HierarchicalModifierGraph::evaluate_chain(const ModifierChain& chain,
                                                 std::string_view key, double base_value) const {
    return evaluate_chain(chain, hash_key(key), base_value);
}

double HierarchicalModifierGraph::evaluate_chain(const ModifierChain& chain,
                                                 std::uint64_t key_hash, double base_value) const {
    if (chain.building.valid()) {
        return evaluate(ModifierScopeLevel::Building, chain.building.value(), key_hash, base_value);
    }
    if (chain.province.valid()) {
        return evaluate(ModifierScopeLevel::Province, chain.province.value(), key_hash, base_value);
    }
    if (chain.state.valid()) {
        return evaluate(ModifierScopeLevel::State, chain.state.value(), key_hash, base_value);
    }
    if (chain.country.valid()) {
        return evaluate(ModifierScopeLevel::Country, chain.country.value(), key_hash, base_value);
    }
    return evaluate(ModifierScopeLevel::Global, 0u, key_hash, base_value);
}

std::int64_t HierarchicalModifierGraph::evaluate_ppm(ModifierScopeLevel level, std::uint32_t scope_id,
                                                     std::string_view key, std::int64_t base_ppm) const {
    return evaluate_ppm(level, scope_id, hash_key(key), base_ppm);
}

std::int64_t HierarchicalModifierGraph::evaluate_ppm(ModifierScopeLevel level, std::uint32_t scope_id,
                                                     std::uint64_t key_hash, std::int64_t base_ppm) const {
    const double result = evaluate(level, scope_id, key_hash, static_cast<double>(base_ppm) / 1'000'000.0);
    return static_cast<std::int64_t>(std::round(result * 1'000'000.0));
}

bool HierarchicalModifierGraph::evaluate_bool(ModifierScopeLevel level, std::uint32_t scope_id,
                                              std::string_view key) const {
    return evaluate_bool(level, scope_id, hash_key(key));
}

bool HierarchicalModifierGraph::evaluate_bool(ModifierScopeLevel level, std::uint32_t scope_id,
                                              std::uint64_t key_hash) const {
    return breakdown(level, scope_id, key_hash, 0.0).bool_flag;
}

std::size_t HierarchicalModifierGraph::modifier_count(ModifierScopeLevel level, std::uint32_t scope_id) const noexcept {
    const auto idx = static_cast<std::size_t>(level);
    if (idx >= static_cast<std::size_t>(ModifierScopeLevel::Count)) return 0;
    const auto& store = levels_[idx];
    if (scope_id >= store.nodes.size()) return 0;
    return store.nodes[scope_id].entries.size();
}

std::size_t HierarchicalModifierGraph::memory_bytes() const noexcept {
    std::size_t total = sizeof(*this);
    for (const auto& store : levels_) {
        total += store.nodes.capacity() * sizeof(ScopeNode);
        for (const auto& node : store.nodes) {
            total += node.entries.capacity() * sizeof(ModifierEntry);
        }
    }
    return total;
}

std::uint64_t HierarchicalModifierGraph::checksum() const noexcept {
    Fnv1a64 h;
    h.add(global_revision_);
    for (std::size_t l = 0; l < static_cast<std::size_t>(ModifierScopeLevel::Count); ++l) {
        h.add(static_cast<std::uint32_t>(l));
        const auto& store = levels_[l];
        h.add(static_cast<std::uint64_t>(store.nodes.size()));
        for (std::size_t i = 0; i < store.nodes.size(); ++i) {
            const auto& node = store.nodes[i];
            h.add(node.revision);
            h.add(node.parent_id);
            h.add(static_cast<std::uint64_t>(node.entries.size()));
            for (const auto& entry : node.entries) {
                h.add(entry.key_hash);
                h.add(entry.value);
                h.add(static_cast<std::uint8_t>(entry.op));
                h.add(entry.source_id);
            }
        }
    }
    return h.value();
}

} // namespace thunder
