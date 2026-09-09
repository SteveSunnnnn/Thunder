#pragma once

#include "thunder/presentation/render/terrain/TerrainClipmap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace thunder {

struct TerrainPatchKeyHash {
    [[nodiscard]] std::size_t operator()(const TerrainPatchKey& key) const noexcept;
};

struct TerrainPageAllocation {
    std::uint32_t page = 0;
    std::optional<TerrainPatchKey> evicted;
};

class TerrainPageCache {
public:
    explicit TerrainPageCache(std::uint32_t page_capacity);

    [[nodiscard]] std::optional<std::uint32_t> find(TerrainPatchKey key) const noexcept;
    [[nodiscard]] bool resident(TerrainPatchKey key) const noexcept { return find(key).has_value(); }
    void touch(TerrainPatchKey key, std::uint64_t frame) noexcept;
    [[nodiscard]] TerrainPageAllocation allocate(TerrainPatchKey key, std::uint64_t frame);

    [[nodiscard]] std::uint32_t capacity() const noexcept { return static_cast<std::uint32_t>(pages_.size()); }
    [[nodiscard]] std::uint32_t resident_count() const noexcept { return resident_count_; }

private:
    struct Page {
        TerrainPatchKey key{};
        std::uint64_t last_used_frame = 0;
        bool occupied = false;
    };

    std::vector<Page> pages_;
    std::unordered_map<TerrainPatchKey, std::uint32_t, TerrainPatchKeyHash> lookup_;
    std::uint32_t next_victim_ = 0;
    std::uint32_t resident_count_ = 0;
};

} // namespace thunder
