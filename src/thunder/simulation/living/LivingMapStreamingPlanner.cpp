#include "thunder/simulation/living/LivingMapStreamingPlanner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace thunder {
namespace {
std::int32_t floor_chunk(double value) noexcept {
    return static_cast<std::int32_t>(std::floor(value / static_cast<double>(LivingMapSystem::chunk_size_m)));
}

double chunk_center(std::int32_t coordinate) noexcept {
    return (static_cast<double>(coordinate) + 0.5) * static_cast<double>(LivingMapSystem::chunk_size_m);
}
}

LivingStreamingPlan LivingMapStreamingPlanner::build(const LivingMapSystem& map,
                                                       double camera_x_m,
                                                       double camera_y_m,
                                                       std::span<const std::uint32_t> resident_versions,
                                                       LivingStreamingSettings settings) {
    LivingStreamingPlan plan;
    if (map.chunk_count() == 0u || settings.far_radius_m <= 0.0) return plan;
    const auto camera_chunk_x = floor_chunk(camera_x_m);
    const auto camera_chunk_y = floor_chunk(camera_y_m);
    const auto chunk_radius = static_cast<std::int32_t>(std::ceil(settings.far_radius_m / static_cast<double>(LivingMapSystem::chunk_size_m))) + 1;
    const double near2 = settings.near_radius_m * settings.near_radius_m;
    const double medium2 = settings.medium_radius_m * settings.medium_radius_m;
    const double far2 = settings.far_radius_m * settings.far_radius_m;

    struct Candidate { std::size_t chunk_index; double d2; };
    std::vector<Candidate> candidates;
    const auto diameter = static_cast<std::size_t>(chunk_radius * 2 + 1);
    candidates.reserve(diameter * diameter);
    for (std::int32_t y = camera_chunk_y - chunk_radius; y <= camera_chunk_y + chunk_radius; ++y) {
        for (std::int32_t x = camera_chunk_x - chunk_radius; x <= camera_chunk_x + chunk_radius; ++x) {
            const auto dx = chunk_center(x) - camera_x_m;
            const auto dy = chunk_center(y) - camera_y_m;
            const auto d2 = dx * dx + dy * dy;
            if (d2 > far2) continue;
            const auto index = map.find_chunk_index(LivingChunkKey{x, y});
            if (index == map.chunk_count()) continue;
            candidates.push_back(Candidate{index, d2});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.d2 != b.d2) return a.d2 < b.d2;
        return a.chunk_index < b.chunk_index;
    });
    plan.visible.reserve(candidates.size());
    std::size_t near_budget = settings.max_near_instances;
    for (const auto candidate : candidates) {
        LivingChunkDrawRequest request;
        request.chunk_index = static_cast<std::uint32_t>(candidate.chunk_index);
        request.version = map.chunk_version(candidate.chunk_index);
        const auto instance_count = map.chunk_instances(candidate.chunk_index).size();
        const auto cluster_count = map.chunk_clusters(candidate.chunk_index).size();
        if (candidate.d2 <= near2 && instance_count <= near_budget) {
            request.lod = LivingLod::NearInstances;
            request.payload_bytes = static_cast<std::uint32_t>(std::min<std::size_t>(std::numeric_limits<std::uint32_t>::max(),
                instance_count * sizeof(LivingInstanceGpu) + cluster_count * sizeof(LivingClusterGpu)));
            near_budget -= instance_count;
            plan.near_instance_count += instance_count;
        } else if (candidate.d2 <= medium2) {
            request.lod = LivingLod::MediumClusters;
            request.payload_bytes = static_cast<std::uint32_t>(cluster_count * sizeof(LivingClusterGpu));
        } else {
            request.lod = LivingLod::FarClusters;
            request.payload_bytes = static_cast<std::uint32_t>(cluster_count * sizeof(LivingClusterGpu));
        }
        const auto resident = candidate.chunk_index < resident_versions.size() ? resident_versions[candidate.chunk_index] : 0u;
        if (resident != request.version) plan.stale_upload_bytes += request.payload_bytes;
        plan.visible.push_back(request);
    }
    return plan;
}

} // namespace thunder
