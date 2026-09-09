#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include "thunder/presentation/ui/FontAtlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
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

} // namespace

void VulkanDesktopBackend::load_ui_font_metrics() {
    ui_font_metrics_.reset();
    ui_font_slots_.clear();
    ui_font_cell_ = 0;
    ui_font_columns_ = 0;

    auto metrics_path = ui_font_metrics_path_;
    if (metrics_path.empty()) {
        metrics_path = ui_font_atlas_path_;
        const auto stem = metrics_path.stem().string();
        if (stem.size() > 6u && stem.ends_with("_atlas")) {
            metrics_path.replace_filename(stem.substr(0u, stem.size() - 6u) + ".thunderfont");
        } else {
            metrics_path.replace_extension(".thunderfont");
        }
    }
    if (std::filesystem::is_regular_file(metrics_path)) {
        ui_font_metrics_ = std::make_unique<FontAtlas>(FontAtlas::read(metrics_path));
        return;
    }

    // Compatibility with the temporary fixed-cell atlas format used by early
    // desktop previews. New content should always provide .thunderfont metrics so
    // glyph bearings, advances and MSDF coverage survive DPI and font changes.
    auto slot_path = ui_font_atlas_path_;
    slot_path.replace_extension(".map");
    if (std::filesystem::is_regular_file(slot_path)) {
        load_font_slots(slot_path);
        return;
    }
    throw std::runtime_error("UI font atlas has neither .thunderfont metrics nor a legacy .map sidecar");
}

void VulkanDesktopBackend::load_font_slots(const std::filesystem::path& path) {
    ui_font_slots_.clear();
    ui_font_width_ = 0;
    ui_font_height_ = 0;
    ui_font_cell_ = 0;
    ui_font_columns_ = 0;
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open UI font slot table: " + path.string());
    }
    std::string line;
    bool header = false;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        if (!header) {
            std::string magic;
            row >> magic >> ui_font_width_ >> ui_font_height_ >> ui_font_cell_ >> ui_font_columns_;
            if (magic != "THUNDER_FONT_ATLAS" || ui_font_width_ == 0u || ui_font_height_ == 0u ||
                ui_font_cell_ == 0u || ui_font_columns_ == 0u) {
                throw std::runtime_error("invalid UI font atlas header");
            }
            header = true;
            const auto cell_width = static_cast<std::uint64_t>(ui_font_cell_) * ui_font_columns_;
            if (cell_width > ui_font_width_ || ui_font_width_ % ui_font_cell_ != 0u ||
                ui_font_height_ % ui_font_cell_ != 0u) {
                throw std::runtime_error("UI font atlas grid exceeds image dimensions");
            }
            continue;
        }
        std::uint32_t codepoint = 0;
        std::uint32_t slot = 0;
        if (!(row >> codepoint >> slot) || codepoint > 0x10ffffu || slot >= 1'000'000u) {
            throw std::runtime_error("invalid UI font atlas slot");
        }
        if (!ui_font_slots_.emplace(codepoint, slot).second) {
            throw std::runtime_error("duplicate UI font atlas codepoint");
        }
        const auto slot_column = static_cast<std::uint64_t>(slot) % ui_font_columns_;
        const auto slot_row = static_cast<std::uint64_t>(slot) / ui_font_columns_;
        if ((slot_column + 1u) * ui_font_cell_ > ui_font_width_ ||
            (slot_row + 1u) * ui_font_cell_ > ui_font_height_) {
            throw std::runtime_error("UI font atlas slot exceeds image dimensions");
        }
    }
    if (!header || ui_font_slots_.empty()) throw std::runtime_error("empty UI font atlas slot table");
}

void VulkanDesktopBackend::create_ui_image(const std::filesystem::path& path,
                                           VkImage& image,
                                           VkDeviceMemory& memory,
                                           VkImageView& view,
                                           VkSampler& sampler,
                                           std::uint32_t& width,
                                           std::uint32_t& height,
                                           bool repeat_horizontal,
                                           bool nearest_filter,
                                           bool generate_mipmaps) {
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    sampler = VK_NULL_HANDLE;
    width = 0;
    height = 0;
    if (path.empty()) return;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open UI image: " + path.string());
    const auto end = input.tellg();
    if (end < static_cast<std::streamoff>(24) ||
        static_cast<std::uint64_t>(end) > 256ull * 1024ull * 1024ull) {
        throw std::runtime_error("invalid UI image size: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("failed reading UI image: " + path.string());
    auto read_u32 = [&](std::size_t offset) {
        if (offset + 4u > bytes.size()) throw std::runtime_error("truncated UI image header");
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1u])) << 8u) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2u])) << 16u) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3u])) << 24u);
    };
    const char expected_magic[] = {'T','H','U','N','D','R','I','M'};
    for (std::size_t i = 0; i < sizeof(expected_magic); ++i) {
        if (std::to_integer<char>(bytes[i]) != expected_magic[i])
            throw std::runtime_error("invalid UI image magic: " + path.string());
    }
    width = read_u32(8);
    height = read_u32(12);
    const auto payload_size = static_cast<std::uint64_t>(read_u32(16)) |
                              (static_cast<std::uint64_t>(read_u32(20)) << 32u);
    if (width == 0u || height == 0u || width > 8192u || height > 8192u ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull != payload_size ||
        payload_size > 256ull * 1024ull * 1024ull || bytes.size() != 24u + payload_size) {
        throw std::runtime_error("invalid UI image dimensions or payload: " + path.string());
    }
    std::uint32_t mip_levels = 1u;
    if (generate_mipmaps && !nearest_filter) {
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R8G8B8A8_UNORM,
                                            &format_properties);
        if ((format_properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0u) {
            mip_levels = 1u + static_cast<std::uint32_t>(
                std::floor(std::log2(static_cast<double>(std::max(width, height)))));
        }
    }
    const auto* pixels = bytes.data() + 24u;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    try {
        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = static_cast<VkDeviceSize>(payload_size);
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkcheck(vkCreateBuffer(device_, &buffer_info, nullptr, &staging), "vkCreateBuffer(ui image staging)");
        VkMemoryRequirements staging_requirements{};
        vkGetBufferMemoryRequirements(device_, staging, &staging_requirements);
        VkMemoryAllocateInfo staging_allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        staging_allocate.allocationSize = staging_requirements.size;
        staging_allocate.memoryTypeIndex = find_memory_type(
            staging_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vkAllocateMemory(device_, &staging_allocate, nullptr, &staging_memory),
                "vkAllocateMemory(ui image staging)");
        vkcheck(vkBindBufferMemory(device_, staging, staging_memory, 0),
                "vkBindBufferMemory(ui image staging)");
        void* mapped = nullptr;
        vkcheck(vkMapMemory(device_, staging_memory, 0, staging_requirements.size, 0, &mapped),
                "vkMapMemory(ui image staging)");
        std::memcpy(mapped, pixels, static_cast<std::size_t>(payload_size));
        vkUnmapMemory(device_, staging_memory);

        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {width, height, 1u};
        image_info.mipLevels = mip_levels;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           (mip_levels > 1u ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u);
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkcheck(vkCreateImage(device_, &image_info, nullptr, &image), "vkCreateImage(ui image)");
        VkMemoryRequirements image_requirements{};
        vkGetImageMemoryRequirements(device_, image, &image_requirements);
        VkMemoryAllocateInfo image_allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        image_allocate.allocationSize = image_requirements.size;
        image_allocate.memoryTypeIndex = find_memory_type(image_requirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkcheck(vkAllocateMemory(device_, &image_allocate, nullptr, &memory),
                "vkAllocateMemory(ui image)");
        vkcheck(vkBindImageMemory(device_, image, memory, 0), "vkBindImageMemory(ui image)");

        VkCommandBufferAllocateInfo command_allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command_allocate.commandPool = command_pool_;
        command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_allocate.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        vkcheck(vkAllocateCommandBuffers(device_, &command_allocate, &command),
                "vkAllocateCommandBuffers(ui image)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkcheck(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer(ui image)");
        VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to_transfer.srcAccessMask = 0;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.image = image;
        to_transfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1u};
        vkCmdCopyBufferToImage(command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        std::int32_t mip_width = static_cast<std::int32_t>(width);
        std::int32_t mip_height = static_cast<std::int32_t>(height);
        for (std::uint32_t level = 1u; level < mip_levels; ++level) {
            VkImageMemoryBarrier source_to_blit{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            source_to_blit.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            source_to_blit.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            source_to_blit.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            source_to_blit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            source_to_blit.image = image;
            source_to_blit.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1u, 1u, 0u, 1u};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                 0, nullptr, 1, &source_to_blit);

            const std::int32_t next_width = std::max(mip_width / 2, 1);
            const std::int32_t next_height = std::max(mip_height / 2, 1);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1u, 0u, 1u};
            blit.srcOffsets[1] = {mip_width, mip_height, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0u, 1u};
            blit.dstOffsets[1] = {next_width, next_height, 1};
            vkCmdBlitImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            VkImageMemoryBarrier source_to_shader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            source_to_shader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            source_to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            source_to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            source_to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            source_to_shader.image = image;
            source_to_shader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1u, 1u, 0u, 1u};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                                 0, nullptr, 1, &source_to_shader);
            mip_width = next_width;
            mip_height = next_height;
        }

        VkImageMemoryBarrier last_to_shader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        last_to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        last_to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        last_to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        last_to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        last_to_shader.image = image;
        last_to_shader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip_levels - 1u, 1u, 0u, 1u};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &last_to_shader);
        vkcheck(vkEndCommandBuffer(command), "vkEndCommandBuffer(ui image)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        vkcheck(vkQueueSubmit(graphics_queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit(ui image)");
        vkcheck(vkQueueWaitIdle(graphics_queue_), "vkQueueWaitIdle(ui image)");
        vkFreeCommandBuffers(device_, command_pool_, 1, &command);

        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1};
        vkcheck(vkCreateImageView(device_, &view_info, nullptr, &view), "vkCreateImageView(ui image)");
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = nearest_filter ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler_info.minFilter = nearest_filter ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = repeat_horizontal ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                      : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = static_cast<float>(mip_levels - 1u);
        vkcheck(vkCreateSampler(device_, &sampler_info, nullptr, &sampler), "vkCreateSampler(ui image)");
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_memory, nullptr);
    } catch (...) {
        if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device_, staging, nullptr);
        if (staging_memory != VK_NULL_HANDLE) vkFreeMemory(device_, staging_memory, nullptr);
        destroy_ui_image(image, memory, view, sampler);
        throw;
    }
}

void VulkanDesktopBackend::destroy_ui_image(VkImage& image, VkDeviceMemory& memory,
                                            VkImageView& view, VkSampler& sampler) noexcept {
    if (device_ == VK_NULL_HANDLE) return;
    if (sampler != VK_NULL_HANDLE) vkDestroySampler(device_, sampler, nullptr);
    if (view != VK_NULL_HANDLE) vkDestroyImageView(device_, view, nullptr);
    if (image != VK_NULL_HANDLE) vkDestroyImage(device_, image, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device_, memory, nullptr);
    sampler = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}



} // namespace thunder
