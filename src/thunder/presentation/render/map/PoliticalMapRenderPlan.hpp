#pragma once

#include "thunder/presentation/render/RenderGraph.hpp"

namespace thunder {

struct PoliticalMapRenderPlan {
    RenderResourceHandle province_id_cache{};
    RenderResourceHandle coast_distance_cache{};
    RenderResourceHandle province_records{};
    RenderResourceHandle country_colors{};
    RenderResourceHandle map_mode_words{};
    RenderResourceHandle vector_borders_vbo{};
    RenderResourceHandle vector_borders_ibo{};
    RenderResourceHandle parchment_texture{};
};


PoliticalMapRenderPlan add_political_map_passes(RenderGraph& graph,
                                                 RenderResourceHandle terrain_depth,
                                                 RenderResourceHandle terrain_hdr);

} // namespace thunder
