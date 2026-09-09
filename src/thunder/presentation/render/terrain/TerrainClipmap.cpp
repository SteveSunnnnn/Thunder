#include "thunder/presentation/render/terrain/TerrainClipmap.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thunder {


TerrainMeshLod TerrainClipmap::mesh_lod_for_level(std::uint32_t level) noexcept {
    if (level == 0u) return TerrainMeshLod::Fine65;
    if (level <= 2u) return TerrainMeshLod::Medium33;
    return TerrainMeshLod::Coarse17;
}

std::uint32_t TerrainClipmap::triangles_per_patch(TerrainMeshLod lod) noexcept {
    switch (lod) {
        case TerrainMeshLod::Fine65: return 64u * 64u * 2u;
        case TerrainMeshLod::Medium33: return 32u * 32u * 2u;
        case TerrainMeshLod::Coarse17: return 16u * 16u * 2u;
    }
    return 0u;
}

TerrainClipmap::TerrainClipmap(TerrainClipmapConfig config) : config_(config) {
    if (config_.levels == 0u || config_.patches_per_side < 4u || (config_.patches_per_side % 2u) != 0u ||
        config_.base_patch_size_m <= 0.0 || config_.origin_snap_m <= 0.0) {
        throw std::invalid_argument("invalid terrain clipmap configuration");
    }
}

std::size_t TerrainClipmap::max_patch_count() const noexcept {
    const std::size_t side = config_.patches_per_side;
    const std::size_t full = side * side;
    const std::size_t inner_side = side / 2u;
    const std::size_t ring = full - (inner_side * inner_side);
    return full + (static_cast<std::size_t>(config_.levels) - 1u) * ring;
}

TerrainClipmapBuildStats TerrainClipmap::build(const StrategicCameraState& camera,
                                                std::vector<TerrainPatchInstance>& out) const {
    out.clear();
    if (out.capacity() < max_patch_count()) out.reserve(max_patch_count());

    const double snapped_x = std::floor(camera.center.x / config_.origin_snap_m) * config_.origin_snap_m;
    const double snapped_y = std::floor(camera.center.y / config_.origin_snap_m) * config_.origin_snap_m;
    const std::int32_t side = static_cast<std::int32_t>(config_.patches_per_side);
    const std::int32_t half = side / 2;
    const std::int32_t inner_half = side / 4;

    std::uint64_t estimated_triangles = 0;
    for (std::uint32_t level = 0; level < config_.levels; ++level) {
        const TerrainMeshLod mesh_lod = mesh_lod_for_level(level);
        const double patch_size = config_.base_patch_size_m * static_cast<double>(std::uint64_t{1} << level);
        const auto camera_tile_x = static_cast<std::int64_t>(std::floor(snapped_x / patch_size));
        const auto camera_tile_y = static_cast<std::int64_t>(std::floor(snapped_y / patch_size));

        for (std::int32_t oy = -half; oy < half; ++oy) {
            for (std::int32_t ox = -half; ox < half; ++ox) {
                if (level > 0u && ox >= -inner_half && ox < inner_half && oy >= -inner_half && oy < inner_half) {
                    continue;
                }

                const auto tile_x64 = camera_tile_x + ox;
                const auto tile_y64 = camera_tile_y + oy;
                if (tile_x64 < static_cast<std::int64_t>(INT32_MIN) || tile_x64 > static_cast<std::int64_t>(INT32_MAX) ||
                    tile_y64 < static_cast<std::int64_t>(INT32_MIN) || tile_y64 > static_cast<std::int64_t>(INT32_MAX)) {
                    continue;
                }

                const double world_x = static_cast<double>(tile_x64) * patch_size;
                const double world_y = static_cast<double>(tile_y64) * patch_size;
                const double rel_x = world_x - snapped_x;
                const double rel_y = world_y - snapped_y;
                const double radius = std::hypot(rel_x, rel_y);
                const double ring_radius = patch_size * static_cast<double>(half);
                const float morph = static_cast<float>(std::clamp(radius / std::max(ring_radius, 1.0), 0.0, 1.0));

                TerrainPatchInstance patch;
                patch.key.x = static_cast<std::int32_t>(tile_x64);
                patch.key.y = static_cast<std::int32_t>(tile_y64);
                patch.key.level = static_cast<std::uint16_t>(level);
                patch.flags = static_cast<std::uint16_t>(mesh_lod);
                patch.relative_x_m = static_cast<float>(rel_x);
                patch.relative_y_m = static_cast<float>(rel_y);
                patch.world_size_m = static_cast<float>(patch_size);
                patch.morph = morph;
                out.push_back(patch);
                estimated_triangles += triangles_per_patch(mesh_lod);
            }
        }
    }

    return {
        out.size(),
        out.size() * sizeof(TerrainPatchInstance),
        estimated_triangles,
        snapped_x,
        snapped_y
    };
}

} // namespace thunder
