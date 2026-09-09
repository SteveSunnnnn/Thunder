#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/foundation/geo/MercatorProjection.hpp"
#include "thunder/presentation/render/map/ProvincePickingCache.hpp"
#include "thunder/presentation/render/map/WorldMapPageSource.hpp"
#include "thunder/simulation/world/WorldMapHierarchy.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace thunder {

struct WorldMapPickResult {
    ProvinceId raster_location{};
    LocationId location{};
    TradeProvinceId province{};
    AreaId area{};

    [[nodiscard]] bool valid() const noexcept { return location.valid(); }
};

// CPU picking facade over the same virtual pages used by the Vulkan map. It
// loads the finest categorical pages at open, then performs a direct two-level
// array lookup. Hover never decodes, allocates, evicts, or reads from the GPU.
// Coarser render LODs must never change which atomic Location is picked.
class WorldMapPicker {
public:
    explicit WorldMapPicker(std::uint32_t page_capacity = 65'536u)
        : page_capacity_(page_capacity) {}

    [[nodiscard]] bool open(const std::filesystem::path& world_pack,
                            const WorldMapHierarchy& hierarchy,
                            std::string& diagnostic);
    void close() noexcept;

    [[nodiscard]] bool ready() const noexcept { return source_.ready() && !directory_.empty(); }
    [[nodiscard]] std::size_t resident_bytes() const noexcept {
        return pages_.size() * sizeof(ProvinceRasterPage) + directory_.size() * sizeof(std::uint32_t);
    }
    [[nodiscard]] const WorldPackMetadata& metadata() const noexcept { return source_.metadata(); }

    [[nodiscard]] WorldMapPickResult pick_world(WorldMeters world,
                                                std::uint16_t preferred_level,
                                                std::uint64_t frame);
    [[nodiscard]] WorldMapPickResult pick_uv(double u, double v,
                                             std::uint16_t preferred_level,
                                             std::uint64_t frame);

private:
    [[nodiscard]] WorldMapPickResult resolve(ProvinceId raster_location) const noexcept;

    std::uint32_t page_capacity_ = 65'536u;
    WorldMapPageSource source_;
    std::vector<ProvinceRasterPage> pages_;
    // High bit denotes a uniform page; low 16 bits are its raster code.
    // Other entries directly index the dense mixed-page storage.
    std::vector<std::uint32_t> directory_;
    const WorldMapHierarchy* hierarchy_ = nullptr;
};

} // namespace thunder
