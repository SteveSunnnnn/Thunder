#include "thunder/presentation/render/terrain/TerrainPageCache.hpp"

#include <stdexcept>

namespace thunder {

std::size_t TerrainPatchKeyHash::operator()(const TerrainPatchKey& key) const noexcept {
    std::uint64_t x = static_cast<std::uint32_t>(key.x);
    std::uint64_t y = static_cast<std::uint32_t>(key.y);
    std::uint64_t h = (x << 32u) ^ y ^ (static_cast<std::uint64_t>(key.level) * 0x9e3779b97f4a7c15ull);
    h ^= h >> 30u;
    h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27u;
    h *= 0x94d049bb133111ebull;
    h ^= h >> 31u;
    return static_cast<std::size_t>(h);
}

TerrainPageCache::TerrainPageCache(std::uint32_t page_capacity) : pages_(page_capacity) {
    if (page_capacity == 0u) throw std::invalid_argument("terrain page cache capacity must be non-zero");
    lookup_.reserve(static_cast<std::size_t>(page_capacity) * 2u);
    lookup_.max_load_factor(0.70f);
}

std::optional<std::uint32_t> TerrainPageCache::find(TerrainPatchKey key) const noexcept {
    const auto it = lookup_.find(key);
    if (it == lookup_.end()) return std::nullopt;
    return it->second;
}

void TerrainPageCache::touch(TerrainPatchKey key, std::uint64_t frame) noexcept {
    const auto it = lookup_.find(key);
    if (it != lookup_.end()) pages_[it->second].last_used_frame = frame;
}

TerrainPageAllocation TerrainPageCache::allocate(TerrainPatchKey key, std::uint64_t frame) {
    if (const auto existing = find(key)) {
        pages_[*existing].last_used_frame = frame;
        return {*existing, std::nullopt};
    }

    if (resident_count_ < pages_.size()) {
        for (std::uint32_t i = 0; i < pages_.size(); ++i) {
            if (!pages_[i].occupied) {
                pages_[i] = {key, frame, true};
                lookup_.emplace(key, i);
                ++resident_count_;
                next_victim_ = (i + 1u) % static_cast<std::uint32_t>(pages_.size());
                return {i, std::nullopt};
            }
        }
    }

    // Bounded approximate-LRU scan. Exact full-cache LRU makes every streamed
    // page O(cache_capacity); a 64-page clock window keeps allocation cost
    // bounded while still strongly preferring cold pages.
    constexpr std::uint32_t victim_scan_window = 64u;
    const auto scan_count = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(pages_.size()), victim_scan_window);
    std::uint32_t victim = next_victim_;
    std::uint64_t oldest = pages_[victim].last_used_frame;
    for (std::uint32_t scanned = 1u; scanned < scan_count; ++scanned) {
        const std::uint32_t candidate = (next_victim_ + scanned) % static_cast<std::uint32_t>(pages_.size());
        if (pages_[candidate].last_used_frame < oldest) {
            victim = candidate;
            oldest = pages_[candidate].last_used_frame;
        }
    }

    const TerrainPatchKey evicted = pages_[victim].key;
    lookup_.erase(evicted);
    pages_[victim] = {key, frame, true};
    lookup_.emplace(key, victim);
    next_victim_ = (victim + 1u) % static_cast<std::uint32_t>(pages_.size());
    return {victim, evicted};
}

} // namespace thunder
