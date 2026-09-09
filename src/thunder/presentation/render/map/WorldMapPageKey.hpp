#pragma once

#include <cstddef>
#include <cstdint>

namespace thunder {

// Stable identity for one virtual world-map page.  This belongs to the map
// stream rather than terrain geometry: political, coast, height and mask
// payloads all share the same page coordinate, while the terrain clipmap is
// free to choose a different mesh/layout for rendering.
struct WorldMapPageKey {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint16_t level = 0;

    friend bool operator==(const WorldMapPageKey&, const WorldMapPageKey&) = default;
};

struct WorldMapPageKeyHash {
    [[nodiscard]] std::size_t operator()(const WorldMapPageKey& key) const noexcept {
        std::uint64_t x = static_cast<std::uint32_t>(key.x);
        std::uint64_t y = static_cast<std::uint32_t>(key.y);
        std::uint64_t h = (x << 32u) ^ y ^
                          (static_cast<std::uint64_t>(key.level) * 0x9e3779b97f4a7c15ull);
        h ^= h >> 30u;
        h *= 0xbf58476d1ce4e5b9ull;
        h ^= h >> 27u;
        h *= 0x94d049bb133111ebull;
        h ^= h >> 31u;
        return static_cast<std::size_t>(h);
    }
};

} // namespace thunder
