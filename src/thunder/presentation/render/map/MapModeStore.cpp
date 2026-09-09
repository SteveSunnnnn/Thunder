#include "thunder/presentation/render/map/MapModeStore.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {

constexpr std::array<MapModeDescriptor, 9> descriptors{{
    {MapMode::Terrain, MapModeValueKind::None, ScalarMapTransform::Linear, "terrain"},
    {MapMode::Political, MapModeValueKind::Owner, ScalarMapTransform::Linear, "political"},
    {MapMode::Population, MapModeValueKind::ScalarUnorm16, ScalarMapTransform::Log1p, "population"},
    {MapMode::Gdp, MapModeValueKind::ScalarUnorm16, ScalarMapTransform::Log1p, "gdp"},
    {MapMode::GdpPerCapita, MapModeValueKind::ScalarUnorm16, ScalarMapTransform::Log1p, "gdp_per_capita"},
    {MapMode::StandardOfLiving, MapModeValueKind::ScalarUnorm16, ScalarMapTransform::Linear, "standard_of_living"},
    {MapMode::Market, MapModeValueKind::CategoryU16, ScalarMapTransform::Linear, "market"},
    {MapMode::Culture, MapModeValueKind::CategoryU16, ScalarMapTransform::Linear, "culture"},
    {MapMode::Religion, MapModeValueKind::CategoryU16, ScalarMapTransform::Linear, "religion"},
}};

float transform_value(float value, ScalarMapTransform transform) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    if (transform == ScalarMapTransform::Log1p) return std::log1p(std::max(value, 0.0f));
    return value;
}

float inverse_transform(float value, ScalarMapTransform transform) noexcept {
    if (transform == ScalarMapTransform::Log1p) return std::expm1(value);
    return value;
}

} // namespace

void MapModeStore::resize(std::uint32_t province_count) {
    province_count_ = province_count;
    words_.assign(static_cast<std::size_t>(province_count_) * data_mode_count, 0u);
    ranges_.fill({});
    generations_.fill(0u);
}

const MapModeDescriptor& MapModeStore::descriptor(MapMode mode) noexcept {
    const auto index = index_of(mode);
    return descriptors[index < descriptors.size() ? index : 0u];
}

std::uint32_t MapModeStore::data_slot_of(MapMode mode) noexcept {
    switch (mode) {
        case MapMode::Population: return 0u;
        case MapMode::Gdp: return 1u;
        case MapMode::GdpPerCapita: return 2u;
        case MapMode::StandardOfLiving: return 3u;
        case MapMode::Market: return 4u;
        case MapMode::Culture: return 5u;
        case MapMode::Religion: return 6u;
        case MapMode::Terrain:
        case MapMode::Political: return 0u;
    }
    return 0u;
}

std::uint32_t MapModeStore::offset_of(MapMode mode) const noexcept {
    return static_cast<std::uint32_t>(static_cast<std::size_t>(data_slot_of(mode)) * province_count_);
}

bool MapModeStore::valid_province(ProvinceId province) const noexcept {
    return province.valid() && province.value() < province_count_;
}

void MapModeStore::set_scalar(MapMode mode, std::span<const float> values) {
    if (descriptor(mode).kind != MapModeValueKind::ScalarUnorm16) throw std::invalid_argument("map mode is not scalar");
    if (values.size() != province_count_) throw std::invalid_argument("map mode scalar count mismatch");
    encode_scalar_slot(mode, values);
    ++generations_[index_of(mode)];
}

void MapModeStore::encode_scalar_slot(MapMode mode, std::span<const float> values) {
    const auto transform = descriptor(mode).transform;
    float min_v = std::numeric_limits<float>::infinity();
    float max_v = -std::numeric_limits<float>::infinity();

    if (transform == ScalarMapTransform::Linear) {
        for (const float v : values) {
            const float t = std::isfinite(v) ? v : 0.0f;
            min_v = std::min(min_v, t);
            max_v = std::max(max_v, t);
        }
    } else {
        for (const float v : values) {
            const float t = std::isfinite(v) ? std::log1p(std::max(v, 0.0f)) : 0.0f;
            min_v = std::min(min_v, t);
            max_v = std::max(max_v, t);
        }
    }

    if (!std::isfinite(min_v) || !std::isfinite(max_v)) { min_v = 0.0f; max_v = 1.0f; }
    if (max_v - min_v < 1e-12f) max_v = min_v + 1.0f;

    auto& range = ranges_[index_of(mode)];
    range.transformed_min = min_v;
    range.transformed_max = max_v;
    range.source_min = inverse_transform(min_v, transform);
    range.source_max = inverse_transform(max_v, transform);

    const float inv = 65535.0f / (max_v - min_v);
    const std::size_t base = offset_of(mode);

    if (transform == ScalarMapTransform::Linear) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            const float v = values[i];
            const float t = std::isfinite(v) ? v : 0.0f;
            const float normalized = std::clamp((t - min_v) * inv, 0.0f, 65535.0f);
            words_[base + i] = static_cast<std::uint16_t>(normalized + 0.5f);
        }
    } else {
        for (std::size_t i = 0; i < values.size(); ++i) {
            const float v = values[i];
            const float t = std::isfinite(v) ? std::log1p(std::max(v, 0.0f)) : 0.0f;
            const float normalized = std::clamp((t - min_v) * inv, 0.0f, 65535.0f);
            words_[base + i] = static_cast<std::uint16_t>(normalized + 0.5f);
        }
    }
}

void MapModeStore::set_categories(MapMode mode, std::span<const std::uint16_t> values) {
    if (descriptor(mode).kind != MapModeValueKind::CategoryU16) throw std::invalid_argument("map mode is not categorical");
    if (values.size() != province_count_) throw std::invalid_argument("map mode category count mismatch");
    const std::size_t base = offset_of(mode);
    std::copy(values.begin(), values.end(), words_.begin() + static_cast<std::ptrdiff_t>(base));
    ++generations_[index_of(mode)];
}

void MapModeStore::set_scalar_value(MapMode mode, ProvinceId province, float value) {
    if (!valid_province(province)) return;
    if (descriptor(mode).kind != MapModeValueKind::ScalarUnorm16) return;
    // Single-value updates use the existing range. If simulation values move beyond
    // it, clamp for this frame; periodic full re-encoding can refresh the range.
    const auto range = ranges_[index_of(mode)];
    const auto transform = descriptor(mode).transform;
    const float t = transform_value(value, transform);
    const float span = std::max(range.transformed_max - range.transformed_min, 1e-12f);
    const float q = std::clamp((t - range.transformed_min) * (65535.0f / span), 0.0f, 65535.0f);
    words_[static_cast<std::size_t>(offset_of(mode)) + province.value()] = static_cast<std::uint16_t>(std::lround(q));
    ++generations_[index_of(mode)];
}

void MapModeStore::set_category_value(MapMode mode, ProvinceId province, std::uint16_t value) {
    if (!valid_province(province) || descriptor(mode).kind != MapModeValueKind::CategoryU16) return;
    words_[static_cast<std::size_t>(offset_of(mode)) + province.value()] = value;
    ++generations_[index_of(mode)];
}

MapModeGpuView MapModeStore::gpu_view(MapMode mode) const noexcept {
    const auto i = index_of(mode);
    if (i >= mode_count) return {};
    const auto kind = descriptor(mode).kind;
    const auto count = (kind == MapModeValueKind::ScalarUnorm16 || kind == MapModeValueKind::CategoryU16) ? province_count_ : 0u;
    return {mode, kind, offset_of(mode), count, ranges_[i], generations_[i]};
}

float MapModeStore::decode_scalar(MapMode mode, ProvinceId province) const noexcept {
    if (!valid_province(province) || descriptor(mode).kind != MapModeValueKind::ScalarUnorm16) return 0.0f;
    const auto& range = ranges_[index_of(mode)];
    const std::uint16_t q = words_[static_cast<std::size_t>(offset_of(mode)) + province.value()];
    const float t = range.transformed_min + (static_cast<float>(q) / 65535.0f) * (range.transformed_max - range.transformed_min);
    return inverse_transform(t, descriptor(mode).transform);
}

std::uint16_t MapModeStore::category(MapMode mode, ProvinceId province) const noexcept {
    if (!valid_province(province) || descriptor(mode).kind != MapModeValueKind::CategoryU16) return 0u;
    return words_[static_cast<std::size_t>(offset_of(mode)) + province.value()];
}

} // namespace thunder
