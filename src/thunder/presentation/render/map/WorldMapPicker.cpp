#include "thunder/presentation/render/map/WorldMapPicker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {

bool WorldMapPicker::open(const std::filesystem::path& world_pack,
                          const WorldMapHierarchy& hierarchy,
                          std::string& diagnostic) {
    close();
    if (page_capacity_ == 0u) {
        diagnostic = "world map picker page capacity is zero";
        return false;
    }
    if (!source_.open(world_pack, diagnostic)) return false;
    const auto& metadata = source_.metadata();
    const auto nx = metadata.page_count_x(0u);
    const auto ny = metadata.page_count_y(0u);
    const auto count = static_cast<std::uint64_t>(nx) * ny;
    if (count == 0u || count > static_cast<std::uint64_t>(page_capacity_) * 16u) {
        diagnostic = "finest picking pages exceed resident page budget: " + std::to_string(count);
        close();
        return false;
    }
    try {
        directory_.resize(static_cast<std::size_t>(count));
        WorldMapPage decoded;
        for (std::uint32_t y = 0u; y < ny; ++y) {
            for (std::uint32_t x = 0u; x < nx; ++x) {
                if (!source_.decode({static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), 0u}, decoded))
                    throw std::runtime_error("cannot preload finest picking page " +
                                             std::to_string(x) + "/" + std::to_string(y));
                auto& address = directory_[static_cast<std::size_t>(y) * nx + x];
                const auto code = decoded.province.front();
                if (std::all_of(decoded.province.begin(), decoded.province.end(),
                                [code](auto sample) { return sample == code; })) {
                    address = 0x80000000u | code;
                } else {
                    if (pages_.size() >= page_capacity_)
                        throw std::runtime_error("mixed picking pages exceed resident page budget");
                    address = static_cast<std::uint32_t>(pages_.size());
                    auto& page = pages_.emplace_back();
                    std::copy(decoded.province.begin(), decoded.province.end(), page.samples().begin());
                }
            }
        }
    } catch (const std::exception& error) {
        diagnostic = error.what();
        close();
        return false;
    }
    hierarchy_ = &hierarchy;
    diagnostic.clear();
    return true;
}

void WorldMapPicker::close() noexcept {
    std::vector<ProvinceRasterPage>{}.swap(pages_);
    std::vector<std::uint32_t>{}.swap(directory_);
    source_.close();
    hierarchy_ = nullptr;
}

WorldMapPickResult WorldMapPicker::resolve(ProvinceId raster_location) const noexcept {
    if (!raster_location.valid() || hierarchy_ == nullptr) return {};
    const auto location = hierarchy_->location_for_raster(raster_location);
    if (!location.valid()) return {};
    try {
        const auto province = hierarchy_->province_for_location(location);
        return {raster_location, location, province,
                hierarchy_->area_for_province(province)};
    } catch (...) {
        return {};
    }
}

WorldMapPickResult WorldMapPicker::pick_world(WorldMeters world,
                                              std::uint16_t preferred_level,
                                              std::uint64_t frame) {
    if (!ready() || !std::isfinite(world.x) || !std::isfinite(world.y)) return {};
    (void)preferred_level;
    (void)frame;
    const auto& metadata = source_.metadata();
    const double width = metadata.bounds_world_m[2] - metadata.bounds_world_m[0];
    if (metadata.horizontal_wrap && width > 0.0) {
        world.x = metadata.bounds_world_m[0] +
                  std::fmod(std::fmod(world.x - metadata.bounds_world_m[0], width) + width, width);
    }
    if (world.x < metadata.bounds_world_m[0] || world.x > metadata.bounds_world_m[2] ||
        world.y < metadata.bounds_world_m[1] || world.y > metadata.bounds_world_m[3]) return {};
    const auto nx = metadata.page_count_x(0u);
    const auto ny = metadata.page_count_y(0u);
    const double size = metadata.base_page_world_size_m;
    // Page y increases northward; samples within each page run southward.
    const double px = (world.x - metadata.bounds_world_m[0]) / size;
    const double py = (world.y - metadata.bounds_world_m[1]) / size;
    const auto x = std::min(static_cast<std::uint32_t>(px), nx - 1u);
    const auto y = std::min(static_cast<std::uint32_t>(py), ny - 1u);
    const auto tx = static_cast<std::uint32_t>(std::clamp(px - x, 0.0, 0.999999999) * 128.0);
    const auto ty = static_cast<std::uint32_t>(std::clamp(1.0 - (py - y), 0.0, 0.999999999) * 128.0);
    const auto address = directory_[static_cast<std::size_t>(y) * nx + x];
    if ((address & 0x80000000u) != 0u) {
        const auto code = address & 0xffffu;
        return code == 0u ? WorldMapPickResult{} : resolve(ProvinceId{code - 1u});
    }
    return resolve(pages_[address].sample(tx, ty));
}

WorldMapPickResult WorldMapPicker::pick_uv(double u, double v,
                                           std::uint16_t preferred_level,
                                           std::uint64_t frame) {
    if (!ready() || !std::isfinite(u) || !std::isfinite(v)) return {};
    const auto& metadata = source_.metadata();
    if (metadata.horizontal_wrap) u -= std::floor(u);
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) return {};
    return pick_world({
        metadata.bounds_world_m[0] + u * (metadata.bounds_world_m[2] - metadata.bounds_world_m[0]),
        metadata.bounds_world_m[3] - v * (metadata.bounds_world_m[3] - metadata.bounds_world_m[1])},
        preferred_level, frame);
}

} // namespace thunder
