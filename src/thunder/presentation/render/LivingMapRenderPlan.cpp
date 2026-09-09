#include "thunder/presentation/render/LivingMapRenderPlan.hpp"
namespace thunder {
LivingMapRenderPlan add_living_map_passes(RenderGraph& graph) {
    LivingMapRenderPlan plan;
    plan.instance_buffer = graph.add_resource("living_instances", RenderResourceKind::Buffer);
    plan.cluster_buffer = graph.add_resource("living_clusters", RenderResourceKind::Buffer);
    plan.transport_buffer = graph.add_resource("living_transport", RenderResourceKind::Buffer);
    plan.instance_indirect = graph.add_resource("living_instance_indirect", RenderResourceKind::Buffer);
    plan.cluster_indirect = graph.add_resource("living_cluster_indirect", RenderResourceKind::Buffer);
    plan.transport_indirect = graph.add_resource("living_transport_indirect", RenderResourceKind::Buffer);
    plan.terrain_depth = graph.add_resource("living_terrain_depth", RenderResourceKind::Image);
    plan.hdr_color = graph.add_resource("living_hdr", RenderResourceKind::Image);
    graph.add_pass({"living_stream_upload", RenderQueue::Transfer,
                    {{plan.instance_buffer,RenderUsage::TransferDst,true},{plan.cluster_buffer,RenderUsage::TransferDst,true},{plan.transport_buffer,RenderUsage::TransferDst,true}}});
    graph.add_pass({"living_gpu_cull", RenderQueue::Compute,
                    {{plan.instance_buffer,RenderUsage::StorageRead,false},{plan.cluster_buffer,RenderUsage::StorageRead,false},{plan.transport_buffer,RenderUsage::StorageRead,false},
                     {plan.instance_indirect,RenderUsage::StorageWrite,true},{plan.cluster_indirect,RenderUsage::StorageWrite,true},{plan.transport_indirect,RenderUsage::StorageWrite,true}}});
    graph.add_pass({"living_transport_draw",RenderQueue::Graphics,
                    {{plan.transport_buffer,RenderUsage::StorageRead,false},{plan.transport_indirect,RenderUsage::Indirect,false},{plan.terrain_depth,RenderUsage::DepthAttachment,true},{plan.hdr_color,RenderUsage::ColorAttachment,true}}});
    graph.add_pass({"living_cluster_draw",RenderQueue::Graphics,
                    {{plan.cluster_buffer,RenderUsage::StorageRead,false},{plan.cluster_indirect,RenderUsage::Indirect,false},{plan.terrain_depth,RenderUsage::DepthAttachment,true},{plan.hdr_color,RenderUsage::ColorAttachment,true}}});
    graph.add_pass({"living_instance_draw",RenderQueue::Graphics,
                    {{plan.instance_buffer,RenderUsage::StorageRead,false},{plan.instance_indirect,RenderUsage::Indirect,false},{plan.terrain_depth,RenderUsage::DepthAttachment,true},{plan.hdr_color,RenderUsage::ColorAttachment,true}}});
    return plan;
}
} // namespace thunder
