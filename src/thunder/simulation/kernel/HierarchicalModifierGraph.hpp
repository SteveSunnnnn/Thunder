#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace thunder {

enum class ModifierScopeLevel : std::uint8_t {
    Global = 0,
    Country = 1,
    State = 2,
    Province = 3,
    Building = 4,
    Count = 5
};

enum class ModifierOp : std::uint8_t {
    Add = 0,            // Flat addition
    Multiply = 1,       // Percentage/Multiplier: 0.1 = +10%, compounded as (1 + sum(m)) or prod(1 + m)
    Min = 2,            // Minimum lower bound
    Max = 3,            // Maximum upper bound
    BoolFlag = 4        // Logical OR flag
};

struct ModifierEntry {
    std::uint64_t key_hash = 0;
    double value = 0.0;
    ModifierOp op = ModifierOp::Add;
    std::uint32_t source_id = 0; // Optional identifier for debugging/removal
};

struct ModifierChain {
    CountryId country{};
    StateId state{};
    ProvinceId province{};
    BuildingId building{};
};

// Summary of all modifier contributions for a specific key
struct ModifierBreakdown {
    double base = 0.0;
    double flat_add = 0.0;
    double percent_mult = 0.0; // sum of (mult)
    double min_bound = -std::numeric_limits<double>::infinity();
    double max_bound = std::numeric_limits<double>::infinity();
    bool bool_flag = false;
    double final_value = 0.0;
};

// Hierarchical Modifier Graph
// Provides Global -> Country -> State -> Province -> Building hierarchical
// modifier accumulation with O(1) dirty modification via revision counters.
class HierarchicalModifierGraph {
public:
    HierarchicalModifierGraph();
    ~HierarchicalModifierGraph() = default;

    void reserve(ModifierScopeLevel level, std::size_t count);

    // Parent hierarchy linkage
    void set_building_parent(BuildingId building, ProvinceId province);
    void set_province_parent(ProvinceId province, StateId state);
    void set_state_parent(StateId state, CountryId country);

    [[nodiscard]] ProvinceId building_parent(BuildingId building) const noexcept;
    [[nodiscard]] StateId province_parent(ProvinceId province) const noexcept;
    [[nodiscard]] CountryId state_parent(StateId state) const noexcept;

    // Direct modifier entry management
    void add_modifier(ModifierScopeLevel level, std::uint32_t scope_id,
                      std::string_view key, ModifierOp op, double value,
                      std::uint32_t source_id = 0);
    void add_modifier(ModifierScopeLevel level, std::uint32_t scope_id,
                      std::uint64_t key_hash, ModifierOp op, double value,
                      std::uint32_t source_id = 0);

    bool remove_modifier_by_source(ModifierScopeLevel level, std::uint32_t scope_id,
                                   std::string_view key, std::uint32_t source_id);
    bool remove_modifier_by_source(ModifierScopeLevel level, std::uint32_t scope_id,
                                   std::uint64_t key_hash, std::uint32_t source_id);
    void clear_modifiers(ModifierScopeLevel level, std::uint32_t scope_id);
    void clear_all();

    // Revisions
    [[nodiscard]] std::uint64_t revision(ModifierScopeLevel level, std::uint32_t scope_id) const noexcept;
    [[nodiscard]] std::uint64_t global_revision() const noexcept { return global_revision_; }

    // Evaluation APIs
    [[nodiscard]] double evaluate(ModifierScopeLevel level, std::uint32_t scope_id,
                                  std::string_view key, double base_value = 0.0) const;
    [[nodiscard]] double evaluate(ModifierScopeLevel level, std::uint32_t scope_id,
                                  std::uint64_t key_hash, double base_value = 0.0) const;

    [[nodiscard]] double evaluate_chain(const ModifierChain& chain,
                                        std::string_view key, double base_value = 0.0) const;
    [[nodiscard]] double evaluate_chain(const ModifierChain& chain,
                                        std::uint64_t key_hash, double base_value = 0.0) const;

    [[nodiscard]] std::int64_t evaluate_ppm(ModifierScopeLevel level, std::uint32_t scope_id,
                                            std::string_view key, std::int64_t base_ppm) const;
    [[nodiscard]] std::int64_t evaluate_ppm(ModifierScopeLevel level, std::uint32_t scope_id,
                                            std::uint64_t key_hash, std::int64_t base_ppm) const;

    [[nodiscard]] bool evaluate_bool(ModifierScopeLevel level, std::uint32_t scope_id,
                                     std::string_view key) const;
    [[nodiscard]] bool evaluate_bool(ModifierScopeLevel level, std::uint32_t scope_id,
                                     std::uint64_t key_hash) const;

    [[nodiscard]] ModifierBreakdown breakdown(ModifierScopeLevel level, std::uint32_t scope_id,
                                              std::string_view key, double base_value = 0.0) const;
    [[nodiscard]] ModifierBreakdown breakdown(ModifierScopeLevel level, std::uint32_t scope_id,
                                              std::uint64_t key_hash, double base_value = 0.0) const;

    [[nodiscard]] std::size_t modifier_count(ModifierScopeLevel level, std::uint32_t scope_id) const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    struct ScopeNode {
        std::uint64_t revision = 1;
        std::uint32_t parent_id = 0xffffffffu;
        std::vector<ModifierEntry> entries;
    };

    struct LevelStore {
        std::vector<ScopeNode> nodes;
        /// Grow to cover `id`. Returns false when the id exceeds the hard cap.
        /// Callers MUST bail out on false: indexing `nodes[id]` anyway writes
        /// past the end of the vector.
        [[nodiscard]] bool ensure(std::size_t id) {
            if (id > 10'000'000u) return false;
            if (id >= nodes.size()) {
                nodes.resize(id + 1u);
            }
            return true;
        }
    };

    struct ScopeChainNode {
        ModifierScopeLevel level = ModifierScopeLevel::Global;
        std::uint32_t id = 0;
    };
    struct ScopeChain {
        std::array<ScopeChainNode, 5> nodes{};
        std::uint8_t count = 0;
    };

    [[nodiscard]] std::uint32_t get_parent(ModifierScopeLevel level, std::uint32_t scope_id) const noexcept;
    void collect_chain(ModifierScopeLevel level, std::uint32_t scope_id, ScopeChain& out_chain) const noexcept;

    LevelStore levels_[static_cast<std::size_t>(ModifierScopeLevel::Count)];
    std::uint64_t global_revision_ = 1;
};

} // namespace thunder
