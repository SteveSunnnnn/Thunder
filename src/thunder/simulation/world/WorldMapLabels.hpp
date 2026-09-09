#pragma once

#include "thunder/content/worldpack/WorldPack.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thunder {

enum class WorldMapLabelKind : std::uint8_t {
    Country = 0,
    Ocean = 1,
    Area = 2,
    Province = 3,
    Location = 4,
    Geographic = 5,
    Count = 6,
};

struct WorldMapLabelPoint {
    float u = 0.0f;
    float v = 0.0f;
};

struct WorldMapLabelRecord {
    std::string key;
    std::string text;
    WorldMapLabelKind kind = WorldMapLabelKind::Location;
    std::uint8_t priority = 0u;
    float minimum_zoom = 0.0f;
    float maximum_zoom = 1.0f;
    double geographic_area_km2 = 0.0;
    std::vector<WorldMapLabelPoint> spine;
};

class WorldMapLabels {
public:
    void clear() noexcept { records_.clear(); }
    void load_from_worldpack(const WorldPackReader& pack);

    [[nodiscard]] std::span<const WorldMapLabelRecord> records() const noexcept {
        return records_;
    }
    [[nodiscard]] bool validate() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    std::vector<WorldMapLabelRecord> records_;
};

} // namespace thunder

