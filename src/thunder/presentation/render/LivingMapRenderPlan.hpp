#pragma once
#include "thunder/presentation/render/RenderGraph.hpp"

namespace thunder {
struct LivingMapRenderPlan {
    RenderResourceHandle instance_buffer{};
    RenderResourceHandle cluster_buffer{};
    RenderResourceHandle transport_buffer{};
    RenderResourceHandle instance_indirect{};
    RenderResourceHandle cluster_indirect{};
    RenderResourceHandle transport_indirect{};
    RenderResourceHandle terrain_depth{};
    RenderResourceHandle hdr_color{};
};
LivingMapRenderPlan add_living_map_passes(RenderGraph& graph);
} // namespace thunder
