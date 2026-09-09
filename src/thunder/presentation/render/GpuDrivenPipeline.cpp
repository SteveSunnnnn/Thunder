#include "thunder/presentation/render/GpuDrivenPipeline.hpp"
#include <array>

#include <algorithm>
#include <cmath>

namespace thunder {

namespace {

void normalize_plane(FrustumPlane& p) noexcept {
    const float len = std::sqrt(p.nx * p.nx + p.ny * p.ny + p.nz * p.nz);
    if (len > 1e-8f) {
        const float inv_len = 1.0f / len;
        p.nx *= inv_len;
        p.ny *= inv_len;
        p.nz *= inv_len;
        p.d *= inv_len;
    }
}

} // namespace

FrustumPlanes FrustumPlanes::from_view_projection(const std::array<float, 16>& vp) {
    // vp matrix in column-major:
    // row 0: vp[0], vp[4], vp[8], vp[12]
    // row 1: vp[1], vp[5], vp[9], vp[13]
    // row 2: vp[2], vp[6], vp[10], vp[14]
    // row 3: vp[3], vp[7], vp[11], vp[15]

    FrustumPlanes fp;

    // Left plane: row 3 + row 0
    fp.planes[0] = {vp[3] + vp[0], vp[7] + vp[4], vp[11] + vp[8], vp[15] + vp[12]};
    // Right plane: row 3 - row 0
    fp.planes[1] = {vp[3] - vp[0], vp[7] - vp[4], vp[11] - vp[8], vp[15] - vp[12]};
    // Bottom plane: row 3 + row 1
    fp.planes[2] = {vp[3] + vp[1], vp[7] + vp[5], vp[11] + vp[9], vp[15] + vp[13]};
    // Top plane: row 3 - row 1
    fp.planes[3] = {vp[3] - vp[1], vp[7] - vp[5], vp[11] - vp[9], vp[15] - vp[13]};
    // Near plane: row 3 + row 2 (or row 2 for Vulkan 0..1 range)
    fp.planes[4] = {vp[2], vp[6], vp[10], vp[14]};
    // Far plane: row 3 - row 2
    fp.planes[5] = {vp[3] - vp[2], vp[7] - vp[6], vp[11] - vp[10], vp[15] - vp[14]};

    for (auto& plane : fp.planes) {
        normalize_plane(plane);
    }

    return fp;
}

bool FrustumPlanes::intersects_sphere(float x, float y, float z, float radius) const noexcept {
    for (const auto& plane : planes) {
        if (plane.distance_to_point(x, y, z) < -radius) {
            return false;
        }
    }
    return true;
}

bool FrustumPlanes::intersects_box(float min_x, float min_y, float min_z,
                                   float max_x, float max_y, float max_z) const noexcept {
    for (const auto& plane : planes) {
        const float px = plane.nx > 0.0f ? max_x : min_x;
        const float py = plane.ny > 0.0f ? max_y : min_y;
        const float pz = plane.nz > 0.0f ? max_z : min_z;
        if (plane.distance_to_point(px, py, pz) < 0.0f) {
            return false;
        }
    }
    return true;
}

GpuCullingOutput GpuCullingPipeline::cull_and_generate_draws(
    std::span<const LivingInstanceGpu> instances,
    double chunk_origin_x,
    double chunk_origin_y,
    const StrategicCameraState& camera,
    const FrustumPlanes& frustum,
    const GpuCullingConfig& config,
    const std::array<LodMeshBinding, 3>& lod_meshes) {
    GpuCullingOutput output;
    output.visible_instances_lod0.reserve(instances.size());
    output.visible_instances_lod1.reserve(instances.size() / 2u);
    output.visible_instances_lod2.reserve(instances.size() / 4u);

    const float cam_x = static_cast<float>(camera.center.x);
    const float cam_y = static_cast<float>(camera.center.y);
    const float cam_z = static_cast<float>(camera.altitude_m);

    const double far_lod_dist_sq_d = double(config.far_lod_dist) * double(config.far_lod_dist);
    const double near_lod_dist_sq_d = double(config.near_lod_dist) * double(config.near_lod_dist);
    const double med_lod_dist_sq_d = double(config.med_lod_dist) * double(config.med_lod_dist);

    for (const auto& inst : instances) {
        const float world_x = static_cast<float>(chunk_origin_x) + static_cast<float>(inst.local_x_m);
        const float world_y = static_cast<float>(chunk_origin_y) + static_cast<float>(inst.local_y_m);
        // inst.local_z_half_m is already half-height; do not halve again (fixes half-buried objects)
        const float world_z = static_cast<float>(inst.local_z_half_m);

        const double dx = double(world_x) - double(cam_x);
        const double dy = double(world_y) - double(cam_y);
        const double dz = double(world_z) - double(cam_z);
        const double dist_sq_d = dx * dx + dy * dy + dz * dz;

        // Distance Cull (use double precision to avoid float jitter)
        if (config.distance_culling_enabled && dist_sq_d > far_lod_dist_sq_d) {
            ++output.total_culled_instances;
            continue;
        }

        // Frustum Cull (approximate bounding sphere based on scale)
        const float radius = 50.0f * (static_cast<float>(inst.scale_milli) / 1000.0f);
        if (config.frustum_culling_enabled && !frustum.intersects_sphere(world_x, world_y, world_z, radius)) {
            ++output.total_culled_instances;
            continue;
        }

        // LOD Assignment via squared distance (use double for precision)
        const double dist_sq_for_lod = dist_sq_d;
        if (dist_sq_for_lod < near_lod_dist_sq_d) {
            output.visible_instances_lod0.push_back(inst);
        } else if (dist_sq_for_lod < med_lod_dist_sq_d) {
            output.visible_instances_lod1.push_back(inst);
        } else {
            output.visible_instances_lod2.push_back(inst);
        }
        ++output.total_visible_instances;
    }

    // Build 3 indirect draw commands (one for each LOD mesh)
    output.indirect_commands.resize(3);

    // LOD 0
    output.indirect_commands[0] = {
        .index_count = lod_meshes[0].index_count,
        .instance_count = static_cast<std::uint32_t>(output.visible_instances_lod0.size()),
        .first_index = lod_meshes[0].first_index,
        .vertex_offset = lod_meshes[0].vertex_offset,
        .first_instance = 0u
    };

    // LOD 1
    output.indirect_commands[1] = {
        .index_count = lod_meshes[1].index_count,
        .instance_count = static_cast<std::uint32_t>(output.visible_instances_lod1.size()),
        .first_index = lod_meshes[1].first_index,
        .vertex_offset = lod_meshes[1].vertex_offset,
        .first_instance = static_cast<std::uint32_t>(output.visible_instances_lod0.size())
    };

    // LOD 2
    output.indirect_commands[2] = {
        .index_count = lod_meshes[2].index_count,
        .instance_count = static_cast<std::uint32_t>(output.visible_instances_lod2.size()),
        .first_index = lod_meshes[2].first_index,
        .vertex_offset = lod_meshes[2].vertex_offset,
        .first_instance = static_cast<std::uint32_t>(output.visible_instances_lod0.size() +
                                                     output.visible_instances_lod1.size())
    };

    return output;
}

} // namespace thunder
