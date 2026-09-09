#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {

struct ArchitectureRegionRecord {
    ProvinceId province{};
    StateId state{};
    std::uint64_t region_key = 0;
    std::uint64_t family_key = 0;
};

struct ResourceDistributionRecord {
    StateId state{};
    std::uint64_t resource_key = 0;
    double capacity = 0.0;
};

// Offline-authored map layers that are neither mutable simulation state nor
// graphics resources. WorldBootstrap owns one value and the renderer/living
// systems receive read-only views of it.
struct WorldStaticLayers {
    std::vector<WorldChunkKey> rivers;
    std::vector<WorldChunkKey> transports;
    std::vector<ArchitectureRegionRecord> architecture_regions;
    std::vector<ResourceDistributionRecord> resource_distribution;

    [[nodiscard]] bool validate(std::size_t province_count,
                                std::size_t state_count) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
};

// Decodes only the binary static-layer contracts emitted by the offline GIS
// compiler. No GeoJSON, shapefile or renderer API is allowed past this point.
[[nodiscard]] WorldStaticLayers load_world_static_layers(
    const WorldPackReader& pack,
    std::size_t province_count,
    std::size_t state_count);

} // namespace thunder
