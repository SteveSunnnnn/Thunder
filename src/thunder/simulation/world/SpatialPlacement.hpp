#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct SpatialChunkKey {
    std::int32_t x = 0;
    std::int32_t y = 0;
    friend constexpr bool operator==(SpatialChunkKey, SpatialChunkKey) = default;
    friend constexpr auto operator<=>(SpatialChunkKey, SpatialChunkKey) = default;
};

enum class PlacementClass : std::uint8_t {
    Buildable = 0,
    Urban,
    Rural,
    Industrial,
    Farm,
    Mine,
    Port,
    Vegetation,
    Count,
};

enum PlacementCandidateFlag : std::uint8_t {
    PlacementBuildable = 1u << 0u,
    PlacementCoastal = 1u << 1u,
};

struct PlacementCandidate {
    ProvinceId province{};
    SpatialChunkKey chunk{};
    std::uint16_t local_x_m = 0;
    std::uint16_t local_y_m = 0;
    std::int16_t local_z_half_m = 0;
    PlacementClass placement_class = PlacementClass::Buildable;
    std::uint8_t flags = PlacementBuildable;
    std::uint16_t weight = 1u;
};

struct SettlementAnchor {
    ProvinceId province{};
    SpatialChunkKey chunk{};
    std::uint16_t local_x_m = 0;
    std::uint16_t local_y_m = 0;
    std::int16_t local_z_half_m = 0;
    std::uint16_t importance = 0;
    std::uint64_t key_hash = 0;
};

// Local records used by offline compilers/tests. The WorldChunkKey carries the
// 64-km chunk coordinate, so it is intentionally absent from the wire record.
struct PlacementCandidateLocal {
    ProvinceId province{};
    std::uint16_t local_x_m = 0;
    std::uint16_t local_y_m = 0;
    std::int16_t local_z_half_m = 0;
    PlacementClass placement_class = PlacementClass::Buildable;
    std::uint8_t flags = PlacementBuildable;
    std::uint16_t weight = 1u;
};

struct SettlementAnchorLocal {
    ProvinceId province{};
    std::uint16_t local_x_m = 0;
    std::uint16_t local_y_m = 0;
    std::int16_t local_z_half_m = 0;
    std::uint16_t importance = 0;
    std::uint64_t key_hash = 0;
};

class SpatialPlacementWire {
public:
    [[nodiscard]] static std::vector<std::byte> placement_candidates(std::span<const PlacementCandidateLocal> records);
    [[nodiscard]] static std::vector<std::byte> settlement_anchors(std::span<const SettlementAnchorLocal> records);
};

class SpatialPlacementDatabase {
public:
    static constexpr std::int32_t chunk_size_m = 64'000;
    static constexpr std::size_t placement_class_count = static_cast<std::size_t>(PlacementClass::Count);

    void clear() noexcept;
    void load_from_worldpack(const WorldPackReader& pack, std::size_t province_count);
    void build(std::size_t province_count, std::span<const PlacementCandidate> candidates,
               std::span<const SettlementAnchor> anchors);

    [[nodiscard]] std::size_t province_count() const noexcept { return province_count_; }
    [[nodiscard]] std::size_t candidate_count() const noexcept { return candidates_.size(); }
    [[nodiscard]] std::size_t anchor_count() const noexcept { return anchors_.size(); }
    [[nodiscard]] bool empty() const noexcept { return candidates_.empty() && anchors_.empty(); }
    [[nodiscard]] std::span<const PlacementCandidate> candidates(ProvinceId province, PlacementClass cls) const noexcept;
    [[nodiscard]] std::span<const SettlementAnchor> anchors(ProvinceId province) const noexcept;
    [[nodiscard]] std::span<const SpatialChunkKey> chunks(ProvinceId province) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    struct Range { std::uint32_t begin = 0; std::uint32_t end = 0; };
    [[nodiscard]] std::size_t range_index(std::size_t province, PlacementClass cls) const noexcept;
    void rebuild_indices();

    std::size_t province_count_ = 0;
    std::vector<PlacementCandidate> candidates_;
    std::vector<SettlementAnchor> anchors_;
    std::vector<Range> candidate_ranges_;
    std::vector<std::uint32_t> anchor_offsets_;
    std::vector<std::uint32_t> province_chunk_offsets_;
    std::vector<SpatialChunkKey> province_chunks_;
};

} // namespace thunder
