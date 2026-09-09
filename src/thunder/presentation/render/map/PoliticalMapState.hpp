#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/presentation/render/map/DirtySpanSet.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum class MapMode : std::uint8_t {
    Terrain,
    Political,
    Population,
    Gdp,
    GdpPerCapita,
    StandardOfLiving,
    Market,
    Culture,
    Religion
};

struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
    friend constexpr bool operator==(Rgba8, Rgba8) = default;
};

struct ProvincePoliticalRecord {
    std::uint32_t owner_country = 0xffffffffu;
    std::uint32_t flags = 0u;
};
static_assert(sizeof(ProvincePoliticalRecord) == 8u);

enum ProvincePoliticalFlags : std::uint32_t {
    PoliticalSea = 1u << 0u,
    PoliticalLake = 1u << 1u,
    PoliticalOccupied = 1u << 2u,
    PoliticalDisputed = 1u << 3u,
};

struct MapUploadStats {
    std::uint64_t owner_bytes = 0;
    std::uint64_t country_color_bytes = 0;
    std::uint64_t map_value_bytes = 0;
    std::uint32_t owner_spans = 0;
    std::uint32_t country_color_spans = 0;
    std::uint32_t map_value_spans = 0;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept {
        return owner_bytes + country_color_bytes + map_value_bytes;
    }
};

// Mutable visual indirection tables. The expensive province-id raster is static;
// conquest only changes an 8-byte province record, not map geometry or textures.
class PoliticalMapState {
public:
    void resize(std::uint32_t province_count, std::uint32_t country_count);
    void clear_dirty() noexcept;

    void set_owner(ProvinceId province, CountryId owner) noexcept;
    void set_province_flags(ProvinceId province, std::uint32_t flags) noexcept;
    void set_country_color(CountryId country, Rgba8 color) noexcept;
    void set_map_value(ProvinceId province, float value) noexcept;

    [[nodiscard]] CountryId owner(ProvinceId province) const noexcept;
    [[nodiscard]] Rgba8 country_color(CountryId country) const noexcept;
    [[nodiscard]] float map_value(ProvinceId province) const noexcept;

    [[nodiscard]] std::span<const ProvincePoliticalRecord> province_records() const noexcept { return province_records_; }
    [[nodiscard]] std::span<const Rgba8> country_colors() const noexcept { return country_colors_; }
    [[nodiscard]] std::span<const float> map_values() const noexcept { return map_values_; }

    MapUploadStats normalize_dirty(std::uint32_t merge_gap = 3u);
    [[nodiscard]] std::span<const DirtySpan> owner_dirty() const noexcept { return owner_dirty_.spans(); }
    [[nodiscard]] std::span<const DirtySpan> country_color_dirty() const noexcept { return country_color_dirty_.spans(); }
    [[nodiscard]] std::span<const DirtySpan> map_value_dirty() const noexcept { return map_value_dirty_.spans(); }

private:
    [[nodiscard]] bool valid_province(ProvinceId id) const noexcept;
    [[nodiscard]] bool valid_country(CountryId id) const noexcept;

    std::vector<ProvincePoliticalRecord> province_records_;
    std::vector<Rgba8> country_colors_;
    std::vector<float> map_values_;
    DirtySpanSet owner_dirty_{64};
    DirtySpanSet country_color_dirty_{32};
    DirtySpanSet map_value_dirty_{64};
};

} // namespace thunder
