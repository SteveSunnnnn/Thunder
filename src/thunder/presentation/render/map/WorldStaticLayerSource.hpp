#pragma once

#include "thunder/presentation/render/map/WorldStaticGeometry.hpp"

#include <memory>

namespace thunder {

// Random-access decoder for chunked river/transport vectors. It owns only a
// reusable decode scratch buffer; the pack reader remains owned by the map
// page source/session.
class WorldStaticLayerSource {
public:
    explicit WorldStaticLayerSource(const WorldPackReader& pack);
    ~WorldStaticLayerSource();
    WorldStaticLayerSource(const WorldStaticLayerSource&) = delete;
    WorldStaticLayerSource& operator=(const WorldStaticLayerSource&) = delete;

    [[nodiscard]] bool decode(WorldChunkKey key, WorldPolylineChunk& out) const noexcept;

private:
    const WorldPackReader* pack_ = nullptr;
    mutable std::unique_ptr<WorldPackDecodeScratch> scratch_;
};

} // namespace thunder
