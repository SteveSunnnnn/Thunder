#pragma once
#include "thunder/simulation/living/LivingMapSystem.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum class LivingLod : std::uint8_t { FarClusters = 0, MediumClusters = 1, NearInstances = 2 };

struct LivingChunkDrawRequest {
    std::uint32_t chunk_index = 0u;
    LivingLod lod = LivingLod::FarClusters;
    std::uint32_t version = 0u;
    std::uint32_t payload_bytes = 0u;
};

struct LivingStreamingPlan {
    std::vector<LivingChunkDrawRequest> visible;
    std::size_t stale_upload_bytes = 0u;
    std::size_t near_instance_count = 0u;
};

struct LivingStreamingSettings {
    double near_radius_m = 180'000.0;
    double medium_radius_m = 450'000.0;
    double far_radius_m = 1'100'000.0;
    std::size_t max_near_instances = 350'000u;
};

class LivingMapStreamingPlanner {
public:
    LivingStreamingPlan build(const LivingMapSystem& map,
                              double camera_x_m,
                              double camera_y_m,
                              std::span<const std::uint32_t> resident_versions,
                              LivingStreamingSettings settings = {});
};

} // namespace thunder
