#include "thunder/presentation/render/terrain/TerrainStreamingPlanner.hpp"

#include <algorithm>

namespace thunder {

std::span<const TerrainUploadRequest> TerrainStreamingPlanner::plan(std::span<const TerrainPatchInstance> visible,
                                                                    TerrainPageCache& cache,
                                                                    std::uint64_t frame,
                                                                    std::uint64_t byte_budget) {
    requests_.clear();
    if (requests_.capacity() < visible.size()) requests_.reserve(visible.size());

    constexpr std::uint32_t page_bytes = static_cast<std::uint32_t>(TerrainHeightPage::sample_count * sizeof(std::uint16_t));
    for (const auto& patch : visible) {
        if (cache.resident(patch.key)) {
            cache.touch(patch.key, frame);
            continue;
        }
        // Lower LOD number and smaller morph distance are more urgent.
        const float lod_weight = static_cast<float>(patch.key.level) * 10.0f;
        requests_.push_back({patch.key, page_bytes, lod_weight + patch.morph});
    }

    const std::size_t max_pages = std::max<std::size_t>(1u, static_cast<std::size_t>((byte_budget + page_bytes - 1u) / page_bytes));
    if (requests_.size() > max_pages) {
        std::partial_sort(requests_.begin(), requests_.begin() + max_pages, requests_.end(),
                          [](const TerrainUploadRequest& a, const TerrainUploadRequest& b) {
            return a.priority < b.priority;
        });
        requests_.resize(max_pages);
    } else {
        std::sort(requests_.begin(), requests_.end(), [](const TerrainUploadRequest& a, const TerrainUploadRequest& b) {
            return a.priority < b.priority;
        });
    }

    std::uint64_t used = 0;
    std::size_t keep = 0;
    for (; keep < requests_.size(); ++keep) {
        const std::uint64_t next = used + requests_[keep].estimated_bytes;
        // Always admit the first request even if it alone exceeds the budget,
        // otherwise a single oversized page starves streaming entirely: the
        // two guards below used to be equivalent, so keep stayed 0 and no
        // page was ever uploaded.
        if (next > byte_budget && keep > 0u) break;
        used = next;
    }
    requests_.resize(keep);
    return requests_;
}

} // namespace thunder
