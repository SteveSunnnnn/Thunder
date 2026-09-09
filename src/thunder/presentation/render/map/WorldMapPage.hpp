#pragma once

#include "thunder/presentation/render/map/CoastDistancePage.hpp"
#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"
#include "thunder/presentation/render/terrain/TerrainHeightPage.hpp"

#include <array>
#include <cstdint>

namespace thunder {

// CPU-side payload for one atomic political/coast page family plus its
// terrain and categorical masks. It contains no pack reader or graphics API
// state, so picking, offline inspection and renderer upload code can share it.
//
// A2-R residency: the render side uploads `height`, `province` and `coast`
// verbatim into the resident pyramids (u16 planes, byte-identical to these
// storages), so there is no GPU-ready resampled payload any more. The lake
// and spatial masks stay available for CPU consumers such as picking.
struct WorldMapPage {
    ProvinceRasterPage::Storage province{};
    CoastDistancePage::Storage coast{};
    // Optional printed-chart distance, 32 m/unit. Physical coast stays 0.5 m/unit.
    CoastDistancePage::Storage cartographic_coast{};
    TerrainHeightPage::Storage height{};
    std::array<std::uint8_t, ProvinceRasterPage::sample_count> lake_mask{};
    std::array<std::uint8_t, ProvinceRasterPage::sample_count> spatial_mask{};
    bool has_height = false;
    bool has_lake_mask = false;
    bool has_spatial_mask = false;
};

} // namespace thunder
