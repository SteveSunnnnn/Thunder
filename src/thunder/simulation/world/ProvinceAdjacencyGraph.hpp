#pragma once

#include "thunder/foundation/base/StrongId.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum ProvinceAdjacencyFlag : std::uint16_t {
    AdjacencyLand = 1u << 0u,
    AdjacencyRiver = 1u << 1u,
    AdjacencyStrait = 1u << 2u,
    AdjacencyImpassable = 1u << 3u,
    AdjacencySea = 1u << 4u,
    AdjacencyCanal = 1u << 5u,
    AdjacencyLake = 1u << 6u
};

struct ProvinceAdjacencyInput {
    ProvinceId a{};
    ProvinceId b{};
    std::uint16_t flags = AdjacencyLand;
    std::uint16_t base_cost_q8 = 256u;
};

struct ProvinceNeighbor {
    std::uint32_t province = 0xffffffffu;
    std::uint16_t flags = 0u;
    std::uint16_t base_cost_q8 = 256u;

    [[nodiscard]] ProvinceId id() const noexcept { return ProvinceId{province}; }
};
static_assert(sizeof(ProvinceNeighbor) == 8u);

// Immutable CSR topology compiled by the world compiler. Runtime neighbor scans
// become two offset loads plus a contiguous span; no per-province vectors, heap
// chasing or hash lookup in pathfinding/border hot paths.
class ProvinceAdjacencyGraph {
public:
    void build(std::uint32_t province_count, std::span<const ProvinceAdjacencyInput> edges);
    void load_csr(std::span<const std::uint32_t> offsets, std::span<const ProvinceNeighbor> neighbors);

    [[nodiscard]] std::uint32_t province_count() const noexcept {
        return offsets_.empty() ? 0u : static_cast<std::uint32_t>(offsets_.size() - 1u);
    }
    [[nodiscard]] std::span<const ProvinceNeighbor> neighbors(ProvinceId province) const noexcept;
    [[nodiscard]] bool adjacent(ProvinceId a, ProvinceId b) const noexcept;
    [[nodiscard]] bool is_symmetric() const noexcept;
    [[nodiscard]] std::size_t directed_edge_count() const noexcept { return neighbors_.size(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return offsets_.size() * sizeof(std::uint32_t) + neighbors_.size() * sizeof(ProvinceNeighbor);
    }

private:
    std::vector<std::uint32_t> offsets_;
    std::vector<ProvinceNeighbor> neighbors_;
};

} // namespace thunder
