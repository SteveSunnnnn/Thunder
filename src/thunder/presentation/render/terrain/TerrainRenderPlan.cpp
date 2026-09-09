#include "thunder/presentation/render/terrain/TerrainRenderPlan.hpp"

namespace thunder {

TerrainRenderPlan add_terrain_passes(RenderGraph& graph) {
    TerrainRenderPlan plan;
    plan.height_cache = graph.add_resource("terrain_height_cache", RenderResourceKind::Image);
    plan.material_cache = graph.add_resource("terrain_material_cache", RenderResourceKind::Image);
    plan.patch_instances = graph.add_resource("terrain_patch_instances", RenderResourceKind::Buffer);
    plan.indirect_commands = graph.add_resource("terrain_indirect_commands", RenderResourceKind::Buffer);
    plan.depth = graph.add_resource("terrain_depth", RenderResourceKind::Image);
    plan.hdr_color = graph.add_resource("terrain_hdr", RenderResourceKind::Image);

    graph.add_pass({"terrain_stream_upload", RenderQueue::Transfer,
                    {{plan.height_cache, RenderUsage::TransferDst, true},
                     {plan.material_cache, RenderUsage::TransferDst, true},
                     {plan.patch_instances, RenderUsage::TransferDst, true}}});

    graph.add_pass({"terrain_cull", RenderQueue::Compute,
                    {{plan.patch_instances, RenderUsage::StorageRead, false},
                     {plan.indirect_commands, RenderUsage::StorageWrite, true}}});

    graph.add_pass({"terrain_draw", RenderQueue::Graphics,
                    {{plan.height_cache, RenderUsage::Sampled, false},
                     {plan.material_cache, RenderUsage::Sampled, false},
                     {plan.patch_instances, RenderUsage::StorageRead, false},
                     {plan.indirect_commands, RenderUsage::Indirect, false},
                     {plan.depth, RenderUsage::DepthAttachment, true},
                     {plan.hdr_color, RenderUsage::ColorAttachment, true}}});

    return plan;
}

} // namespace thunder
