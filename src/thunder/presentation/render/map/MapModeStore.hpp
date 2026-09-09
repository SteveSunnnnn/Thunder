#pragma once

#include "thunder/presentation/render/map/PoliticalMapState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum class MapModeValueKind : std::uint8_t {
    None,
    Owner,
    ScalarUnorm16,
    CategoryU16,
};

enum class ScalarMapTransform : std::uint8_t {
    Linear,
    Log1p,
};

struct MapModeDescriptor {
    MapMode mode = MapMode::Terrain;
    MapModeValueKind kind = MapModeValueKind::None;
    ScalarMapTransform transform = ScalarMapTransform::Linear;
    const char* name = "terrain";
};

struct ScalarMapRange {
    float source_min = 0.0f;
    float source_max = 1.0f;
    float transformed_min = 0.0f;
    float transformed_max = 1.0f;
};

struct MapModeGpuView {
    MapMode mode = MapMode::Terrain;
    MapModeValueKind kind = MapModeValueKind::None;
    std::uint32_t element_offset = 0;
    std::uint32_t element_count = 0;
    ScalarMapRange range{};
    std::uint64_t generation = 0;
};

// Compact mode-major province data. All common map modes stay resident on GPU;
// switching modes changes only a tiny uniform containing kind/base offset/range.
// Scalar values use UNORM16 after mode-specific transforms: at 8k provinces,
// seven data modes are only ~112 KiB rather than rebuilding color textures.
class MapModeStore {
public:
    void resize(std::uint32_t province_count);

    void set_scalar(MapMode mode, std::span<const float> values);
    void set_categories(MapMode mode, std::span<const std::uint16_t> values);
    void set_scalar_value(MapMode mode, ProvinceId province, float value);
    void set_category_value(MapMode mode, ProvinceId province, std::uint16_t value);

    [[nodiscard]] MapModeGpuView gpu_view(MapMode mode) const noexcept;
    [[nodiscard]] std::span<const std::uint16_t> gpu_words() const noexcept { return words_; }
    [[nodiscard]] std::uint32_t province_count() const noexcept { return province_count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return words_.size() * sizeof(std::uint16_t); }
    [[nodiscard]] float decode_scalar(MapMode mode, ProvinceId province) const noexcept;
    [[nodiscard]] std::uint16_t category(MapMode mode, ProvinceId province) const noexcept;

    [[nodiscard]] static const MapModeDescriptor& descriptor(MapMode mode) noexcept;

private:
    static constexpr std::size_t mode_count = 9;
    static constexpr std::size_t data_mode_count = 7;
    static constexpr std::size_t index_of(MapMode mode) noexcept { return static_cast<std::size_t>(mode); }
    [[nodiscard]] static std::uint32_t data_slot_of(MapMode mode) noexcept;
    [[nodiscard]] std::uint32_t offset_of(MapMode mode) const noexcept;
    [[nodiscard]] bool valid_province(ProvinceId province) const noexcept;
    void encode_scalar_slot(MapMode mode, std::span<const float> values);

    std::uint32_t province_count_ = 0;
    std::vector<std::uint16_t> words_;
    std::array<ScalarMapRange, mode_count> ranges_{};
    std::array<std::uint64_t, mode_count> generations_{};
};

} // namespace thunder
