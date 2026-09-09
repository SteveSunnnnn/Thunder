#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include <limits>
#include <stdexcept>
#include <string_view>

namespace thunder {
namespace {
template <typename Id>
std::size_t checked_index(Id id, std::size_t size, const char* message) {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= size) throw std::out_of_range(message);
    return index;
}

template <typename Definition>
void validate_unique_key(std::string_view key, std::span<const Definition> definitions,
                         const char* kind) {
    if (key.empty()) throw std::invalid_argument(std::string{kind} + " key must not be empty");
    for (const auto& definition : definitions) {
        if (definition.key == key)
            throw std::invalid_argument(std::string{"duplicate "} + kind + " key: " + std::string{key});
    }
}

template <typename Flow>
void validate_flows(std::span<const Flow> flows, std::size_t good_count, const char* kind) {
    for (const auto& flow : flows) {
        (void)checked_index(flow.good, good_count, "economic flow references invalid GoodId");
        const auto quantity = [&]() -> EconomyAmount {
            if constexpr (requires { flow.quantity_milli_per_1000_workers; })
                return flow.quantity_milli_per_1000_workers;
            else
                return flow.quantity_milli_per_1000_people;
        }();
        if (quantity <= 0)
            throw std::invalid_argument(std::string{kind} + " quantity must be positive");
    }
}

void validate_flow_capacity(std::size_t current, std::size_t added) {
    constexpr auto max_offset = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (current > max_offset || added > max_offset - current)
        throw std::length_error("economic flow table exceeds 32-bit offset capacity");
}
}

GoodId EconomyDefinitions::add_good(GoodDefinition definition) {
    validate_unique_key(definition.key, std::span<const GoodDefinition>{goods_}, "good");
    if (definition.base_price_milli <= 0) throw std::invalid_argument("good base price must be positive");
    if (definition.monetary_metal_key != 0 && definition.category != GoodCategory::Currency)
        throw std::invalid_argument("monetary metal good must have category currency");
    if (goods_.size() >= static_cast<std::size_t>(std::numeric_limits<GoodId::rep_type>::max()))
        throw std::length_error("too many goods");
    const auto raw = static_cast<GoodId::rep_type>(goods_.size());
    goods_.push_back(std::move(definition));
    return GoodId{raw};
}

GoodId EconomyDefinitions::find_good_by_monetary_metal(CurrencyKey metal_key) const noexcept {
    if (metal_key == 0) return GoodId{};
    for (std::size_t i = 0; i < goods_.size(); ++i) {
        if (goods_[i].monetary_metal_key == metal_key)
            return GoodId{static_cast<GoodId::rep_type>(i)};
    }
    return GoodId{};
}

void EconomyDefinitions::set_construction_inputs(std::span<const RecipeFlow> flows) {
    validate_flows(flows, goods_.size(), "construction input");
    validate_flow_capacity(construction_inputs_.size(), flows.size());
    construction_inputs_.assign(flows.begin(), flows.end());
}

BuildingTypeId EconomyDefinitions::add_building_type(std::string key, std::uint32_t workers_per_level,
                                                       std::span<const RecipeFlow> inputs,
                                                       std::span<const RecipeFlow> outputs) {
    if (workers_per_level == 0u) throw std::invalid_argument("building workers_per_level must be non-zero");
    validate_unique_key(key, std::span<const BuildingTypeDefinition>{building_types_}, "building type");
    if (inputs.size() > 65535u || outputs.size() > 65535u) throw std::invalid_argument("building recipe too large");
    validate_flows(inputs, goods_.size(), "building input");
    validate_flows(outputs, goods_.size(), "building output");
    validate_flow_capacity(recipe_inputs_.size(), inputs.size());
    validate_flow_capacity(recipe_outputs_.size(), outputs.size());
    if (building_types_.size() >= static_cast<std::size_t>(std::numeric_limits<BuildingTypeId::rep_type>::max()))
        throw std::length_error("too many building types");
    const auto raw = static_cast<BuildingTypeId::rep_type>(building_types_.size());
    BuildingTypeDefinition definition;
    definition.key = std::move(key);
    definition.workers_per_level = workers_per_level;
    definition.input_begin = static_cast<std::uint32_t>(recipe_inputs_.size());
    definition.input_count = static_cast<std::uint16_t>(inputs.size());
    definition.output_begin = static_cast<std::uint32_t>(recipe_outputs_.size());
    definition.output_count = static_cast<std::uint16_t>(outputs.size());
    recipe_inputs_.insert(recipe_inputs_.end(), inputs.begin(), inputs.end());
    recipe_outputs_.insert(recipe_outputs_.end(), outputs.begin(), outputs.end());
    building_types_.push_back(std::move(definition));
    return BuildingTypeId{raw};
}


ProductionMethodId EconomyDefinitions::add_production_method(
    std::string key, BuildingTypeId building_type, std::int32_t throughput_ppm,
    std::span<const RecipeFlow> inputs, std::span<const RecipeFlow> outputs) {
    (void)checked_index(building_type, building_types_.size(), "invalid BuildingTypeId for production method");
    validate_unique_key(key, std::span<const ProductionMethodDefinition>{production_methods_}, "production method");
    if (throughput_ppm <= 0 || throughput_ppm > 4'000'000)
        throw std::invalid_argument("production method throughput_ppm out of range");
    if (inputs.size() > 65535u || outputs.size() > 65535u)
        throw std::invalid_argument("production method recipe too large");
    validate_flows(inputs, goods_.size(), "production-method input");
    validate_flows(outputs, goods_.size(), "production-method output");
    validate_flow_capacity(recipe_inputs_.size(), inputs.size());
    validate_flow_capacity(recipe_outputs_.size(), outputs.size());
    if (production_methods_.size() >= static_cast<std::size_t>(std::numeric_limits<ProductionMethodId::rep_type>::max()))
        throw std::length_error("too many production methods");
    const auto raw = static_cast<ProductionMethodId::rep_type>(production_methods_.size());
    ProductionMethodDefinition definition;
    definition.key = std::move(key);
    definition.building_type = building_type;
    definition.throughput_ppm = throughput_ppm;
    definition.input_begin = static_cast<std::uint32_t>(recipe_inputs_.size());
    definition.input_count = static_cast<std::uint16_t>(inputs.size());
    definition.output_begin = static_cast<std::uint32_t>(recipe_outputs_.size());
    definition.output_count = static_cast<std::uint16_t>(outputs.size());
    recipe_inputs_.insert(recipe_inputs_.end(), inputs.begin(), inputs.end());
    recipe_outputs_.insert(recipe_outputs_.end(), outputs.begin(), outputs.end());
    production_methods_.push_back(std::move(definition));
    return ProductionMethodId{raw};
}

NeedProfileId EconomyDefinitions::add_need_profile(std::string key, std::span<const NeedFlow> flows) {
    validate_unique_key(key, std::span<const NeedProfileDefinition>{need_profiles_}, "need profile");
    if (flows.size() > 65535u) throw std::invalid_argument("need profile too large");
    validate_flows(flows, goods_.size(), "need");
    validate_flow_capacity(need_flows_.size(), flows.size());
    if (need_profiles_.size() >= static_cast<std::size_t>(std::numeric_limits<NeedProfileId::rep_type>::max()))
        throw std::length_error("too many need profiles");
    const auto raw = static_cast<NeedProfileId::rep_type>(need_profiles_.size());
    NeedProfileDefinition definition;
    definition.key = std::move(key);
    definition.flow_begin = static_cast<std::uint32_t>(need_flows_.size());
    definition.flow_count = static_cast<std::uint16_t>(flows.size());
    need_flows_.insert(need_flows_.end(), flows.begin(), flows.end());
    need_profiles_.push_back(std::move(definition));
    return NeedProfileId{raw};
}

const GoodDefinition& EconomyDefinitions::good(GoodId id) const {
    return goods_[checked_index(id, goods_.size(), "invalid GoodId")];
}
const BuildingTypeDefinition& EconomyDefinitions::building_type(BuildingTypeId id) const {
    return building_types_[checked_index(id, building_types_.size(), "invalid BuildingTypeId")];
}
const ProductionMethodDefinition& EconomyDefinitions::production_method(ProductionMethodId id) const {
    return production_methods_[checked_index(id, production_methods_.size(), "invalid ProductionMethodId")];
}
std::span<const RecipeFlow> EconomyDefinitions::inputs(ProductionMethodId id) const {
    const auto& d = production_method(id);
    return std::span<const RecipeFlow>{recipe_inputs_}.subspan(d.input_begin, d.input_count);
}
std::span<const RecipeFlow> EconomyDefinitions::outputs(ProductionMethodId id) const {
    const auto& d = production_method(id);
    return std::span<const RecipeFlow>{recipe_outputs_}.subspan(d.output_begin, d.output_count);
}
const NeedProfileDefinition& EconomyDefinitions::need_profile(NeedProfileId id) const {
    return need_profiles_[checked_index(id, need_profiles_.size(), "invalid NeedProfileId")];
}
std::span<const RecipeFlow> EconomyDefinitions::inputs(BuildingTypeId id) const {
    const auto& d = building_type(id);
    return std::span<const RecipeFlow>{recipe_inputs_}.subspan(d.input_begin, d.input_count);
}
std::span<const RecipeFlow> EconomyDefinitions::outputs(BuildingTypeId id) const {
    const auto& d = building_type(id);
    return std::span<const RecipeFlow>{recipe_outputs_}.subspan(d.output_begin, d.output_count);
}
std::span<const NeedFlow> EconomyDefinitions::needs(NeedProfileId id) const {
    const auto& d = need_profile(id);
    return std::span<const NeedFlow>{need_flows_}.subspan(d.flow_begin, d.flow_count);
}
std::size_t EconomyDefinitions::memory_bytes() const noexcept {
    return goods_.capacity() * sizeof(GoodDefinition)
        + building_types_.capacity() * sizeof(BuildingTypeDefinition)
        + recipe_inputs_.capacity() * sizeof(RecipeFlow)
        + recipe_outputs_.capacity() * sizeof(RecipeFlow)
        + production_methods_.capacity() * sizeof(ProductionMethodDefinition)
        + need_profiles_.capacity() * sizeof(NeedProfileDefinition)
        + need_flows_.capacity() * sizeof(NeedFlow)
        + construction_inputs_.capacity() * sizeof(RecipeFlow);
}

} // namespace thunder
