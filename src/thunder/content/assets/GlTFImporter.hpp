#pragma once
#include "thunder/content/assets/MeshData.hpp"
#include <span>
#include <vector>

namespace thunder::assets {

// Minimal glTF 2.0 Binary (.glb) importer for static triangle meshes — the
// delivery front-end of docs/3D_ASSET_SPECIFICATION_AND_INVENTORY.md §1.1.
// Scope (phase 1, matches the first architecture-kit pipeline):
//   - .glb container (JSON chunk + optional BIN chunk), glTF 2.0 only;
//   - meshes[0] primitives, triangle-list mode (4) only, concatenated;
//   - attributes POSITION (required), NORMAL, TEXCOORD_0 (optional);
//   - component types: int8/uint8/int16/uint16/uint32/float32, normalized
//     accessors honoured, interleaved buffer views honoured;
//   - no nodes/scenes traversal, no skins/animations, no sparse accessors —
//     rejected with diagnostics rather than misread.
// Authored units stay metres / Y-up; chunk-local rebasing happens later in the
// placement stage.
[[nodiscard]] std::vector<MeshGeometry> import_glb_meshes(std::span<const std::byte> glb_bytes);

// Cooker entry point: import + quantized encode of the first mesh, ready to be
// packed as an AssetPack Mesh-kind payload.
[[nodiscard]] std::vector<std::byte> cook_glb_mesh_payload(std::span<const std::byte> glb_bytes);

} // namespace thunder::assets
