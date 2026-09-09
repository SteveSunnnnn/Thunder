#pragma once

#include "thunder/foundation/geo/MercatorProjection.hpp"
#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"
#include "thunder/presentation/render/map/WorldMapPageKey.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace thunder {

struct ProvincePickingConfig {
    double base_page_world_size_m = 1'024.0;
    std::uint16_t maximum_level = 7u;
    double origin_x_m = 0.0;
    double origin_y_m = 0.0;
};

// CPU mirror of a small subset of streamed province-ID pages. Hover/picking never
// needs a GPU image readback or fence wait. Pages use the exact same virtual keys
// as the GPU political-map atlas.
class ProvincePickingCache {
public:
    explicit ProvincePickingCache(std::uint32_t page_capacity = 512u,
                                  ProvincePickingConfig config = {});

    void insert(WorldMapPageKey key, const ProvinceRasterPage& page, std::uint64_t frame);
    [[nodiscard]] ProvinceId pick(WorldMeters world, std::uint16_t preferred_level, std::uint64_t frame) noexcept;
    [[nodiscard]] ProvinceId pick_with_fallback(WorldMeters world, std::uint16_t preferred_level,
                                                std::uint64_t frame) noexcept;
    [[nodiscard]] bool resident(WorldMapPageKey key) const noexcept;
    [[nodiscard]] std::uint32_t resident_count() const noexcept { return resident_count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return slots_.size() * sizeof(Slot); }

    [[nodiscard]] WorldMapPageKey key_for_world(WorldMeters world, std::uint16_t level) const noexcept;

private:
    struct Slot {
        WorldMapPageKey key{};
        ProvinceRasterPage page{};
        std::uint64_t last_used_frame = 0;
        bool occupied = false;
    };

    [[nodiscard]] ProvinceId pick_exact(WorldMeters world, WorldMapPageKey key, std::uint64_t frame) noexcept;
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> texel_for_world(WorldMeters world,
                                                                         WorldMapPageKey key) const noexcept;

    ProvincePickingConfig config_{};
    std::vector<Slot> slots_;
    std::unordered_map<WorldMapPageKey, std::uint32_t, WorldMapPageKeyHash> lookup_;
    std::uint32_t resident_count_ = 0;
    std::uint32_t next_victim_ = 0;
};

} // namespace thunder
