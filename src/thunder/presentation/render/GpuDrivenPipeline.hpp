#pragma once

#include "thunder/simulation/living/LivingMapTypes.hpp"
#include "thunder/presentation/render/StrategicCamera.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct FrustumPlane {
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    float d = 0.0f;

    [[nodiscard]] float distance_to_point(float x, float y, float z) const noexcept {
        return nx * x + ny * y + nz * z + d;
    }
};

struct FrustumPlanes {
    std::array<FrustumPlane, 6> planes; // Left, Right, Bottom, Top, Near, Far

    [[nodiscard]] static FrustumPlanes from_view_projection(const std::array<float, 16>& vp);
    [[nodiscard]] bool intersects_sphere(float x, float y, float z, float radius) const noexcept;
    [[nodiscard]] bool intersects_box(float min_x, float min_y, float min_z,
                                      float max_x, float max_y, float max_z) const noexcept;
};

// Exact binary layout matching Vulkan VkDrawIndexedIndirectCommand
struct GpuDrawIndexedIndirectCommand {
    std::uint32_t index_count = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};
static_assert(sizeof(GpuDrawIndexedIndirectCommand) == 20u);

// Exact binary layout matching Vulkan VkDrawIndirectCommand
struct GpuDrawIndirectCommand {
    std::uint32_t vertex_count = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t first_vertex = 0;
    std::uint32_t first_instance = 0;
};
static_assert(sizeof(GpuDrawIndirectCommand) == 16u);

struct GpuCullingConfig {
    float near_lod_dist = 50'000.0f;
    float med_lod_dist = 200'000.0f;
    float far_lod_dist = 800'000.0f;
    bool frustum_culling_enabled = true;
    bool distance_culling_enabled = true;
};

struct LodMeshBinding {
    std::uint32_t index_count = 0;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
};

struct GpuCullingOutput {
    std::vector<LivingInstanceGpu> visible_instances_lod0;
    std::vector<LivingInstanceGpu> visible_instances_lod1;
    std::vector<LivingInstanceGpu> visible_instances_lod2;
    std::vector<GpuDrawIndexedIndirectCommand> indirect_commands;
    std::size_t total_culled_instances = 0;
    std::size_t total_visible_instances = 0;
};

class GpuCullingPipeline {
public:
    [[nodiscard]] static GpuCullingOutput cull_and_generate_draws(
        std::span<const LivingInstanceGpu> instances,
        double chunk_origin_x,
        double chunk_origin_y,
        const StrategicCameraState& camera,
        const FrustumPlanes& frustum,
        const GpuCullingConfig& config,
        const std::array<LodMeshBinding, 3>& lod_meshes);
};

} // namespace thunder
