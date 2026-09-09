#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace thunder {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
    }
}

} // namespace

void VulkanDesktopBackend::open_world_pack() {
    if (world_pack_path_.empty()) {
        throw std::runtime_error("world pack is required before Vulkan renderer initialization");
    }
    // Four pack readers decode the 1490 resident pages in parallel; enqueuing
    // immediately at startup gets the background workers decoding while Vulkan initializes.
    world_page_streamer_ = std::make_unique<WorldMapPageStreamer>(4u);
    std::string diagnostic;
    if (!world_page_streamer_->open(world_pack_path_, diagnostic))
        throw std::runtime_error(diagnostic.empty() ? "failed to open world map page source" : diagnostic);
    world_page_streamer_->enqueue_all_pages({0.5f, 0.5f, 0.5f, 0.5f});
    world_patch_staging_.clear();
    world_patch_count_ = 0u;
    world_last_uploaded_ = 0u;
    world_lod_ = 0.0f;
    world_pyramid_layout_initialized_.fill(false);
    std::copy(std::begin(map_view_), std::end(map_view_), std::begin(world_render_view_));
    world_render_camera_[0] = map_camera_[0];
    world_render_camera_[1] = map_camera_[1];
    world_render_camera_[2] = 0.0f;
    world_render_camera_[3] = 0.0f;
    world_palette_layout_initialized_ = false;
    world_palette_dirty_ = true;
    world_palette_cpu_.assign(world_palette_bytes_, std::byte{0});
    world_political_cpu_.assign(65'536u, ProvincePoliticalRecord{});
    world_political_frame_revision_.fill(0u);
    ++world_political_revision_;

    // A deterministic neutral palette keeps the renderer useful before the
    // first simulation-side ownership upload. The actual desktop path calls
    // set_world_political_state() immediately after bootstrap.
    for (std::uint32_t id = 1u; id < 65'536u; ++id) {
        std::uint32_t hash = 2166136261u ^ id;
        hash ^= hash >> 13u;
        hash *= 0x9e3779b1u;
        const auto offset = static_cast<std::size_t>(id) * 4u;
        world_palette_cpu_[offset + 0u] = static_cast<std::byte>(96u + (hash & 0x5fu));
        world_palette_cpu_[offset + 1u] = static_cast<std::byte>(88u + ((hash >> 8u) & 0x5fu));
        world_palette_cpu_[offset + 2u] = static_cast<std::byte>(76u + ((hash >> 16u) & 0x5fu));
        world_palette_cpu_[offset + 3u] = static_cast<std::byte>(255u);
    }
    world_pack_ready_ = true;
    ensure_wired_terrain_pipelines();
}

void VulkanDesktopBackend::ensure_wired_terrain_pipelines() {
    if (wired_terrain_initialized_) return;
    // Wire previously orphaned engine modules into the Vulkan path.
    // 1) Bindless material for 6 biomes via TerrainMaterialEvaluator (was hard-coded colours in shader)
    for (int b = 0; b < static_cast<int>(TerrainBiomeKind::Count); ++b) {
        auto kind = static_cast<TerrainBiomeKind>(b);
        auto mat = TerrainMaterialEvaluator::default_material_for_biome(kind);
        PbrMaterial cpu_mat{};
        cpu_mat.base_color = {mat.albedo.x, mat.albedo.y, mat.albedo.z, 1.0f};
        cpu_mat.metallic = mat.metallic;
        cpu_mat.roughness = mat.roughness;
        cpu_mat.normal_scale = mat.normal_strength;
        // register a dummy texture slot for each biome (proves bindless path is live)
        std::uint64_t hash = 0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(b) * 0x9e3779b97f4a7c15ull);
        terrain_bindless_.register_texture(hash);
        terrain_bindless_.register_material(cpu_mat);
    }
    // 2) Volumetric atmosphere sky constants (consumed by the diagnostics dump)
    wired_cloud_uniforms_ = CloudLayerUniforms{};
    wired_sky_output_ = VolumetricAtmosphereAndClouds::compute_sky_environment(
        Vec3{-0.65f, -0.45f, 0.60f}.normalize(), 2.2f);
    wired_terrain_initialized_ = true;
}

void VulkanDesktopBackend::set_world_political_state(
    std::span<const ProvincePoliticalRecord> records,
    std::span<const Rgba8> country_colors) {
    if (world_palette_cpu_.size() != world_palette_bytes_) {
        world_palette_cpu_.assign(world_palette_bytes_, std::byte{0});
    }
    std::fill(world_palette_cpu_.begin(), world_palette_cpu_.end(), std::byte{0});
    world_political_cpu_.assign(65'536u, ProvincePoliticalRecord{});
    const auto count = std::min<std::size_t>(records.size(), ProvinceRasterPage::max_province_count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto record = records[index];
        world_political_cpu_[index + 1u] = record;
        const auto offset = (index + 1u) * 4u;
        Rgba8 color{248u, 246u, 238u, 255u};
        if ((record.flags & 2u) != 0u) {
            // Freshwater lake: alpha = 0, blue = 255 signals lake to shaders
            color = Rgba8{0u, 0u, 255u, 0u};
        } else if ((record.flags & 1u) != 0u) {
            // Sea / Ocean: alpha = 0, blue = 0 signals open sea/ocean to shaders
            color = Rgba8{0u, 0u, 0u, 0u};
        } else if (record.owner_country < country_colors.size()) {
            color = country_colors[record.owner_country];
        }
        world_palette_cpu_[offset + 0u] = static_cast<std::byte>(color.r);
        world_palette_cpu_[offset + 1u] = static_cast<std::byte>(color.g);
        world_palette_cpu_[offset + 2u] = static_cast<std::byte>(color.b);
        world_palette_cpu_[offset + 3u] = static_cast<std::byte>(color.a);
    }
    world_palette_dirty_ = true;
    ++world_political_revision_;
}

void VulkanDesktopBackend::create_world_page_resources() {
    // The three resident pyramids (A2-R). All three are allocated once at the
    // full baked size; pages stream into fixed texel rectangles forever after.
    create_image_2d(kHeightPyramidW, kHeightPyramidH, VK_FORMAT_R16_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_height_pyramid_image_, world_height_pyramid_memory_,
                    world_height_pyramid_view_);
    create_image_2d(kPagePyramidW, kPagePyramidH, VK_FORMAT_R16_UINT,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_province_image_, world_province_memory_, world_province_view_);
    create_image_2d(kPagePyramidW, kPagePyramidH, VK_FORMAT_R16_SNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_sdf_image_, world_sdf_memory_, world_sdf_view_);
    create_image_2d(kPagePyramidW, kPagePyramidH, VK_FORMAT_R16_SNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_chart_sdf_image_, world_chart_sdf_memory_, world_chart_sdf_view_);
    create_image_2d(256u, 256u, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                    world_palette_image_, world_palette_memory_, world_palette_view_);

    auto create_sampler = [&](VkFilter filter, VkSampler& sampler, const char* label) {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 0.0f;
        vkcheck(vkCreateSampler(device_, &info, nullptr, &sampler), label);
    };
    // Height and coast SDF filter bilinearly; province ids are texel-fetched
    // (the NEAREST sampler only satisfies the combined-descriptor contract).
    create_sampler(VK_FILTER_LINEAR, world_linear_clamp_sampler_, "vkCreateSampler(world linear)");
    create_sampler(VK_FILTER_NEAREST, world_nearest_sampler_, "vkCreateSampler(world nearest)");
    create_sampler(VK_FILTER_NEAREST, world_palette_sampler_, "vkCreateSampler(world palette)");

    // WorldLevels UBO: the compile-time pyramid geometry, mirrored std140.
    create_mapped_host_buffer(static_cast<VkDeviceSize>(kWorldLevelsUboFloats) * sizeof(float),
                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              world_levels_ubo_.buffer, world_levels_ubo_.memory,
                              world_levels_ubo_.mapped);
    if (world_levels_ubo_.mapped != nullptr) {
        // std140 field order: base_pages (floats 0-3), height_inv_size (4-7),
        // page_inv_size (8-11), height_origin[4] (12-27), page_origin[4]
        // (28-43), height_scale_bias (44-47). vec4 arrays keep a 16-byte
        // stride, so the host side is one flat float array.
        std::array<float, kWorldLevelsUboFloats> ubo{};
        ubo[0] = 40.0f;
        ubo[1] = 28.0f;
        const auto& metadata = world_page_streamer_->metadata();
        ubo[2] = static_cast<float>((metadata.bounds_world_m[2] - metadata.bounds_world_m[0]) /
                                    metadata.base_page_world_size_m);
        ubo[3] = static_cast<float>((metadata.bounds_world_m[3] - metadata.bounds_world_m[1]) /
                                    metadata.base_page_world_size_m);
        ubo[4] = 1.0f / static_cast<float>(kHeightPyramidW);
        ubo[5] = 1.0f / static_cast<float>(kHeightPyramidH);
        ubo[8] = 1.0f / static_cast<float>(kPagePyramidW);
        ubo[9] = 1.0f / static_cast<float>(kPagePyramidH);
        for (std::uint32_t level = 0u; level < kWorldLevelCount; ++level) {
            const auto& layout = kWorldLevels[level];
            ubo[12u + level * 4u + 0u] = static_cast<float>(layout.h_ox);
            ubo[12u + level * 4u + 1u] = static_cast<float>(layout.h_oy);
            ubo[28u + level * 4u + 0u] = static_cast<float>(layout.p_ox);
            ubo[28u + level * 4u + 1u] = static_cast<float>(layout.p_oy);
        }
        ubo[44] = kHeightMetersPerUnit;
        ubo[45] = kHeightBiasMeters;
        std::memcpy(world_levels_ubo_.mapped, ubo.data(), sizeof(ubo));
    }

    for (auto& frame : world_upload_frames_) {
        create_mapped_host_buffer(static_cast<VkDeviceSize>(world_upload_buffer_bytes_),
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  frame.buffer, frame.memory, frame.mapped);
    }
    for (auto& frame : world_patch_frames_) {
        create_mapped_host_buffer(static_cast<VkDeviceSize>(world_patch_buffer_bytes_),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  frame.buffer, frame.memory, frame.mapped);
    }
    for (auto& frame : world_political_frames_) {
        create_mapped_host_buffer(static_cast<VkDeviceSize>(world_political_bytes_),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  frame.buffer, frame.memory, frame.mapped);
    }

    // Descriptor set 1: the resident pyramids, the level-layout UBO and the
    // per-frame patch SSBO ring. Set 0 (palette) and the pipeline layout are
    // created by the runtime renderer on top of these layouts.
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                   VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[6] = {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();
    vkcheck(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr,
                                        &world_levels_descriptor_layout_),
            "vkCreateDescriptorSetLayout(world levels)");

    std::array<VkDescriptorPoolSize, 3> pool_sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4u * frames_in_flight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames_in_flight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2u * frames_in_flight}}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = frames_in_flight;
    pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    vkcheck(vkCreateDescriptorPool(device_, &pool_info, nullptr,
                                   &world_levels_descriptor_pool_),
            "vkCreateDescriptorPool(world levels)");

    const VkDescriptorImageInfo height_info{
        world_linear_clamp_sampler_, world_height_pyramid_view_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo province_info{
        world_nearest_sampler_, world_province_view_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo sdf_info{
        world_linear_clamp_sampler_, world_sdf_view_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo chart_info{
        world_linear_clamp_sampler_, world_chart_sdf_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo ubo_info{
        world_levels_ubo_.buffer, 0,
        static_cast<VkDeviceSize>(kWorldLevelsUboFloats) * sizeof(float)};
    for (std::uint32_t frame = 0u; frame < frames_in_flight; ++frame) {
        VkDescriptorSetAllocateInfo allocate{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = world_levels_descriptor_pool_;
        allocate.descriptorSetCount = 1u;
        allocate.pSetLayouts = &world_levels_descriptor_layout_;
        vkcheck(vkAllocateDescriptorSets(device_, &allocate,
                                         &world_map_levels_descriptor_sets_[frame]),
                "vkAllocateDescriptorSets(world levels)");
        const VkDescriptorBufferInfo patches_info{
            world_patch_frames_[frame].buffer, 0, static_cast<VkDeviceSize>(world_patch_buffer_bytes_)};
        const VkDescriptorBufferInfo political_info{
            world_political_frames_[frame].buffer, 0, static_cast<VkDeviceSize>(world_political_bytes_)};
        std::array<VkWriteDescriptorSet, 7> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     world_map_levels_descriptor_sets_[frame], 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &height_info, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     world_map_levels_descriptor_sets_[frame], 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &province_info, nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     world_map_levels_descriptor_sets_[frame], 2, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sdf_info, nullptr, nullptr};
        writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     world_map_levels_descriptor_sets_[frame], 3, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo_info, nullptr};
        writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     world_map_levels_descriptor_sets_[frame], 4, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &patches_info, nullptr};
        writes[5] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     world_map_levels_descriptor_sets_[frame], 5, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &political_info, nullptr};
        writes[6] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     world_map_levels_descriptor_sets_[frame], 6, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &chart_info, nullptr, nullptr};
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void VulkanDesktopBackend::destroy_world_page_resources() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        for (auto& frame : world_upload_frames_)
            destroy_mapped_host_buffer(frame.buffer, frame.memory, frame.mapped);
        for (auto& frame : world_patch_frames_)
            destroy_mapped_host_buffer(frame.buffer, frame.memory, frame.mapped);
        for (auto& frame : world_political_frames_)
            destroy_mapped_host_buffer(frame.buffer, frame.memory, frame.mapped);
        destroy_mapped_host_buffer(world_levels_ubo_.buffer, world_levels_ubo_.memory,
                                   world_levels_ubo_.mapped);
        world_levels_ubo_ = {};
        if (world_levels_descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, world_levels_descriptor_pool_, nullptr);
            world_levels_descriptor_pool_ = VK_NULL_HANDLE;
        }
        if (world_levels_descriptor_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, world_levels_descriptor_layout_, nullptr);
            world_levels_descriptor_layout_ = VK_NULL_HANDLE;
        }
        world_map_levels_descriptor_sets_.fill(VK_NULL_HANDLE);
        if (world_linear_clamp_sampler_ != VK_NULL_HANDLE)
            vkDestroySampler(device_, world_linear_clamp_sampler_, nullptr);
        if (world_nearest_sampler_ != VK_NULL_HANDLE)
            vkDestroySampler(device_, world_nearest_sampler_, nullptr);
        if (world_palette_sampler_ != VK_NULL_HANDLE)
            vkDestroySampler(device_, world_palette_sampler_, nullptr);
        world_linear_clamp_sampler_ = VK_NULL_HANDLE;
        world_nearest_sampler_ = VK_NULL_HANDLE;
        world_palette_sampler_ = VK_NULL_HANDLE;
        destroy_image_2d(world_height_pyramid_image_, world_height_pyramid_memory_,
                         world_height_pyramid_view_);
        destroy_image_2d(world_province_image_, world_province_memory_, world_province_view_);
        destroy_image_2d(world_sdf_image_, world_sdf_memory_, world_sdf_view_);
        destroy_image_2d(world_chart_sdf_image_, world_chart_sdf_memory_, world_chart_sdf_view_);
        destroy_image_2d(world_palette_image_, world_palette_memory_, world_palette_view_);
    }
    world_page_streamer_.reset();
    world_patch_staging_.clear();
    world_patch_count_ = 0u;
    world_last_uploaded_ = 0u;
    world_palette_cpu_.clear();
    world_pack_ready_ = false;
    world_palette_dirty_ = true;
    world_palette_layout_initialized_ = false;
    world_pyramid_layout_initialized_.fill(false);
}

void VulkanDesktopBackend::debug_dump_stream_state() const {
    std::fprintf(stderr,
                 "[diag] patches=%u lod=%.3f resident=%u remaining=%u\n",
                 world_patch_count_, static_cast<double>(world_lod_),
                 world_page_streamer_ ? world_page_streamer_->resident_count() : 0u,
                 world_page_streamer_ ? world_page_streamer_->remaining() : 0u);
}

void VulkanDesktopBackend::dump_height_pyramid(VkCommandBuffer command) {
    constexpr VkDeviceSize bytes = static_cast<VkDeviceSize>(kHeightPyramidW) *
                                   kHeightPyramidH * 2u;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = bytes;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkcheck(vkCreateBuffer(device_, &buffer_info, nullptr, &world_dump_buffer_),
            "vkCreateBuffer(pyramid dump)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, world_dump_buffer_, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = find_memory_type(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkcheck(vkAllocateMemory(device_, &allocate, nullptr, &world_dump_memory_),
            "vkAllocateMemory(pyramid dump)");
    vkcheck(vkBindBufferMemory(device_, world_dump_buffer_, world_dump_memory_, 0),
            "vkBindBufferMemory(pyramid dump)");
    vkMapMemory(device_, world_dump_memory_, 0, VK_WHOLE_SIZE, 0, &world_dump_mapped_);

    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.buffer = world_dump_buffer_;
    barrier.size = bytes;
    VkImageMemoryBarrier2 image_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    image_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.image = world_height_pyramid_image_;
    image_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &barrier;
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = &image_barrier;
    vkCmdPipelineBarrier2(command, &dependency);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    copy.imageExtent = {kHeightPyramidW, kHeightPyramidH, 1u};
    vkCmdCopyImageToBuffer(command, world_height_pyramid_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, world_dump_buffer_,
                           1u, &copy);

    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    vkCmdPipelineBarrier2(command, &dependency);
    world_dump_pending_ = true;
}

void VulkanDesktopBackend::dump_page_pyramids(VkCommandBuffer command) {
    constexpr VkDeviceSize bytes = static_cast<VkDeviceSize>(kPagePyramidW) *
                                   kPagePyramidH * 2u;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    // Headroom beyond the SDF plane for the patch-buffer readback slice.
    buffer_info.size = bytes + 128u * 1024u;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkcheck(vkCreateBuffer(device_, &buffer_info, nullptr, &world_dump2_buffer_),
            "vkCreateBuffer(page dump)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, world_dump2_buffer_, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = find_memory_type(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkcheck(vkAllocateMemory(device_, &allocate, nullptr, &world_dump2_memory_),
            "vkAllocateMemory(page dump)");
    vkcheck(vkBindBufferMemory(device_, world_dump2_buffer_, world_dump2_memory_, 0),
            "vkBindBufferMemory(page dump)");
    vkMapMemory(device_, world_dump2_memory_, 0, VK_WHOLE_SIZE, 0, &world_dump2_mapped_);

    auto barrier_for = [&](VkImage image) {
        VkImageMemoryBarrier2 image_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                     VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        image_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.image = image;
        image_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        return image_barrier;
    };
    VkBufferMemoryBarrier2 buffer_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    buffer_barrier.buffer = world_dump2_buffer_;
    buffer_barrier.size = bytes;
    VkImageMemoryBarrier2 barriers[1] = {barrier_for(world_sdf_image_)};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &buffer_barrier;
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(command, &dependency);

    VkBufferImageCopy copies[1]{};
    copies[0].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    copies[0].imageExtent = {kPagePyramidW, kPagePyramidH, 1u};
    vkCmdCopyImageToBuffer(command, world_sdf_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, world_dump2_buffer_,
                           1u, copies);

    for (auto& image_barrier : barriers) {
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                     VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    buffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    buffer_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    vkCmdPipelineBarrier2(command, &dependency);
}

void VulkanDesktopBackend::stream_world_pages() {
    // TEMP DIAG: THUNDER_STREAM_CSV=1 prints one CSV row per frame.
    struct StreamDiag {
        std::chrono::steady_clock::time_point t0;
        std::uint64_t frame;
        std::uint32_t patches = 0u;
        std::uint32_t uploaded = 0u;
        std::uint32_t resident = 0u;
        float lod = 0.0f;
        ~StreamDiag() {
            if (std::getenv("THUNDER_STREAM_CSV") == nullptr) return;
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - t0).count();
            std::fprintf(stderr, "CSV_STREAM,%llu,%u,%u,%u,%.3f,%lld\n",
                         static_cast<unsigned long long>(frame), patches, uploaded, resident,
                         static_cast<double>(lod), static_cast<long long>(us));
        }
    } diag{std::chrono::steady_clock::now(), frames_presented_};
    if (!world_pack_ready_ || !world_page_streamer_ || !world_page_streamer_->ready()) return;

    const auto now = std::chrono::steady_clock::now();
    if (world_have_stream_time_) {
        const double dt = std::chrono::duration<double>(now - world_last_stream_time_).count();
        world_frame_dt_ = static_cast<float>(std::clamp(dt, 1.0 / 240.0, 1.0 / 15.0));
    }
    world_last_stream_time_ = now;
    world_have_stream_time_ = true;

    const std::array<float, 4> view{{map_view_[0], map_view_[1], map_view_[2], map_view_[3]}};
    // TEMP DIAG: the dump copy from the previous frame has long completed by
    // the time this frame's work starts; read it back and write a raw file.
    if (world_dump_pending_) {
        world_dump_pending_ = false;
        const char* path = std::getenv("THUNDER_DUMP_PYRAMID");
        if (path != nullptr && path[0] != '\0') {
            std::FILE* file = std::fopen(path, "wb");
            if (file != nullptr) {
                std::fwrite(world_dump_mapped_, 1,
                            static_cast<std::size_t>(kHeightPyramidW) * kHeightPyramidH * 2u,
                            file);
                std::fclose(file);
                std::fprintf(stderr, "[diag] height pyramid dumped to %s\n", path);
            }
            std::string sdf_path = std::string(path) + ".sdf";
            file = std::fopen(sdf_path.c_str(), "wb");
            if (file != nullptr) {
                std::fwrite(world_dump2_mapped_, 1,
                            static_cast<std::size_t>(kPagePyramidW) * kPagePyramidH * 2u +
                                static_cast<std::size_t>(world_patch_buffer_bytes_),
                            file);
                std::fclose(file);
                std::fprintf(stderr, "[diag] sdf pyramid dumped to %s\n", sdf_path.c_str());
            }
        }
        vkUnmapMemory(device_, world_dump_memory_);
        vkDestroyBuffer(device_, world_dump_buffer_, nullptr);
        vkFreeMemory(device_, world_dump_memory_, nullptr);
        world_dump_buffer_ = VK_NULL_HANDLE;
        world_dump_memory_ = VK_NULL_HANDLE;
        world_dump_mapped_ = nullptr;
        vkUnmapMemory(device_, world_dump2_memory_);
        vkDestroyBuffer(device_, world_dump2_buffer_, nullptr);
        vkFreeMemory(device_, world_dump2_memory_, nullptr);
        world_dump2_buffer_ = VK_NULL_HANDLE;
        world_dump2_memory_ = VK_NULL_HANDLE;
        world_dump2_mapped_ = nullptr;
    }
    // Startup: queue every page of every level once (coarse levels first).
    world_page_streamer_->enqueue_all_pages(view);
    world_page_streamer_->set_frame_dt(world_frame_dt_);

    // Fractional-LOD patch build. The render viewport stays tied to the
    // requested camera every frame so movement never lags the decode pipeline;
    // the residency fallback already handles not-yet-arrived pages. The LOD
    // measures pixels against the target the world pass renders into.
    const float screen_width_px = static_cast<float>(
        scene_extent_.width != 0u ? scene_extent_.width : extent_.width);
    world_lod_ = WorldMapPageStreamer::lod_fraction(view, screen_width_px);
    world_page_streamer_->build_world_patches(view, screen_width_px,
                                              frames_presented_, world_patch_staging_);
    world_patch_count_ = std::min<std::size_t>(world_patch_staging_.size(), kWorldPatchCapacity);

    std::copy(std::begin(map_view_), std::end(map_view_), std::begin(world_render_view_));
    world_render_camera_[0] = map_camera_[0];
    world_render_camera_[1] = map_camera_[1];
    world_render_camera_[2] = 0.0f;
    world_render_camera_[3] = 0.0f;

    auto& patch_frame = world_patch_frames_[frame_index_];
    // The frame fence has completed before this preparation step. Each in-flight
    // frame owns its identity buffer; ownership updates never race GPU reads.
    auto& political_frame = world_political_frames_[frame_index_];
    if (political_frame.mapped != nullptr &&
        world_political_frame_revision_[frame_index_] != world_political_revision_) {
        std::memcpy(political_frame.mapped, world_political_cpu_.data(), world_political_bytes_);
        world_political_frame_revision_[frame_index_] = world_political_revision_;
    }
    if (patch_frame.mapped != nullptr && world_patch_count_ != 0u) {
        std::memcpy(patch_frame.mapped, world_patch_staging_.data(),
                    static_cast<std::size_t>(world_patch_count_) * sizeof(WorldMapPatchGpu));
        // TEMP DIAG: dump the first few patches once per run.
        static bool patch_dumped = false;
        if (!patch_dumped && frames_presented_ > 200u) {
            patch_dumped = true;
            for (std::size_t i = 0; i < std::min<std::size_t>(world_patch_count_, 4u); ++i) {
                const auto& p = world_patch_staging_[i];
                std::fprintf(stderr, "[diag] patch %zu rect=(%.4f, %.4f, %.4f, %.4f) lvl=%u/%u m=%.3f\n",
                             i, p.rect[0], p.rect[1], p.rect[2], p.rect[3],
                             p.level_a, p.level_b, p.morph);
            }
        }
    }

    diag.patches = world_patch_count_;
    diag.uploaded = world_last_uploaded_;
    diag.resident = world_page_streamer_->resident_count();
    diag.lod = world_lod_;
}

void VulkanDesktopBackend::record_world_page_uploads(VkCommandBuffer command) {
    // TEMP DIAG: THUNDER_STREAM_CSV=1 prints one CSV row per frame.
    struct UploadDiag {
        std::chrono::steady_clock::time_point t0;
        std::uint32_t count = 0u;
        ~UploadDiag() {
            if (std::getenv("THUNDER_STREAM_CSV") == nullptr) return;
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - t0).count();
            std::fprintf(stderr, "CSV_UPLOAD,%u,%lld\n", count, static_cast<long long>(us));
        }
    } diag{std::chrono::steady_clock::now()};
    if (!world_pack_ready_ || !world_page_streamer_) return;
    auto& staging = world_upload_frames_[frame_index_];
    if (staging.buffer == VK_NULL_HANDLE || staging.mapped == nullptr) return;
    auto* staging_base = static_cast<std::byte*>(staging.mapped);

    if (world_palette_dirty_)
        std::memcpy(staging_base, world_palette_cpu_.data(), world_palette_bytes_);

    // Staging partition per frame: [height pages | province planes | sdf planes].
    // Height pages are 8450 payload bytes at an 8452-byte pitch (the 2-byte
    // gap keeps every offset 4-byte aligned); the u16 planes are contiguous.
    const auto page_data_base = static_cast<VkDeviceSize>(world_palette_bytes_);
    world_page_streamer_->pop_decoded(static_cast<std::uint32_t>(frames_presented_),
                                      kWorldUploadPageBudget);
    const auto& window = world_page_streamer_->upload_window();
    const auto upload_count = std::min<std::size_t>(window.size(), kWorldUploadPageBudget);
    diag.count = static_cast<std::uint32_t>(upload_count);
    world_last_uploaded_ = static_cast<std::uint32_t>(upload_count);

    std::array<VkBufferImageCopy, kWorldUploadPageBudget> height_regions{};
    std::array<VkBufferImageCopy, kWorldUploadPageBudget> province_regions{};
    std::array<VkBufferImageCopy, kWorldUploadPageBudget> sdf_regions{};
    std::array<VkBufferImageCopy, kWorldUploadPageBudget> chart_regions{};
    const auto province_partition = page_data_base +
        static_cast<VkDeviceSize>(kWorldUploadPageBudget) * kHeightPageStride;
    const auto sdf_partition = province_partition +
        static_cast<VkDeviceSize>(kWorldUploadPageBudget) * kPagePlaneBytes;
    const auto chart_partition = sdf_partition +
        static_cast<VkDeviceSize>(kWorldUploadPageBudget) * kPagePlaneBytes;
    for (std::size_t index = 0; index < upload_count; ++index) {
        const auto& [key, page] = window[index];
        const auto& layout = kWorldLevels[key.level];
        const auto texture_y = (layout.rows - 1u) - static_cast<std::uint32_t>(key.y);
        const auto n = static_cast<std::uint32_t>(index);
        const auto offset_h = page_data_base +
                              static_cast<VkDeviceSize>(n) * kHeightPageStride;
        const auto offset_id = province_partition +
                               static_cast<VkDeviceSize>(n) * kPagePlaneBytes;
        const auto offset_sdf = sdf_partition +
                                static_cast<VkDeviceSize>(n) * kPagePlaneBytes;
        const auto offset_chart = chart_partition + static_cast<VkDeviceSize>(n) * kPagePlaneBytes;
        std::memcpy(staging_base + offset_h, page.height.data(), kHeightPageBytes);
        std::memcpy(staging_base + offset_id, page.province.data(), kPagePlaneBytes);
        std::memcpy(staging_base + offset_sdf, page.coast.data(), kPagePlaneBytes);
        std::memcpy(staging_base + offset_chart, page.cartographic_coast.data(), kPagePlaneBytes);

        auto& height_region = height_regions[n];
        height_region.bufferOffset = offset_h;
        // Tight rows: 65 texels = 130 bytes, 8450 bytes in total; the copy
        // must not consume the 2-byte page gap that follows the payload.
        height_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        height_region.imageOffset = {static_cast<std::int32_t>(layout.h_ox + key.x * 65u),
                                     static_cast<std::int32_t>(layout.h_oy + texture_y * 65u), 0};
        height_region.imageExtent = {65u, 65u, 1u};

        auto& province_region = province_regions[n];
        province_region.bufferOffset = offset_id;
        province_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        province_region.imageOffset = {static_cast<std::int32_t>(layout.p_ox + key.x * 128u),
                                       static_cast<std::int32_t>(layout.p_oy + texture_y * 128u), 0};
        province_region.imageExtent = {128u, 128u, 1u};

        auto& sdf_region = sdf_regions[n];
        sdf_region = province_region;
        sdf_region.bufferOffset = offset_sdf;
        chart_regions[n] = province_region;
        chart_regions[n].bufferOffset = offset_chart;
    }
    world_page_streamer_->clear_upload_window();

    auto transition = [&](VkImage image, bool initialized, VkPipelineStageFlags2 source_stage,
                          VkAccessFlags2 source_access, VkPipelineStageFlags2 destination_stage,
                          VkAccessFlags2 destination_access) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = initialized ? source_stage : VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = initialized ? source_access : 0;
        barrier.dstStageMask = destination_stage;
        barrier.dstAccessMask = destination_access;
        barrier.oldLayout = initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                        : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    };
    constexpr VkPipelineStageFlags2 transfer_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    constexpr VkAccessFlags2 transfer_write = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    // The vertex stage displaces geometry from the height pyramid, so the
    // release barrier must cover vertex reads as well as fragment reads.
    constexpr VkPipelineStageFlags2 sample_stages =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    constexpr VkAccessFlags2 shader_read = VK_ACCESS_2_SHADER_READ_BIT;

    if (world_palette_dirty_) {
        transition(world_palette_image_, world_palette_layout_initialized_,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, shader_read,
                   transfer_stage, transfer_write);
    }
    if (upload_count != 0u) {
        transition(world_height_pyramid_image_, world_pyramid_layout_initialized_[0],
                   sample_stages, shader_read, transfer_stage, transfer_write);
        transition(world_province_image_, world_pyramid_layout_initialized_[1],
                   sample_stages, shader_read, transfer_stage, transfer_write);
        transition(world_sdf_image_, world_pyramid_layout_initialized_[2],
                   sample_stages, shader_read, transfer_stage, transfer_write);
        transition(world_chart_sdf_image_, world_pyramid_layout_initialized_[3],
                   sample_stages, shader_read, transfer_stage, transfer_write);
        if (!world_pyramid_layout_initialized_[0] || !world_pyramid_layout_initialized_[1] ||
            !world_pyramid_layout_initialized_[2]) {
            // One-time deterministic clear: never-uploaded texels (the unused
            // half of level 3's bottom row, band padding) must not read as
            // allocation garbage once the samplers can reach them.
            VkClearColorValue zero{};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
            VkImageSubresourceRange ranges[3] = {range, range, range};
            vkCmdClearColorImage(command, world_height_pyramid_image_,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1u, &ranges[0]);
            vkCmdClearColorImage(command, world_province_image_,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1u, &ranges[1]);
            vkCmdClearColorImage(command, world_sdf_image_,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1u, &ranges[2]);
            vkCmdClearColorImage(command, world_chart_sdf_image_,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1u, &range);
        }

        vkCmdCopyBufferToImage(command, staging.buffer, world_height_pyramid_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(upload_count), height_regions.data());
        vkCmdCopyBufferToImage(command, staging.buffer, world_province_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(upload_count), province_regions.data());
        vkCmdCopyBufferToImage(command, staging.buffer, world_sdf_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(upload_count), sdf_regions.data());
        vkCmdCopyBufferToImage(command, staging.buffer, world_chart_sdf_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(upload_count), chart_regions.data());

        auto release = [&](VkImage image, bool& initialized) {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = transfer_stage;
            barrier.srcAccessMask = transfer_write;
            barrier.dstStageMask = sample_stages;
            barrier.dstAccessMask = shader_read;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(command, &dependency);
            initialized = true;
        };
        release(world_height_pyramid_image_, world_pyramid_layout_initialized_[0]);
        release(world_province_image_, world_pyramid_layout_initialized_[1]);
        release(world_sdf_image_, world_pyramid_layout_initialized_[2]);
        release(world_chart_sdf_image_, world_pyramid_layout_initialized_[3]);
    }
    // TEMP DIAG: THUNDER_DUMP_PYRAMID=1 dumps the resident height pyramid once
    // everything is resident, for offline inspection.
    if (world_page_streamer_->all_resident() && std::getenv("THUNDER_DUMP_PYRAMID") != nullptr &&
        !world_pyramid_dumped_) {
        world_pyramid_dumped_ = true;
        dump_height_pyramid(command);
        dump_page_pyramids(command);
        // TEMP DIAG: also read back the patch SSBO the GPU actually sees.
        VkBufferCopy buffer_copy{};
        buffer_copy.srcOffset = 0u;
        buffer_copy.dstOffset = static_cast<VkDeviceSize>(kPagePyramidW) * kPagePyramidH * 2u;
        buffer_copy.size = static_cast<VkDeviceSize>(world_patch_buffer_bytes_);
        vkCmdCopyBuffer(command, world_patch_frames_[frame_index_].buffer,
                        world_dump2_buffer_, 1u, &buffer_copy);
    }
    if (world_palette_dirty_) {
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        copy.imageExtent = {256u, 256u, 1u};
        vkCmdCopyBufferToImage(command, staging.buffer, world_palette_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = transfer_stage;
        barrier.srcAccessMask = transfer_write;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = shader_read;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = world_palette_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
        world_palette_layout_initialized_ = true;
        world_palette_dirty_ = false;
    }
}

} // namespace thunder
