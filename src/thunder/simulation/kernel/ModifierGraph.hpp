#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include <functional>
#include <string>
#include <vector>

namespace thunder {

class ModifierGraph {
public:
    using Calculator = std::function<double(const ModifierGraph&)>;

    ModifierNodeId add_source(std::string name, double initial);
    ModifierNodeId add_derived(std::string name, std::vector<ModifierNodeId> dependencies, Calculator calculator);

    void set_source(ModifierNodeId id, double value);
    void recompute_all_dirty();
    [[nodiscard]] double value(ModifierNodeId id) const;
    [[nodiscard]] bool dirty(ModifierNodeId id) const;
    [[nodiscard]] std::string_view name(ModifierNodeId id) const;

private:
    struct Node {
        std::string name;
        double value = 0.0;
        bool source = true;
        mutable bool dirty = false;
        std::vector<ModifierNodeId> dependencies;
        std::vector<ModifierNodeId> dependents;
        Calculator calculator;
    };

    [[nodiscard]] std::size_t idx(ModifierNodeId id) const;
    void mark_dependents_dirty(ModifierNodeId id);
    [[nodiscard]] double evaluate(ModifierNodeId id) const;

    mutable std::vector<Node> nodes_;
    std::vector<ModifierNodeId> dirty_stack_;
};

} // namespace thunder
