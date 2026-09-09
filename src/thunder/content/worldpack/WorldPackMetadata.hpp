#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

// The metadata chunk is deliberately small and human-readable. It describes
// the coordinate contract for all streamed page families; page payloads never
// need to guess a projection or an implicit world rectangle.
struct WorldPackMetadata {
    std::uint32_t schema_version = 1;
    std::string projection = "mercator";
    std::array<double, 4> bounds_wgs84{};   // min lon, min lat, max lon, max lat
    std::array<double, 4> bounds_world_m{}; // min x, min y, max x, max y
    bool horizontal_wrap = false;
    std::uint32_t page_size = 128;
    double base_page_world_size_m = 64'000.0;
    std::uint32_t clip_levels = 1;
    std::int32_t page_origin_x = 0;
    std::int32_t page_origin_y = 0;
    std::uint32_t base_page_count_x = 0;
    std::uint32_t base_page_count_y = 0;
    std::uint32_t province_count = 0;
    std::uint32_t state_count = 0;
    std::uint32_t country_count = 0;
    std::uint32_t area_count = 0;
    std::uint32_t trade_province_count = 0;
    std::uint32_t location_count = 0;
    std::uint32_t sea_count = 0;
    std::uint32_t lake_count = 0;
    std::uint32_t authored_hub_count = 0;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint32_t page_count_x(std::uint32_t level) const noexcept;
    [[nodiscard]] std::uint32_t page_count_y(std::uint32_t level) const noexcept;
};

[[nodiscard]] std::string world_pack_metadata_json(const WorldPackMetadata& metadata);
[[nodiscard]] WorldPackMetadata parse_world_pack_metadata(std::span<const std::byte> payload);

} // namespace thunder
