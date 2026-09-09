#pragma once

#include "thunder/presentation/render/RenderSnapshotData.hpp"
#include "thunder/simulation/kernel/World.hpp"

namespace thunder {

// Simulation-to-presentation bridge. RenderSnapshotData.hpp intentionally has
// no World dependency; only this builder knows how to read authoritative state.
void build_render_snapshot(const World& world, RenderSnapshot& out,
                           std::uint64_t generation = 0,
                           std::uint64_t world_checksum = 0);
[[nodiscard]] RenderSnapshot build_render_snapshot(const World& world);

} // namespace thunder
