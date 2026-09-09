#pragma once

#include "thunder/presentation/render/terrain/TerrainPageCache.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct PoliticalMapPageUploadRequest {
    TerrainPatchKey key{};
    std::uint32_t estimated_bytes = 0;
    float priority = 0.0f;
};

// Province-ID + coast-distance pages are uploaded as an atomic 64 KiB bundle so
// shader layers never observe mismatched map generations during camera movement.
class PoliticalMapStreamingPlanner {
public:
    static constexpr std::uint32_t province_id_page_bytes = 32u * 1024u;
    static constexpr std::uint32_t coast_distance_page_bytes = 32u * 1024u;
    static constexpr std::uint32_t bundle_bytes = province_id_page_bytes + coast_distance_page_bytes;

    void reserve(std::size_t count) { requests_.reserve(count); }
    [[nodiscard]] std::span<const PoliticalMapPageUploadRequest> plan(
        std::span<const TerrainPatchInstance> visible,
        TerrainPageCache& resident_bundles,
        std::uint64_t frame,
        std::uint64_t byte_budget);

private:
    std::vector<PoliticalMapPageUploadRequest> requests_;
};

} // namespace thunder
