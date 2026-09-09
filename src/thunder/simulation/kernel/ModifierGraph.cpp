#include "thunder/simulation/kernel/ModifierGraph.hpp"
#include <stdexcept>

namespace thunder {

std::size_t ModifierGraph::idx(ModifierNodeId id) const {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= nodes_.size()) throw std::out_of_range("invalid ModifierNodeId");
    return i;
}

ModifierNodeId ModifierGraph::add_source(std::string name, double initial) {
    const auto id = ModifierNodeId{static_cast<ModifierNodeId::rep_type>(nodes_.size())};
    Node node;
    node.name = std::move(name);
    node.value = initial;
    node.source = true;
    nodes_.push_back(std::move(node));
    return id;
}

ModifierNodeId ModifierGraph::add_derived(std::string name, std::vector<ModifierNodeId> dependencies, Calculator calculator) {
    if (!calculator) throw std::invalid_argument("derived modifier requires calculator");
    const auto id = ModifierNodeId{static_cast<ModifierNodeId::rep_type>(nodes_.size())};
    for (const auto dep : dependencies) (void)idx(dep); // dependencies must already exist => graph stays acyclic

    Node node;
    node.name = std::move(name);
    node.source = false;
    node.dirty = true;
    node.dependencies = std::move(dependencies);
    node.calculator = std::move(calculator);
    nodes_.push_back(std::move(node));
    for (const auto dep : nodes_[idx(id)].dependencies) nodes_[idx(dep)].dependents.push_back(id);
    return id;
}

void ModifierGraph::mark_dependents_dirty(ModifierNodeId id) {
    dirty_stack_.clear();
    for (const auto dep : nodes_[idx(id)].dependents) dirty_stack_.push_back(dep);
    while (!dirty_stack_.empty()) {
        const auto current = dirty_stack_.back();
        dirty_stack_.pop_back();
        auto& node = nodes_[idx(current)];
        if (node.dirty) continue;
        node.dirty = true;
        for (const auto dependent : node.dependents) dirty_stack_.push_back(dependent);
    }
}

void ModifierGraph::set_source(ModifierNodeId id, double value) {
    auto& node = nodes_[idx(id)];
    if (!node.source) throw std::logic_error("cannot set derived modifier node");
    if (node.value == value) return;
    node.value = value;
    mark_dependents_dirty(id);
}

double ModifierGraph::evaluate(ModifierNodeId id) const {
    const auto target = idx(id);
    auto& target_node = nodes_[target];
    if (target_node.source || !target_node.dirty) return target_node.value;

    // Dependencies may only reference nodes created earlier, so numeric ID is
    // a topological order. Iterating to the requested node avoids recursive
    // evaluation and remains O(target): calculator calls to value(dep) return
    // immediately because each dependency has already been made clean.
    for (std::size_t i = 0; i <= target; ++i) {
        auto& node = nodes_[i];
        if (!node.source && node.dirty) {
            node.value = node.calculator(*this);
            node.dirty = false;
        }
    }
    return target_node.value;
}

void ModifierGraph::recompute_all_dirty() {
    // add_derived only accepts existing dependencies; numeric ID order is therefore
    // a valid topological order and batch recomputation is a linear scan.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];
        if (!node.source && node.dirty) {
            node.value = node.calculator(*this);
            node.dirty = false;
        }
    }
}

double ModifierGraph::value(ModifierNodeId id) const { return evaluate(id); }
bool ModifierGraph::dirty(ModifierNodeId id) const { return nodes_[idx(id)].dirty; }
std::string_view ModifierGraph::name(ModifierNodeId id) const { return nodes_[idx(id)].name; }

} // namespace thunder
