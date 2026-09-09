#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include "thunder/presentation/ui/FontAtlas.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace thunder {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
    }
}

struct LivingVertex {
    float x;
    float y;
    float z;
};

struct FlagGpuPush {
    std::array<float, 4> placement{};
    std::array<float, 4> motion{};
    std::array<float, 4> primary{};
    std::array<float, 4> secondary{};
    std::array<float, 4> accent{};
};

VkPipelineColorBlendAttachmentState blend_attachment(bool alpha) {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (alpha) {
        state.blendEnable = VK_TRUE;
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.colorBlendOp = VK_BLEND_OP_ADD;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    return state;
}

} // namespace

void VulkanDesktopBackend::create_runtime_renderer() {
    if (!std::filesystem::is_directory(shader_dir_)) {
        throw std::runtime_error("THUNDER_SHADER_DIR is not a directory: " + shader_dir_.string());
    }

    // Resolve the depth format up front: pipelines are compiled against it,
    // and they are built before the offscreen targets are allocated.
    if (settings_.depth_buffer) {
        depth_format_ = pick_depth_format();
    }

    // The streamed map and living instances share a single 32-byte push block;
    // the atlas window follows the camera viewport in the second vec4.
    VkPushConstantRange fullscreen_range{};
    fullscreen_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    fullscreen_range.offset = 0;
    fullscreen_range.size = sizeof(float) * 16u;
    VkPipelineLayoutCreateInfo empty_layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    empty_layout.pushConstantRangeCount = 1;
    empty_layout.pPushConstantRanges = &fullscreen_range;
    vkcheck(vkCreatePipelineLayout(device_, &empty_layout, nullptr, &fullscreen_layout_),
            "vkCreatePipelineLayout(fullscreen)");

    VkPushConstantRange flag_push{};
    flag_push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    flag_push.offset = 0;
    flag_push.size = sizeof(FlagGpuPush);
    VkPipelineLayoutCreateInfo flag_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    flag_layout_info.pushConstantRangeCount = 1;
    flag_layout_info.pPushConstantRanges = &flag_push;
    vkcheck(vkCreatePipelineLayout(device_, &flag_layout_info, nullptr, &flag_layout_),
            "vkCreatePipelineLayout(flag)");

    VkPushConstantRange ui_push{};
    ui_push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    ui_push.offset = 0;
    ui_push.size = sizeof(float) * 4u;
    VkPipelineLayoutCreateInfo ui_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ui_layout_info.pushConstantRangeCount = 1;
    ui_layout_info.pPushConstantRanges = &ui_push;
    vkcheck(vkCreatePipelineLayout(device_, &ui_layout_info, nullptr, &ui_layout_),
            "vkCreatePipelineLayout(ui)");

    // Textured UI (the client-supplied font atlas) uses a separate pipeline
    // layout so solid quads retain the original zero-descriptor hot path.
    VkDescriptorSetLayoutBinding ui_texture_binding{};
    ui_texture_binding.binding = 0;
    ui_texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ui_texture_binding.descriptorCount = 1;
    ui_texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ui_texture_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ui_texture_layout_info.bindingCount = 1;
    ui_texture_layout_info.pBindings = &ui_texture_binding;
    vkcheck(vkCreateDescriptorSetLayout(device_, &ui_texture_layout_info, nullptr, &ui_font_descriptor_layout_),
            "vkCreateDescriptorSetLayout(ui font)");
    VkPipelineLayoutCreateInfo ui_textured_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ui_textured_layout_info.setLayoutCount = 1;
    ui_textured_layout_info.pSetLayouts = &ui_font_descriptor_layout_;
    ui_textured_layout_info.pushConstantRangeCount = 1;
    ui_textured_layout_info.pPushConstantRanges = &ui_push;
    vkcheck(vkCreatePipelineLayout(device_, &ui_textured_layout_info, nullptr, &ui_textured_layout_),
            "vkCreatePipelineLayout(ui textured)");

    // Open the one authoritative world pack before creating the scene
    // descriptors. The renderer never discovers or uploads a legacy atlas;
    // it only exposes bounded page, height and political-palette resources.
    open_world_pack();
    create_world_page_resources();

    // The political map is a scene pass. Set 0 carries only the ownership
    // palette; the resident height/province/SDF pyramids, the level-layout
    // UBO and the patch SSBO live on set 1 (world_levels_descriptor_layout_,
    // created with the pyramid resources in create_world_page_resources()).
    std::array<VkDescriptorSetLayoutBinding, 1> world_map_bindings{};
    world_map_bindings[0].binding = 0;
    world_map_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    world_map_bindings[0].descriptorCount = 1;
    world_map_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo world_map_descriptor_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    world_map_descriptor_info.bindingCount = static_cast<std::uint32_t>(world_map_bindings.size());
    world_map_descriptor_info.pBindings = world_map_bindings.data();
    vkcheck(vkCreateDescriptorSetLayout(device_, &world_map_descriptor_info, nullptr,
                                         &world_map_scene_descriptor_layout_),
            "vkCreateDescriptorSetLayout(world map)");
    const std::array<VkDescriptorSetLayout, 2> world_map_set_layouts{{
        world_map_scene_descriptor_layout_, world_levels_descriptor_layout_}};
    VkPipelineLayoutCreateInfo world_map_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    world_map_layout_info.setLayoutCount = static_cast<std::uint32_t>(world_map_set_layouts.size());
    world_map_layout_info.pSetLayouts = world_map_set_layouts.data();
    world_map_layout_info.pushConstantRangeCount = 1;
    world_map_layout_info.pPushConstantRanges = &fullscreen_range;
    vkcheck(vkCreatePipelineLayout(device_, &world_map_layout_info, nullptr, &world_map_layout_),
            "vkCreatePipelineLayout(world map)");

    if (!ui_font_atlas_path_.empty()) {
        load_ui_font_metrics();
        create_ui_image(ui_font_atlas_path_, ui_font_image_, ui_font_image_memory_,
                        ui_font_view_, ui_font_sampler_, ui_font_width_, ui_font_height_);
        if (ui_font_metrics_ != nullptr &&
            (ui_font_metrics_->width() != ui_font_width_ || ui_font_metrics_->height() != ui_font_height_)) {
            throw std::runtime_error("UI font metrics dimensions do not match atlas image");
        }
        if (ui_font_image_ != VK_NULL_HANDLE) {
            VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
            VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool_info.maxSets = 1;
            pool_info.poolSizeCount = 1;
            pool_info.pPoolSizes = &pool_size;
            vkcheck(vkCreateDescriptorPool(device_, &pool_info, nullptr, &ui_font_descriptor_pool_),
                    "vkCreateDescriptorPool(ui font)");
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = ui_font_descriptor_pool_;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &ui_font_descriptor_layout_;
            vkcheck(vkAllocateDescriptorSets(device_, &allocate, &ui_font_descriptor_set_),
                    "vkAllocateDescriptorSets(ui font)");
            VkDescriptorImageInfo image_info{};
            image_info.sampler = ui_font_sampler_;
            image_info.imageView = ui_font_view_;
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = ui_font_descriptor_set_;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image_info;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    } else {
        std::cerr << "THUNDER_UI_FONT_WARNING: no MSDF font package configured; "
                     "using the diagnostics-only 5x7 fallback. Set THUNDER_UI_FONT_ATLAS "
                     "and THUNDER_UI_FONT_METRICS for shipping UI.\n";
    }
    {
        const VkDescriptorPoolSize world_pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u};
        VkDescriptorPoolCreateInfo world_pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        world_pool_info.maxSets = 1;
        world_pool_info.poolSizeCount = 1;
        world_pool_info.pPoolSizes = &world_pool_size;
        vkcheck(vkCreateDescriptorPool(device_, &world_pool_info, nullptr,
                                        &world_map_scene_descriptor_pool_),
                "vkCreateDescriptorPool(world page streamer)");
        VkDescriptorSetAllocateInfo world_allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        world_allocate.descriptorPool = world_map_scene_descriptor_pool_;
        world_allocate.descriptorSetCount = 1;
        world_allocate.pSetLayouts = &world_map_scene_descriptor_layout_;
        vkcheck(vkAllocateDescriptorSets(device_, &world_allocate, &world_map_scene_descriptor_set_),
                "vkAllocateDescriptorSets(world page streamer)");
        const VkDescriptorImageInfo world_image{
            world_palette_sampler_, world_palette_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkWriteDescriptorSet world_write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
            world_map_scene_descriptor_set_, 0, 0, 1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &world_image, nullptr, nullptr};
        vkUpdateDescriptorSets(device_, 1, &world_write, 0, nullptr);
    }

    // 3D passes render into an MSAA RGBA16F target; the tonemap pass samples
    // the resolved copy and writes the (possibly non-sRGB) swapchain.
    msaa_samples_ = static_cast<VkSampleCountFlagBits>(settings_.msaa_samples);

    VkDescriptorSetLayoutBinding sampled_binding{};
    sampled_binding.binding = 0;
    sampled_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampled_binding.descriptorCount = 1;
    sampled_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo descriptor_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor_layout_info.bindingCount = 1;
    descriptor_layout_info.pBindings = &sampled_binding;
    vkcheck(vkCreateDescriptorSetLayout(device_, &descriptor_layout_info, nullptr, &hdr_descriptor_layout_),
            "vkCreateDescriptorSetLayout(hdr)");

    // The tonemap pass draws a fullscreen triangle, so it shares the same
    // 32-byte block: map viewport for the vertex stage, grading parameters for
    // the fragment stage.
    VkPushConstantRange tonemap_range{};
    tonemap_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemap_range.offset = 0;
    tonemap_range.size = sizeof(float) * 8u;
    VkPipelineLayoutCreateInfo tonemap_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    tonemap_layout_info.setLayoutCount = 1;
    tonemap_layout_info.pSetLayouts = &hdr_descriptor_layout_;
    tonemap_layout_info.pushConstantRangeCount = 1;
    tonemap_layout_info.pPushConstantRanges = &tonemap_range;
    vkcheck(vkCreatePipelineLayout(device_, &tonemap_layout_info, nullptr, &tonemap_layout_),
            "vkCreatePipelineLayout(tonemap)");

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    vkcheck(vkCreateDescriptorPool(device_, &pool_info, nullptr, &hdr_descriptor_pool_),
            "vkCreateDescriptorPool(hdr)");
    VkDescriptorSetAllocateInfo set_allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_allocate.descriptorPool = hdr_descriptor_pool_;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &hdr_descriptor_layout_;
    vkcheck(vkAllocateDescriptorSets(device_, &set_allocate, &hdr_descriptor_set_),
            "vkAllocateDescriptorSets(hdr)");

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    vkcheck(vkCreateSampler(device_, &sampler_info, nullptr, &hdr_sampler_), "vkCreateSampler(hdr)");

    const auto fullscreen = load_shader_module(shader_dir_ / "fullscreen.vert.spv");
    const auto world_map_vertex = load_shader_module(shader_dir_ / "world_map.vert.spv");
    const auto world_map_fragment = load_shader_module(shader_dir_ / "world_map.frag.spv");
    const auto living_vertex = load_shader_module(shader_dir_ / "living.vert.spv");
    const auto living_fragment = load_shader_module(shader_dir_ / "living.frag.spv");
    const auto flag_vertex = load_shader_module(shader_dir_ / "flag.vert.spv");
    const auto flag_fragment = load_shader_module(shader_dir_ / "flag.frag.spv");
    const auto ui_vertex = load_shader_module(shader_dir_ / "ui.vert.spv");
    const auto ui_fragment = load_shader_module(shader_dir_ / "ui.frag.spv");
    const auto ui_textured_fragment = load_shader_module(shader_dir_ / "ui_textured.frag.spv");
    const auto ui_msdf_fragment = load_shader_module(shader_dir_ / "ui_msdf.frag.spv");

    // Post-processing stages only exist on the HDR path. Loaded lazily so the
    // legacy tier never pays for shaders it will not run.
    VkShaderModule tonemap = VK_NULL_HANDLE;
    VkShaderModule fxaa = VK_NULL_HANDLE;
    if (settings_.hdr_target) {
        tonemap = load_shader_module(shader_dir_ / "tonemap.frag.spv");
        if (settings_.fxaa) {
            fxaa = load_shader_module(shader_dir_ / "fxaa.frag.spv");
        }
    }

    auto create_pipeline = [&](VkShaderModule vertex,
                               VkShaderModule fragment,
                               VkPipelineLayout layout,
                               bool alpha,
                               const VkPipelineVertexInputStateCreateInfo* vertex_input,
                               VkFormat color_format,
                               VkSampleCountFlagBits samples,
                               bool depth_test = false,
                               bool depth_write = false,
                               const VkSpecializationInfo* specialization = nullptr) {
        // The specialisation constants have to reach both stages. The map
        // vertex stage derives its relief from the same octave constants as the
        // fragment stage; feeding only the fragment half would leave the vertex
        // on the default octave count and let the displaced silhouette drift
        // away from the relief the fragment shades on lower quality tiers.
        const VkPipelineShaderStageCreateInfo stages[]{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", specialization},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", specialization}};
        VkPipelineVertexInputStateCreateInfo empty_vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = samples;
        VkPipelineDepthStencilStateCreateInfo depth_state{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_state.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
        depth_state.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
        depth_state.depthCompareOp = VK_COMPARE_OP_LESS;
        // Blended overlays must not write depth or they punch holes in
        // whatever is drawn behind them.
        depth_state.depthWriteEnable = (depth_write && !alpha) ? VK_TRUE : VK_FALSE;
        const auto blend = blend_attachment(alpha);
        VkPipelineColorBlendStateCreateInfo blend_state{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend_state.attachmentCount = 1;
        blend_state.pAttachments = &blend;
        const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &color_format;
        // Every pipeline drawn inside a rendering pass that has a depth
        // attachment must declare a matching depth format, even the ones that
        // do not depth-test. Disabling the test is expressed through
        // VkPipelineDepthStencilStateCreateInfo, not by omitting the format.
        if (settings_.depth_buffer && depth_format_ != VK_FORMAT_UNDEFINED) {
            rendering.depthAttachmentFormat = depth_format_;
        }
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = vertex_input != nullptr ? vertex_input : &empty_vertex;
        info.pInputAssemblyState = &input_assembly;
        info.pViewportState = &viewport_state;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        if (settings_.depth_buffer) {
            info.pDepthStencilState = &depth_state;
        }
        info.pColorBlendState = &blend_state;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        vkcheck(vkCreateGraphicsPipelines(device_, pipeline_cache_, 1, &info, nullptr, &pipeline),
                "vkCreateGraphicsPipelines");
        return pipeline;
    };

    // Scene geometry renders into the offscreen HDR target so the tonemap pass
    // can dither and filter before presentation. UI stays on the swapchain: it
    // is authored in display space and must not be pushed through ACES.
    const VkFormat scene_format = settings_.hdr_target ? hdr_format_ : swapchain_format_;
    const auto scene_samples =
        static_cast<VkSampleCountFlagBits>(settings_.hdr_target ? settings_.msaa_samples : 1u);

    // Patches arrive through the set-1 SSBO indexed by gl_InstanceIndex, so
    // the pipeline binds no vertex buffers at all.
    VkPipelineVertexInputStateCreateInfo world_map_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const std::uint32_t world_map_spec_data[2]{settings_.terrain_octaves, settings_.terrain_detail_octaves};
    const VkSpecializationMapEntry world_map_spec_entries[2]{
        {0, 0, sizeof(std::uint32_t)},
        {1, sizeof(std::uint32_t), sizeof(std::uint32_t)}
    };
    const VkSpecializationInfo world_map_spec{
        2, world_map_spec_entries, sizeof(world_map_spec_data), world_map_spec_data
    };
    world_map_pipeline_ = create_pipeline(world_map_vertex, world_map_fragment, world_map_layout_,
                                          false, &world_map_input, scene_format, scene_samples,
                                          settings_.depth_buffer, settings_.depth_buffer,
                                          &world_map_spec);

    const VkVertexInputBindingDescription living_bindings[]{
        {0, sizeof(LivingVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(float) * 16u, VK_VERTEX_INPUT_RATE_INSTANCE}};
    const VkVertexInputAttributeDescription living_attributes[]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 4u},
        {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 8u},
        {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 12u}};
    VkPipelineVertexInputStateCreateInfo living_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    living_input.vertexBindingDescriptionCount = 2;
    living_input.pVertexBindingDescriptions = living_bindings;
    living_input.vertexAttributeDescriptionCount = 5;
    living_input.pVertexAttributeDescriptions = living_attributes;
    // Living instances are the only genuinely 3D, overlapping geometry in the
    // scene, so they are the pass that actually needs depth sorting.
    living_pipeline_ = create_pipeline(living_vertex, living_fragment, fullscreen_layout_, false,
                                      &living_input, scene_format, scene_samples,
                                      settings_.depth_buffer, settings_.depth_buffer);

    const VkVertexInputBindingDescription flag_binding{
        0, sizeof(DynamicFlagVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription flag_attributes[]{{
        0, 0, VK_FORMAT_R32G32B32_SFLOAT,
        static_cast<std::uint32_t>(offsetof(DynamicFlagVertex, x))}, {
        1, 0, VK_FORMAT_R32G32_SFLOAT,
        static_cast<std::uint32_t>(offsetof(DynamicFlagVertex, u))}};
    VkPipelineVertexInputStateCreateInfo flag_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    flag_input.vertexBindingDescriptionCount = 1;
    flag_input.pVertexBindingDescriptions = &flag_binding;
    flag_input.vertexAttributeDescriptionCount = 2;
    flag_input.pVertexAttributeDescriptions = flag_attributes;
    // The flag is submitted through the UI module path rather than the scene
    // pass, so it targets the swapchain and stays in display space with the
    // rest of the HUD. It is a blended badge: it never writes depth.
    flag_pipeline_ = create_pipeline(flag_vertex, flag_fragment, flag_layout_, true,
                                     &flag_input, swapchain_format_, VK_SAMPLE_COUNT_1_BIT,
                                     false, false);

    const VkVertexInputBindingDescription ui_binding{0, sizeof(UiGpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription ui_attributes[]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(UiGpuVertex, x))},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(UiGpuVertex, u))},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(UiGpuVertex, r))}};
    VkPipelineVertexInputStateCreateInfo ui_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    ui_input.vertexBindingDescriptionCount = 1;
    ui_input.pVertexBindingDescriptions = &ui_binding;
    ui_input.vertexAttributeDescriptionCount = 3;
    ui_input.pVertexAttributeDescriptions = ui_attributes;
    ui_pipeline_ = create_pipeline(ui_vertex, ui_fragment, ui_layout_, true, &ui_input, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);
    ui_textured_pipeline_ = create_pipeline(ui_vertex, ui_textured_fragment, ui_textured_layout_, true, &ui_input, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);
    ui_msdf_pipeline_ = create_pipeline(ui_vertex, ui_msdf_fragment, ui_textured_layout_, true, &ui_input, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);
    // Country/state typography is part of the map artwork, not the display-
    // space HUD. Rendering the same MSDF geometry into the HDR scene makes it
    // inherit map grading and lets living-map geometry naturally cover it.
    map_label_pipeline_ = create_pipeline(ui_vertex, ui_msdf_fragment, ui_textured_layout_, true,
                                          &ui_input, scene_format, scene_samples, false, false);

    // Tonemap resolves the HDR scene into the swapchain. It always exists on
    // the HDR path, even without FXAA, because it is what applies dithering.
    if (tonemap != VK_NULL_HANDLE) {
        tonemap_pipeline_ = create_pipeline(fullscreen, tonemap, tonemap_layout_, false, nullptr,
                                            swapchain_format_, VK_SAMPLE_COUNT_1_BIT);
    }
    if (fxaa != VK_NULL_HANDLE) {
        fxaa_pipeline_ = create_pipeline(fullscreen, fxaa, hdr_descriptor_layout_ == VK_NULL_HANDLE
                                                               ? tonemap_layout_
                                                               : tonemap_layout_,
                                         false, nullptr, swapchain_format_, VK_SAMPLE_COUNT_1_BIT);
    }

    vkDestroyShaderModule(device_, ui_fragment, nullptr);
    vkDestroyShaderModule(device_, ui_textured_fragment, nullptr);
    vkDestroyShaderModule(device_, ui_msdf_fragment, nullptr);
    vkDestroyShaderModule(device_, ui_vertex, nullptr);
    vkDestroyShaderModule(device_, living_fragment, nullptr);
    vkDestroyShaderModule(device_, living_vertex, nullptr);
    vkDestroyShaderModule(device_, flag_fragment, nullptr);
    vkDestroyShaderModule(device_, flag_vertex, nullptr);
    vkDestroyShaderModule(device_, world_map_vertex, nullptr);
    vkDestroyShaderModule(device_, world_map_fragment, nullptr);
    vkDestroyShaderModule(device_, fullscreen, nullptr);
    if (tonemap != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, tonemap, nullptr);
    }
    if (fxaa != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, fxaa, nullptr);
    }

    // A compact unit triangle is instanced by the living-map stream. Its
    // transform is supplied by submit_living_instances(); the fallback
    // identity instance remains useful for renderer smoke tests.
    const std::array<LivingVertex, 3> living_vertices{{
        {-0.50f, -0.50f, 0.0f},
        {0.50f, -0.50f, 0.0f},
        {0.00f, 0.50f, 0.0f}}};
    const std::array<float, 16> identity{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f}};
    create_host_buffer(sizeof(living_vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       living_vertices.data(), living_vertices_, living_vertices_memory_);
    create_host_buffer(sizeof(identity), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       identity.data(), living_instance_, living_instance_memory_);

    if (dynamic_flag_) {
        const auto flag_vertices = dynamic_flag_->vertices();
        const auto flag_indices = dynamic_flag_->indices();
        if (!flag_vertices.empty() && !flag_indices.empty()) {
            create_host_buffer(flag_vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               flag_vertices.data(), flag_vertices_, flag_vertices_memory_);
            create_host_buffer(flag_indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               flag_indices.data(), flag_indices_, flag_indices_memory_);
            flag_index_count_ = static_cast<std::uint32_t>(flag_indices.size());
        }
    }

    const std::array<UiGpuVertex, 6> ui_vertices{{
        {28.0f, 28.0f, 0.0f, 0.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {380.0f, 28.0f, 1.0f, 0.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {380.0f, 96.0f, 1.0f, 1.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {28.0f, 28.0f, 0.0f, 0.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {380.0f, 96.0f, 1.0f, 1.0f, 0.06f, 0.07f, 0.09f, 0.88f},
        {28.0f, 96.0f, 0.0f, 1.0f, 0.06f, 0.07f, 0.09f, 0.88f}}};
    create_host_buffer(sizeof(ui_vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       ui_vertices.data(), ui_vertices_, ui_vertices_memory_);

    // Offscreen targets depend on the swapchain extent, which is only final
    // once create_swapchain() has run, so they are built last.
    create_scene_targets();

    runtime_renderer_enabled_ = true;
}

void VulkanDesktopBackend::destroy_runtime_renderer() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    // Offscreen targets first: the HDR descriptor set holds image views that
    // must outlive nothing else here, but destroying them last would leave the
    // descriptor referencing freed images during teardown.
    destroy_scene_targets();
    for (auto& frame : ui_frame_buffers_) {
        if (frame.vertex_mapped != nullptr && frame.vertex_memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, frame.vertex_memory);
            frame.vertex_mapped = nullptr;
        }
        if (frame.vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, frame.vertex_buffer, nullptr);
            frame.vertex_buffer = VK_NULL_HANDLE;
        }
        if (frame.vertex_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, frame.vertex_memory, nullptr);
            frame.vertex_memory = VK_NULL_HANDLE;
        }
        frame.vertex_capacity = 0;
        if (frame.index_mapped != nullptr && frame.index_memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, frame.index_memory);
            frame.index_mapped = nullptr;
        }
        if (frame.index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, frame.index_buffer, nullptr);
            frame.index_buffer = VK_NULL_HANDLE;
        }
        if (frame.index_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, frame.index_memory, nullptr);
            frame.index_memory = VK_NULL_HANDLE;
        }
        frame.index_capacity = 0;
    }
    for (auto& frame : map_overlay_frames_) {
        destroy_mapped_host_buffer(frame.vertex_buffer, frame.vertex_memory, frame.vertex_mapped);
        destroy_mapped_host_buffer(frame.index_buffer, frame.index_memory, frame.index_mapped);
        frame.vertex_capacity = 0;
        frame.index_capacity = 0;
        frame.index_count = 0;
        frame.generation = 0;
    }
    map_overlay_staging_vertices_.clear();
    map_overlay_staging_indices_.clear();
    map_overlay_generation_ = 0;
    if (ui_vertices_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, ui_vertices_, nullptr);
        ui_vertices_ = VK_NULL_HANDLE;
    }
    if (ui_vertices_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, ui_vertices_memory_, nullptr);
        ui_vertices_memory_ = VK_NULL_HANDLE;
    }
    if (living_instance_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, living_instance_, nullptr);
        living_instance_ = VK_NULL_HANDLE;
    }
    if (living_instance_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, living_instance_memory_, nullptr);
        living_instance_memory_ = VK_NULL_HANDLE;
    }
    for (auto& frame : living_frame_buffers_) {
        destroy_mapped_host_buffer(frame.buffer, frame.memory, frame.mapped);
        frame.capacity = 0;
    }
    living_instance_count_ = 0u;
    if (living_vertices_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, living_vertices_, nullptr);
        living_vertices_ = VK_NULL_HANDLE;
    }
    if (living_vertices_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, living_vertices_memory_, nullptr);
        living_vertices_memory_ = VK_NULL_HANDLE;
    }
    if (flag_indices_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, flag_indices_, nullptr);
        flag_indices_ = VK_NULL_HANDLE;
    }
    if (flag_indices_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, flag_indices_memory_, nullptr);
        flag_indices_memory_ = VK_NULL_HANDLE;
    }
    if (flag_vertices_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, flag_vertices_, nullptr);
        flag_vertices_ = VK_NULL_HANDLE;
    }
    if (flag_vertices_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, flag_vertices_memory_, nullptr);
        flag_vertices_memory_ = VK_NULL_HANDLE;
    }
    flag_index_count_ = 0;

    const std::array<VkPipeline*, 9> pipelines{{
        &ui_pipeline_, &ui_textured_pipeline_, &ui_msdf_pipeline_, &map_label_pipeline_,
        &living_pipeline_, &flag_pipeline_,
        &world_map_pipeline_,
        &tonemap_pipeline_, &fxaa_pipeline_}};
    for (auto* pipeline : pipelines) {
        if (*pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
    }
    if (ui_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, ui_layout_, nullptr);
        ui_layout_ = VK_NULL_HANDLE;
    }
    if (ui_textured_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, ui_textured_layout_, nullptr);
        ui_textured_layout_ = VK_NULL_HANDLE;
    }
    if (flag_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, flag_layout_, nullptr);
        flag_layout_ = VK_NULL_HANDLE;
    }
    if (fullscreen_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, fullscreen_layout_, nullptr);
        fullscreen_layout_ = VK_NULL_HANDLE;
    }
    if (world_map_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, world_map_layout_, nullptr);
        world_map_layout_ = VK_NULL_HANDLE;
    }
    if (tonemap_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, tonemap_layout_, nullptr);
        tonemap_layout_ = VK_NULL_HANDLE;
    }
    if (hdr_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, hdr_descriptor_pool_, nullptr);
        hdr_descriptor_pool_ = VK_NULL_HANDLE;
        hdr_descriptor_set_ = VK_NULL_HANDLE;
    }
    if (hdr_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, hdr_descriptor_layout_, nullptr);
        hdr_descriptor_layout_ = VK_NULL_HANDLE;
    }
    if (hdr_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, hdr_sampler_, nullptr);
        hdr_sampler_ = VK_NULL_HANDLE;
    }
    if (ui_font_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, ui_font_descriptor_pool_, nullptr);
        ui_font_descriptor_pool_ = VK_NULL_HANDLE;
        ui_font_descriptor_set_ = VK_NULL_HANDLE;
    }
    if (world_map_scene_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, world_map_scene_descriptor_pool_, nullptr);
        world_map_scene_descriptor_pool_ = VK_NULL_HANDLE;
        world_map_scene_descriptor_set_ = VK_NULL_HANDLE;
    }
    if (ui_font_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, ui_font_descriptor_layout_, nullptr);
        ui_font_descriptor_layout_ = VK_NULL_HANDLE;
    }
    if (world_map_scene_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, world_map_scene_descriptor_layout_, nullptr);
        world_map_scene_descriptor_layout_ = VK_NULL_HANDLE;
    }
    destroy_ui_image(ui_font_image_, ui_font_image_memory_, ui_font_view_, ui_font_sampler_);
    destroy_world_page_resources();
    ui_font_slots_.clear();
    ui_font_metrics_.reset();
    ui_font_width_ = ui_font_height_ = ui_font_cell_ = ui_font_columns_ = 0;
    runtime_renderer_enabled_ = false;
}



} // namespace thunder
