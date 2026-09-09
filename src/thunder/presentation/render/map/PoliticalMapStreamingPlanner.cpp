#include "thunder/presentation/render/map/PoliticalMapStreamingPlanner.hpp"

#include <algorithm>

namespace thunder {

std::span<const PoliticalMapPageUploadRequest> PoliticalMapStreamingPlanner::plan(
    std::span<const TerrainPatchInstance> visible,
    TerrainPageCache& resident_bundles,
    std::uint64_t frame,
    std::uint64_t byte_budget) {
    requests_.clear();
    if (requests_.capacity() < visible.size()) requests_.reserve(visible.size());

    for (const auto& patch : visible) {
        if (resident_bundles.resident(patch.key)) {
            resident_bundles.touch(patch.key, frame);
            continue;
        }
        // Political/coast detail matters most near the camera and at lower LOD levels.
        const float priority = static_cast<float>(patch.key.level) * 16.0f + patch.morph;
        requests_.push_back({patch.key, bundle_bytes, priority});
    }

    const std::size_t page_limit = static_cast<std::size_t>(byte_budget / bundle_bytes);
    if (requests_.size() > page_limit) {
        std::partial_sort(requests_.begin(), requests_.begin() + page_limit, requests_.end(),
                          [](const auto& a, const auto& b) {
            return a.priority < b.priority;
        });
        requests_.resize(page_limit);
    } else {
        std::sort(requests_.begin(), requests_.end(), [](const auto& a, const auto& b) {
            return a.priority < b.priority;
        });
    }
    return requests_;
}

} // namespace thunder
