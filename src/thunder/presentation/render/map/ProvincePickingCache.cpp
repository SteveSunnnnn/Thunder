#include "thunder/presentation/render/map/ProvincePickingCache.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thunder {

ProvincePickingCache::ProvincePickingCache(std::uint32_t page_capacity, ProvincePickingConfig config)
    : config_(config), slots_(page_capacity) {
    if (page_capacity == 0u || config_.base_page_world_size_m <= 0.0) {
        throw std::invalid_argument("invalid province picking cache configuration");
    }
    lookup_.reserve(static_cast<std::size_t>(page_capacity) * 2u);
    lookup_.max_load_factor(0.70f);
}

WorldMapPageKey ProvincePickingCache::key_for_world(WorldMeters world, std::uint16_t level) const noexcept {
    const auto clamped_level = std::min(level, config_.maximum_level);
    const double page_size = config_.base_page_world_size_m * static_cast<double>(std::uint64_t{1} << clamped_level);
    const auto x = static_cast<std::int64_t>(std::floor((world.x - config_.origin_x_m) / page_size));
    const auto y = static_cast<std::int64_t>(std::floor((world.y - config_.origin_y_m) / page_size));
    return {
        static_cast<std::int32_t>(std::clamp<std::int64_t>(x, INT32_MIN, INT32_MAX)),
        static_cast<std::int32_t>(std::clamp<std::int64_t>(y, INT32_MIN, INT32_MAX)),
        clamped_level
    };
}

std::pair<std::uint32_t, std::uint32_t> ProvincePickingCache::texel_for_world(WorldMeters world,
                                                                              WorldMapPageKey key) const noexcept {
    const double page_size = config_.base_page_world_size_m * static_cast<double>(std::uint64_t{1} << key.level);
    const double origin_x = config_.origin_x_m + static_cast<double>(key.x) * page_size;
    const double origin_y = config_.origin_y_m + static_cast<double>(key.y) * page_size;
    const double u = std::clamp((world.x - origin_x) / page_size, 0.0, 0.999999999);
    // GIS raster rows are encoded north-to-south (`from_origin`). Keep CPU
    // picking in the same convention as the GPU page sampler instead of
    // vertically mirroring province IDs.
    const double v = std::clamp((origin_y + page_size - world.y) / page_size,
                                0.0, 0.999999999);
    return {
        static_cast<std::uint32_t>(u * ProvinceRasterPage::samples_per_side),
        static_cast<std::uint32_t>(v * ProvinceRasterPage::samples_per_side)
    };
}

void ProvincePickingCache::insert(WorldMapPageKey key, const ProvinceRasterPage& page, std::uint64_t frame) {
    if (const auto it = lookup_.find(key); it != lookup_.end()) {
        auto& slot = slots_[it->second];
        slot.page = page;
        slot.last_used_frame = frame;
        return;
    }

    if (resident_count_ < slots_.size()) {
        for (std::uint32_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].occupied) {
                slots_[i] = {key, page, frame, true};
                lookup_.emplace(key, i);
                ++resident_count_;
                next_victim_ = (i + 1u) % static_cast<std::uint32_t>(slots_.size());
                return;
            }
        }
    }

    std::uint32_t victim = next_victim_;
    std::uint64_t oldest = slots_[victim].last_used_frame;
    for (std::uint32_t scanned = 1u; scanned < slots_.size(); ++scanned) {
        const auto candidate = (next_victim_ + scanned) % static_cast<std::uint32_t>(slots_.size());
        if (slots_[candidate].last_used_frame < oldest) {
            victim = candidate;
            oldest = slots_[candidate].last_used_frame;
        }
    }

    lookup_.erase(slots_[victim].key);
    slots_[victim] = {key, page, frame, true};
    lookup_.emplace(key, victim);
    next_victim_ = (victim + 1u) % static_cast<std::uint32_t>(slots_.size());
}

ProvinceId ProvincePickingCache::pick_exact(WorldMeters world, WorldMapPageKey key, std::uint64_t frame) noexcept {
    const auto it = lookup_.find(key);
    if (it == lookup_.end()) return ProvinceId{};
    auto& slot = slots_[it->second];
    slot.last_used_frame = frame;
    const auto [x, y] = texel_for_world(world, key);
    return slot.page.sample(x, y);
}

ProvinceId ProvincePickingCache::pick(WorldMeters world, std::uint16_t preferred_level, std::uint64_t frame) noexcept {
    return pick_exact(world, key_for_world(world, preferred_level), frame);
}

ProvinceId ProvincePickingCache::pick_with_fallback(WorldMeters world, std::uint16_t preferred_level,
                                                     std::uint64_t frame) noexcept {
    for (std::uint16_t level = std::min(preferred_level, config_.maximum_level); level <= config_.maximum_level; ++level) {
        const auto result = pick_exact(world, key_for_world(world, level), frame);
        if (result.valid()) return result;
        if (level == config_.maximum_level) break;
    }
    return ProvinceId{};
}

bool ProvincePickingCache::resident(WorldMapPageKey key) const noexcept {
    return lookup_.find(key) != lookup_.end();
}

} // namespace thunder
