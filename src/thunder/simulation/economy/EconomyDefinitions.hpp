#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thunder {

// Consumer-goods classification authored in script (`category = staple` etc.).
// Stored as a byte so definitions stay trivially copyable and checksum-stable.
// `Currency` covers monetary metals (gold/silver bullion) and coinage: their
// market price IS the monetary-metal price that anchors CurrencyStore parities.
enum class GoodCategory : std::uint8_t {
    Staple = 0,
    Luxury = 1,
    Military = 2,
    Industrial = 3,
    Currency = 4,
    Count = 5,
};

[[nodiscard]] inline std::string_view good_category_name(GoodCategory category) noexcept {
    switch (category) {
        case GoodCategory::Staple: return "staple";
        case GoodCategory::Luxury: return "luxury";
        case GoodCategory::Military: return "military";
        case GoodCategory::Industrial: return "industrial";
        case GoodCategory::Currency: return "currency";
        default: return "staple";
    }
}

struct GoodDefinition {
    std::string key;
    EconomyPrice base_price_milli = 1000;
    GoodCategory category = GoodCategory::Staple;
    // Non-zero only for monetary metals (category == Currency): binds this
    // good's market price to a metal numeraire (metal_gold_key /
    // metal_silver_key) so bullion supply/demand drives the FX anchor.
    CurrencyKey monetary_metal_key = 0;
};

struct RecipeFlow {
    GoodId good;
    // milli-units of goods per 1000 employed workers each weekly tick.
    EconomyAmount quantity_milli_per_1000_workers = 0;
};

struct BuildingTypeDefinition {
    std::string key;
    std::uint32_t workers_per_level = 1000;
    std::uint32_t input_begin = 0;
    std::uint16_t input_count = 0;
    std::uint32_t output_begin = 0;
    std::uint16_t output_count = 0;
};

struct ProductionMethodDefinition {
    std::string key;
    BuildingTypeId building_type{};
    std::int32_t throughput_ppm = 1'000'000;
    std::uint32_t input_begin = 0;
    std::uint16_t input_count = 0;
    std::uint32_t output_begin = 0;
    std::uint16_t output_count = 0;
};

struct NeedFlow {
    GoodId good;
    // milli-units per 1000 people each weekly tick.
    EconomyAmount quantity_milli_per_1000_people = 0;
};

struct NeedProfileDefinition {
    std::string key;
    std::uint32_t flow_begin = 0;
    std::uint16_t flow_count = 0;
};

class EconomyDefinitions {
public:
    GoodId add_good(GoodDefinition definition);
    BuildingTypeId add_building_type(std::string key, std::uint32_t workers_per_level,
                                     std::span<const RecipeFlow> inputs,
                                     std::span<const RecipeFlow> outputs);
    ProductionMethodId add_production_method(std::string key, BuildingTypeId building_type,
                                                   std::int32_t throughput_ppm,
                                                   std::span<const RecipeFlow> inputs,
                                                   std::span<const RecipeFlow> outputs);
    NeedProfileId add_need_profile(std::string key, std::span<const NeedFlow> flows);

    [[nodiscard]] std::size_t good_count() const noexcept { return goods_.size(); }
    [[nodiscard]] std::size_t building_type_count() const noexcept { return building_types_.size(); }
    [[nodiscard]] std::size_t production_method_count() const noexcept { return production_methods_.size(); }
    [[nodiscard]] std::size_t need_profile_count() const noexcept { return need_profiles_.size(); }
    [[nodiscard]] const GoodDefinition& good(GoodId id) const;
    [[nodiscard]] const BuildingTypeDefinition& building_type(BuildingTypeId id) const;
    [[nodiscard]] const ProductionMethodDefinition& production_method(ProductionMethodId id) const;
    [[nodiscard]] std::span<const RecipeFlow> inputs(ProductionMethodId id) const;
    [[nodiscard]] std::span<const RecipeFlow> outputs(ProductionMethodId id) const;
    [[nodiscard]] const NeedProfileDefinition& need_profile(NeedProfileId id) const;
    [[nodiscard]] std::span<const RecipeFlow> inputs(BuildingTypeId id) const;
    [[nodiscard]] std::span<const RecipeFlow> outputs(BuildingTypeId id) const;
    [[nodiscard]] std::span<const NeedFlow> needs(NeedProfileId id) const;
    // First good bound to the given metal numeraire (invalid when none is
    // authored). Definitions are immutable after binding, so the result is
    // stable across ticks.
    [[nodiscard]] GoodId find_good_by_monetary_metal(CurrencyKey metal_key) const noexcept;

    // Construction goods basket: milli-units consumed per construction point.
    // Authored via content (`construction_goods` block); an empty basket keeps
    // the money-only legacy construction model.
    void set_construction_inputs(std::span<const RecipeFlow> flows);
    [[nodiscard]] std::span<const RecipeFlow> construction_inputs() const noexcept { return construction_inputs_; }
    [[nodiscard]] std::span<const BuildingTypeDefinition> building_types() const noexcept { return building_types_; }
    [[nodiscard]] std::span<const RecipeFlow> input_flows() const noexcept { return recipe_inputs_; }
    [[nodiscard]] std::span<const RecipeFlow> output_flows() const noexcept { return recipe_outputs_; }
    [[nodiscard]] std::span<const ProductionMethodDefinition> production_methods() const noexcept { return production_methods_; }
    [[nodiscard]] std::span<const NeedProfileDefinition> need_profiles() const noexcept { return need_profiles_; }
    [[nodiscard]] std::span<const NeedFlow> need_flows() const noexcept { return need_flows_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    std::vector<GoodDefinition> goods_;
    std::vector<BuildingTypeDefinition> building_types_;
    std::vector<RecipeFlow> recipe_inputs_;
    std::vector<RecipeFlow> recipe_outputs_;
    std::vector<ProductionMethodDefinition> production_methods_;
    std::vector<NeedProfileDefinition> need_profiles_;
    std::vector<NeedFlow> need_flows_;
    std::vector<RecipeFlow> construction_inputs_;
};

} // namespace thunder
