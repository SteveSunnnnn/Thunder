#pragma once

#include "thunder/presentation/render/StrategicCamera.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {

struct TerrainPatchKey {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint16_t level = 0;

    friend bool operator==(const TerrainPatchKey&, const TerrainPatchKey&) = default;
};

enum class TerrainMeshLod : std::uint16_t {
    Fine65 = 0,
    Medium33 = 1,
    Coarse17 = 2
};

struct TerrainPatchInstance {
    TerrainPatchKey key{};
    std::uint16_t flags = 0;
    float relative_x_m = 0.0f;
    float relative_y_m = 0.0f;
    float world_size_m = 0.0f;
    float morph = 0.0f;
    std::uint32_t height_page = 0;
    std::uint32_t material_page = 0;
};

static_assert(sizeof(TerrainPatchInstance) <= 40, "Terrain patch instances must stay compact");

struct TerrainClipmapConfig {
    std::uint32_t levels = 8;
    std::uint32_t patches_per_side = 8;
    double base_patch_size_m = 1'024.0;
    double origin_snap_m = 256.0;
};

struct TerrainClipmapBuildStats {
    std::size_t patch_count = 0;
    std::size_t bytes = 0;
    std::uint64_t estimated_triangles = 0;
    double snapped_origin_x = 0.0;
    double snapped_origin_y = 0.0;
};

class TerrainClipmap {
public:
    explicit TerrainClipmap(TerrainClipmapConfig config = {});

    [[nodiscard]] std::size_t max_patch_count() const noexcept;
    [[nodiscard]] const TerrainClipmapConfig& config() const noexcept { return config_; }

    TerrainClipmapBuildStats build(const StrategicCameraState& camera, std::vector<TerrainPatchInstance>& out) const;
    [[nodiscard]] static TerrainMeshLod mesh_lod_for_level(std::uint32_t level) noexcept;
    [[nodiscard]] static std::uint32_t triangles_per_patch(TerrainMeshLod lod) noexcept;

private:
    TerrainClipmapConfig config_{};
};

} // namespace thunder
