#include "thunder/presentation/render/map/PoliticalMapState.hpp"

#include <algorithm>

namespace thunder {

void PoliticalMapState::resize(std::uint32_t province_count, std::uint32_t country_count) {
    province_records_.resize(province_count);
    country_colors_.resize(country_count);
    map_values_.resize(province_count);
    clear_dirty();
    owner_dirty_.mark(0u, province_count);
    country_color_dirty_.mark(0u, country_count);
    map_value_dirty_.mark(0u, province_count);
}

void PoliticalMapState::clear_dirty() noexcept {
    owner_dirty_.clear();
    country_color_dirty_.clear();
    map_value_dirty_.clear();
}

bool PoliticalMapState::valid_province(ProvinceId id) const noexcept {
    return id.valid() && static_cast<std::size_t>(id.value()) < province_records_.size();
}

bool PoliticalMapState::valid_country(CountryId id) const noexcept {
    return id.valid() && static_cast<std::size_t>(id.value()) < country_colors_.size();
}

void PoliticalMapState::set_owner(ProvinceId province, CountryId owner_id) noexcept {
    if (!valid_province(province)) return;
    auto& record = province_records_[province.value()];
    const auto raw = valid_country(owner_id) ? owner_id.value() : 0xffffffffu;
    if (record.owner_country == raw) return;
    record.owner_country = raw;
    owner_dirty_.mark(province.value());
}

void PoliticalMapState::set_province_flags(ProvinceId province, std::uint32_t flags) noexcept {
    if (!valid_province(province)) return;
    auto& record = province_records_[province.value()];
    if (record.flags == flags) return;
    record.flags = flags;
    owner_dirty_.mark(province.value());
}

void PoliticalMapState::set_country_color(CountryId country, Rgba8 color) noexcept {
    if (!valid_country(country)) return;
    auto& target = country_colors_[country.value()];
    if (target == color) return;
    target = color;
    country_color_dirty_.mark(country.value());
}

void PoliticalMapState::set_map_value(ProvinceId province, float value) noexcept {
    if (!valid_province(province)) return;
    auto& target = map_values_[province.value()];
    if (target == value) return;
    target = value;
    map_value_dirty_.mark(province.value());
}

CountryId PoliticalMapState::owner(ProvinceId province) const noexcept {
    if (!valid_province(province)) return CountryId{};
    const auto raw = province_records_[province.value()].owner_country;
    return raw == 0xffffffffu ? CountryId{} : CountryId{raw};
}

Rgba8 PoliticalMapState::country_color(CountryId country) const noexcept {
    if (!valid_country(country)) return {};
    return country_colors_[country.value()];
}

float PoliticalMapState::map_value(ProvinceId province) const noexcept {
    if (!valid_province(province)) return 0.0f;
    return map_values_[province.value()];
}

MapUploadStats PoliticalMapState::normalize_dirty(std::uint32_t merge_gap) {
    owner_dirty_.normalize_or_full(static_cast<std::uint32_t>(province_records_.size()), merge_gap);
    country_color_dirty_.normalize_or_full(static_cast<std::uint32_t>(country_colors_.size()), merge_gap);
    map_value_dirty_.normalize_or_full(static_cast<std::uint32_t>(map_values_.size()), merge_gap);

    MapUploadStats stats;
    stats.owner_bytes = owner_dirty_.element_count() * sizeof(ProvincePoliticalRecord);
    stats.country_color_bytes = country_color_dirty_.element_count() * sizeof(Rgba8);
    stats.map_value_bytes = map_value_dirty_.element_count() * sizeof(float);
    stats.owner_spans = static_cast<std::uint32_t>(owner_dirty_.spans().size());
    stats.country_color_spans = static_cast<std::uint32_t>(country_color_dirty_.spans().size());
    stats.map_value_spans = static_cast<std::uint32_t>(map_value_dirty_.spans().size());
    return stats;
}

} // namespace thunder
