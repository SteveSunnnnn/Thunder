#include "thunder/presentation/render/map/PoliticalMapRenderPlan.hpp"

namespace thunder {

PoliticalMapRenderPlan add_political_map_passes(RenderGraph& graph,
                                                 RenderResourceHandle terrain_depth,
                                                 RenderResourceHandle terrain_hdr) {
    PoliticalMapRenderPlan plan;
    plan.province_id_cache = graph.add_resource("province_id_cache", RenderResourceKind::Image);
    plan.coast_distance_cache = graph.add_resource("coast_distance_cache", RenderResourceKind::Image);
    plan.province_records = graph.add_resource("province_political_records", RenderResourceKind::Buffer);
    plan.country_colors = graph.add_resource("country_colors", RenderResourceKind::Buffer);
    plan.map_mode_words = graph.add_resource("province_map_mode_words", RenderResourceKind::Buffer);
    plan.vector_borders_vbo = graph.add_resource("vector_borders_vbo", RenderResourceKind::Buffer);
    plan.vector_borders_ibo = graph.add_resource("vector_borders_ibo", RenderResourceKind::Buffer);
    plan.parchment_texture = graph.add_resource("decorative_parchment_texture", RenderResourceKind::Image);

    graph.add_pass({"political_map_upload", RenderQueue::Transfer,
                    {{plan.province_id_cache, RenderUsage::TransferDst, true},
                     {plan.coast_distance_cache, RenderUsage::TransferDst, true},
                     {plan.province_records, RenderUsage::TransferDst, true},
                     {plan.country_colors, RenderUsage::TransferDst, true},
                     {plan.map_mode_words, RenderUsage::TransferDst, true},
                     {plan.vector_borders_vbo, RenderUsage::TransferDst, true},
                     {plan.vector_borders_ibo, RenderUsage::TransferDst, true},
                     {plan.parchment_texture, RenderUsage::TransferDst, true}}});

    graph.add_pass({"political_map_overlay", RenderQueue::Graphics,
                    {{plan.province_id_cache, RenderUsage::Sampled, false},
                     {plan.coast_distance_cache, RenderUsage::Sampled, false},
                     {plan.province_records, RenderUsage::StorageRead, false},
                     {plan.country_colors, RenderUsage::StorageRead, false},
                     {plan.map_mode_words, RenderUsage::StorageRead, false},
                     {plan.vector_borders_vbo, RenderUsage::StorageRead, false},
                     {plan.vector_borders_ibo, RenderUsage::Index, false},
                     {plan.parchment_texture, RenderUsage::Sampled, false},
                     {terrain_depth, RenderUsage::DepthAttachment, false},
                     {terrain_hdr, RenderUsage::ColorAttachment, true}}});

    return plan;

}

} // namespace thunder
