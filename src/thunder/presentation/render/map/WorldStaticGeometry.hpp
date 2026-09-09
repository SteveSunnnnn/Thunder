#pragma once

#include "thunder/content/worldpack/WorldPack.hpp"

#include <vector>

namespace thunder {

struct WorldPolylinePoint {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WorldPolyline {
    std::vector<WorldPolylinePoint> points;
};

// One chunk-local vector payload. The directory of available chunks lives in
// WorldStaticLayers; this render-facing type is materialized only when the
// visible chunk is decoded.
struct WorldPolylineChunk {
    WorldChunkKey key{};
    std::vector<WorldPolyline> lines;
};

} // namespace thunder
