#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/living/LivingMapTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum class TransportKind : std::uint8_t { Road = 0, Rail = 1, Canal = 2 };

struct TransportLinkDefinition {
    ProvinceId a{};
    ProvinceId b{};
    TransportKind kind = TransportKind::Road;
    std::uint8_t level = 1u;
    std::uint16_t flags = 0u;
};

// Terrain height is sampled in the shader; transport topology only needs XY.
// Payload is 16 bytes and can be drawn as GPU-generated ribbons/rails.
struct TransportSegmentGpu {
    std::uint16_t x0_m = 0;
    std::uint16_t y0_m = 0;
    std::uint16_t x1_m = 0;
    std::uint16_t y1_m = 0;
    std::uint16_t province_a_r16 = 0;
    std::uint16_t province_b_r16 = 0;
    std::uint8_t kind = 0;
    std::uint8_t level = 1;
    std::uint16_t flags = 0;
};
static_assert(sizeof(TransportSegmentGpu) == 16u);

class TransportNetwork {
public:
    void build(std::span<const ProvinceVisualDefinition> provinces,
               std::span<const TransportLinkDefinition> links);
    void set_link_level(std::size_t link_index, std::uint8_t level);

    [[nodiscard]] std::size_t link_count() const noexcept { return links_.size(); }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunk_keys_.size(); }
    [[nodiscard]] std::size_t last_build_candidate_chunks() const noexcept { return last_build_candidate_chunks_; }
    [[nodiscard]] LivingChunkKey chunk_key(std::size_t index) const;
    [[nodiscard]] std::span<const TransportSegmentGpu> chunk_segments(std::size_t index) const;
    [[nodiscard]] std::uint32_t chunk_version(std::size_t index) const;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    std::vector<TransportLinkDefinition> links_;
    std::vector<LivingChunkKey> chunk_keys_;
    std::vector<std::uint32_t> chunk_offsets_;
    std::vector<TransportSegmentGpu> segments_;
    std::vector<std::uint32_t> segment_chunk_indices_;
    std::vector<std::uint32_t> chunk_versions_;
    std::vector<std::uint32_t> link_segment_offsets_;
    std::vector<std::uint32_t> link_segment_indices_;
    std::size_t last_build_candidate_chunks_ = 0u;
};

} // namespace thunder
