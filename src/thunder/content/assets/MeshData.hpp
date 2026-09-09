#pragma once
#include "thunder/content/assets/AssetPack.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder::assets {

// Engine runtime mesh layout, baked by `encode_quantized_mesh` from imported
// geometry. Implements the storage contract of
// docs/3D_ASSET_SPECIFICATION_AND_INVENTORY.md §1.2:
//   - positions as 3 × float32 chunk-local coordinates (chunk origin is a
//     placement-time concern, not baked into the asset);
//   - normals octahedral-encoded into 2 × int8 (snorm range, ±127);
//   - UVs as 2 × float16;
//   - indices uint16 when the vertex count fits, else uint32.
// The payload is an AssetPack Mesh-kind entry; layout constants live here so
// cooker, streaming and render agree on one definition.

inline constexpr char mesh_payload_magic[4] = {'T', 'H', 'M', '1'};
inline constexpr std::uint32_t mesh_flag_has_normals = 1u << 0u;
inline constexpr std::uint32_t mesh_flag_has_uvs = 1u << 1u;

struct MeshBounds {
    std::array<float, 3> min{0.0f, 0.0f, 0.0f};
    std::array<float, 3> max{0.0f, 0.0f, 0.0f};
};

// Import-time geometry: triangles only, authored scale in metres, Y-up.
struct MeshGeometry {
    std::vector<float> positions;      // 3 per vertex
    std::vector<float> normals;        // 3 per vertex (optional)
    std::vector<float> uvs;            // 2 per vertex (optional)
    std::vector<std::uint32_t> indices;
    MeshBounds bounds{};

    [[nodiscard]] std::uint32_t vertex_count() const noexcept {
        return static_cast<std::uint32_t>(positions.size() / 3u);
    }
    [[nodiscard]] std::uint32_t index_count() const noexcept {
        return static_cast<std::uint32_t>(indices.size());
    }
    void recompute_bounds() noexcept;
};

// Encoded-view helpers: zero-copy accessors over a cooked payload.
struct QuantizedMeshView {
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    bool has_normals = false;
    bool has_uvs = false;
    bool index_width_32 = false;
    MeshBounds bounds{};
    std::span<const std::byte> payload;

    [[nodiscard]] std::array<float, 3> position(std::uint32_t vertex) const noexcept;
    [[nodiscard]] std::array<float, 3> normal(std::uint32_t vertex) const noexcept;
    [[nodiscard]] std::array<float, 2> uv(std::uint32_t vertex) const noexcept;
    [[nodiscard]] std::uint32_t index(std::uint32_t i) const noexcept;
};

// Validates the payload header and bounds; throws std::runtime_error on any
// malformation (bad magic, truncated sections, count mismatch).
[[nodiscard]] QuantizedMeshView decode_quantized_mesh(std::span<const std::byte> payload);
[[nodiscard]] std::vector<std::byte> encode_quantized_mesh(const MeshGeometry& geometry);

// Octahedral normal encoding shared with the importer tests. Component range
// is [-127, 127]; decode normalises the reconstructed vector.
[[nodiscard]] std::array<std::int8_t, 2> encode_octahedral_normal(
    float x, float y, float z) noexcept;
[[nodiscard]] std::array<float, 3> decode_octahedral_normal(
    std::int8_t x, std::int8_t y) noexcept;

} // namespace thunder::assets
