#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/simulation/living/LivingMapTypes.hpp"
#include "thunder/simulation/living/ProvinceEntityIndex.hpp"
#include "thunder/simulation/world/SpatialPlacement.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {
class World;

struct LivingMapUpdateStats {
    std::size_t dirty_provinces = 0;
    std::size_t dirty_chunks = 0;
    std::size_t total_instances = 0;
    std::size_t upload_bytes = 0;
    std::size_t workers_used = 1;
};

class LivingMapSystem {
public:
    static constexpr std::int64_t chunk_size_m = 64'000;

    void set_provinces(std::span<const ProvinceVisualDefinition> definitions);
    void set_spatial_placement(SpatialPlacementDatabase placement);
    void set_building_visual(BuildingTypeId type, BuildingVisualKind kind, std::uint16_t asset_family = 0u);
    void rebuild_spatial_index(const World& world);
    LivingMapUpdateStats update(const World& world, JobSystem& jobs);

    [[nodiscard]] std::size_t province_count() const noexcept { return province_defs_.size(); }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunk_keys_.size(); }
    [[nodiscard]] LivingChunkKey chunk_key(std::size_t index) const;
    [[nodiscard]] std::size_t find_chunk_index(LivingChunkKey key) const noexcept;
    [[nodiscard]] std::span<const LivingInstanceGpu> chunk_instances(std::size_t index) const;
    [[nodiscard]] std::span<const LivingClusterGpu> chunk_clusters(std::size_t index) const;
    [[nodiscard]] std::uint32_t chunk_version(std::size_t index) const;
    [[nodiscard]] const ProvinceVisualAggregate& province_aggregate(ProvinceId province) const;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] const ProvinceEntityIndex& spatial_index() const noexcept { return spatial_index_; }
    [[nodiscard]] const SpatialPlacementDatabase& spatial_placement() const noexcept { return placement_; }

private:
    struct BuildingVisualRecord {
        BuildingVisualKind kind = BuildingVisualKind::None;
        std::uint16_t asset_family = 0u;
    };
    struct PlacedInstance {
        LivingChunkKey chunk{};
        LivingInstanceGpu gpu{};
    };
    struct ProvinceCache {
        std::uint64_t signature = 0u;
        ProvinceVisualAggregate aggregate{};
        std::vector<PlacedInstance> instances;
        LivingClusterGpu cluster{};
        LivingChunkKey cluster_chunk{};
    };
    struct ChunkCache {
        std::vector<LivingInstanceGpu> instances;
        std::vector<LivingClusterGpu> clusters;
        std::uint32_t version = 0u;
    };

    [[nodiscard]] static LivingChunkKey make_chunk_key(double x_m, double y_m) noexcept;
    [[nodiscard]] static LivingChunkKey living_key(SpatialChunkKey key) noexcept { return {key.x, key.y}; }
    [[nodiscard]] static std::uint16_t province_r16(ProvinceId province);
    [[nodiscard]] std::uint64_t make_signature(const ProvinceVisualAggregate& aggregate) const noexcept;
    void build_chunk_layout();
    void generate_province(ProvinceId province, ProvinceCache& cache) const;
    void rebuild_chunk(std::size_t chunk_index);
    void mark_province_chunks_dirty(std::size_t province_index);

    std::vector<ProvinceVisualDefinition> province_defs_;
    std::vector<BuildingVisualRecord> building_visuals_;
    SpatialPlacementDatabase placement_;
    ProvinceEntityIndex spatial_index_;
    std::vector<ProvinceCache> province_cache_;
    std::vector<LivingChunkKey> chunk_keys_;
    std::vector<std::uint32_t> province_chunk_offsets_;
    std::vector<std::uint32_t> province_chunk_indices_;
    std::vector<std::uint32_t> chunk_province_offsets_;
    std::vector<ProvinceId> chunk_provinces_;
    std::vector<ChunkCache> chunk_cache_;
    bool spatial_index_dirty_ = true;
    std::vector<std::uint32_t> dirty_chunk_epoch_;
    std::vector<std::uint32_t> dirty_chunk_indices_;
    std::uint32_t update_epoch_ = 1u;
    std::size_t total_instances_ = 0u;
};

} // namespace thunder
