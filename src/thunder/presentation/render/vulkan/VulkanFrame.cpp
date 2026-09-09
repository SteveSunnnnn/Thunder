#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

void VulkanDesktopBackend::recreate_swapchain() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(recreate_swapchain)");

    // A resize changes the extent but never the formats, so every pipeline
    // stays valid. Rebuilding only the swapchain and the size-dependent
    // offscreen targets avoids recompiling SPIR-V and re-reading the font
    // atlas from disk on every window drag. Combined with the pipeline cache
    // this turns a resize from a multi-hundred-millisecond stall into a
    // sub-millisecond one.
    destroy_scene_targets();
    destroy_swapchain();
    create_swapchain();
    if (runtime_renderer_enabled_) {
        create_scene_targets();
    }
}

void VulkanDesktopBackend::draw_frame() {
    // Interval since the previous frame is the number that maps to FPS.
    const auto now = std::chrono::steady_clock::now();
    double frame_ms = 0.0;
    if (have_last_frame_time_) {
        frame_ms = std::chrono::duration<double, std::milli>(now - last_frame_time_).count();
    }
    last_frame_time_ = now;
    have_last_frame_time_ = true;
    frame_start_ = now;

    auto& frame = frames_[frame_index_];
    vkcheck(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    upload_map_overlay_frame();

    // The fence for this slot just completed, so the timestamp pair recorded
    // into its query pool on the previous visit to this slot is now readable.
    // Sampling here costs nothing: no stall, results are already available.
    collect_gpu_timing(frame_index_);
    const double gpu_ms = last_gpu_ms_;

    std::uint32_t image_index = 0;
    const auto acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.acquired,
                                                VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        vkcheck(acquire, "vkAcquireNextImageKHR");
    }

    std::optional<std::filesystem::path> screenshot_path;
    const auto submission_start = std::chrono::steady_clock::now();
    if (pending_screenshot_path_) {
        if (!swapchain_transfer_source_)
            throw std::runtime_error("swapchain does not support screenshot transfer reads");
        const VkDeviceSize required = static_cast<VkDeviceSize>(extent_.width) *
                                      static_cast<VkDeviceSize>(extent_.height) * 4u;
        if (screenshot_buffer_.capacity < required) {
            destroy_mapped_host_buffer(screenshot_buffer_.buffer,
                                       screenshot_buffer_.memory,
                                       screenshot_buffer_.mapped);
            create_mapped_host_buffer(required, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      screenshot_buffer_.buffer,
                                      screenshot_buffer_.memory,
                                      screenshot_buffer_.mapped);
            screenshot_buffer_.capacity = required;
        }
        screenshot_path = std::move(pending_screenshot_path_);
        pending_screenshot_path_.reset();
    }

    vkcheck(vkResetFences(device_, 1, &frame.fence), "vkResetFences");
    vkcheck(vkResetCommandBuffer(frame.command, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkcheck(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer");

    draw_calls_ = 0;
    reset_query_pool(frame.command, frame_index_);
    write_gpu_timestamp(frame.command, frame_index_, true);
    // CPU page selection and bounded decode happen after the frame fence has
    // completed; the resulting copies are recorded before the map pass. This
    // keeps the GPU atlas coherent without a full-world upload or a readback.
    stream_world_pages();
    record_world_page_uploads(frame.command);

    auto transition = [&](VkImage image, VkImageAspectFlags aspect,
                          VkImageLayout old_layout, VkImageLayout new_layout,
                          VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                          VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = src_stage;
        barrier.srcAccessMask = src_access;
        barrier.dstStageMask = dst_stage;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        VkDependencyInfo info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        info.imageMemoryBarrierCount = 1;
        info.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame.command, &info);
    };

    constexpr VkPipelineStageFlags2 kColorOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    constexpr VkAccessFlags2 kColorWrite = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    constexpr VkAccessFlags2 kColorRead = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;

    VkClearValue clear{};
    clear.color = {{0.025f, 0.055f, 0.085f, 1.0f}};

    if (settings_.hdr_target && hdr_targets_created_) {
        // ---- Pass 1: 3D scene into the HDR target -----------------------
        const bool multisampled = settings_.msaa_samples > 1u;
        transition(hdr_image_, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_NONE, 0, kColorOut, kColorWrite | kColorRead);
        if (multisampled) {
            transition(hdr_msaa_resolve_image_, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_NONE, 0, kColorOut, kColorWrite | kColorRead);
        }
        if (depth_view_ != VK_NULL_HANDLE) {
            transition(depth_image_, VK_IMAGE_ASPECT_DEPTH_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_NONE, 0,
                       VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        }

        VkRenderingAttachmentInfo scene_color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        scene_color.imageView = hdr_view_;
        scene_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        scene_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        scene_color.clearValue = clear;
        if (multisampled) {
            // Resolve into the single-sample target and discard the multisampled
            // buffer. Nothing ever samples the MSAA image, so keeping it out of
            // memory is the whole point of using a transient attachment.
            scene_color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            scene_color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            scene_color.resolveImageView = hdr_msaa_resolve_view_;
            scene_color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else {
            scene_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0f, 0u};
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth.imageView = depth_view_;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // Depth is consumed entirely within this pass.
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue = depth_clear;

        VkRenderingInfo scene_rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        scene_rendering.renderArea.extent = extent_;
        scene_rendering.layerCount = 1;
        scene_rendering.colorAttachmentCount = 1;
        scene_rendering.pColorAttachments = &scene_color;
        if (depth_view_ != VK_NULL_HANDLE) {
            scene_rendering.pDepthAttachment = &depth;
        }
        vkCmdBeginRendering(frame.command, &scene_rendering);
        record_runtime_draws(frame.command);
        vkCmdEndRendering(frame.command);

        // Make the resolved scene readable by the post pass.
        transition(multisampled ? hdr_msaa_resolve_image_ : hdr_image_, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   kColorOut, kColorWrite,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

        // ---- Pass 2: tonemap, then FXAA, into the swapchain -------------
        // FXAA cannot run in place on the swapchain, so when it is enabled the
        // tonemap writes an LDR intermediate that FXAA then filters.
        VkRenderingAttachmentInfo post_color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        post_color.imageView = views_[image_index];
        post_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        post_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        post_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        post_color.clearValue = clear;
        VkRenderingInfo post_rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        post_rendering.renderArea.extent = extent_;
        post_rendering.layerCount = 1;
        post_rendering.colorAttachmentCount = 1;
        post_rendering.pColorAttachments = &post_color;

        if (settings_.fxaa && fxaa_view_ != VK_NULL_HANDLE) {
            transition(fxaa_image_, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_NONE, 0, kColorOut, kColorWrite | kColorRead);
            post_color.imageView = fxaa_view_;
            vkCmdBeginRendering(frame.command, &post_rendering);
            record_tonemap(frame.command);
            vkCmdEndRendering(frame.command);

            transition(fxaa_image_, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       kColorOut, kColorWrite,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

            transition(images_[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_NONE, 0, kColorOut, kColorWrite | kColorRead);
            post_color.imageView = views_[image_index];
            vkCmdBeginRendering(frame.command, &post_rendering);
            record_fxaa(frame.command);
            vkCmdEndRendering(frame.command);
        } else {
            transition(images_[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_NONE, 0, kColorOut, kColorWrite | kColorRead);
            vkCmdBeginRendering(frame.command, &post_rendering);
            record_tonemap(frame.command);
            vkCmdEndRendering(frame.command);
        }

        // ---- Pass 3: UI composites on top -------------------------------
        // The HUD is authored in display space, so it is drawn after
        // tonemapping and must never pass through ACES.
        const bool has_ui = !ui_staging_batches_.empty() ||
                             !ui_staging_indices_.empty() ||
                             !ui_staging_modules_.empty() ||
                             map_overlay_frames_[frame_index_].index_count != 0u;
        if (has_ui) {
            post_color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            vkCmdBeginRendering(frame.command, &post_rendering);
            record_ui_draws(frame.command);
            if (ui_staging_indices_.empty() && !ui_submitted_) {
                record_ui_fallback(frame.command);
            }
            vkCmdEndRendering(frame.command);
        }
    } else {
        // Legacy single-pass path: everything renders straight into the
        // swapchain, exactly as before the optimisation work.
        transition(images_[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_NONE, 0, kColorOut, kColorWrite | kColorRead);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = views_[image_index];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue = clear;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = extent_;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(frame.command, &rendering);
        record_runtime_draws(frame.command);
        record_ui_draws(frame.command);
        vkCmdEndRendering(frame.command);
    }

    VkImageMemoryBarrier2 to_present{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    to_present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.image = images_[image_index];
    to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_present.subresourceRange.levelCount = 1;
    to_present.subresourceRange.layerCount = 1;
    if (screenshot_path) {
        VkImageMemoryBarrier2 to_copy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        to_copy.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        to_copy.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        to_copy.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        to_copy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        to_copy.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_copy.image = images_[image_index];
        to_copy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_copy.subresourceRange.levelCount = 1;
        to_copy.subresourceRange.layerCount = 1;
        VkDependencyInfo copy_dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        copy_dependency.imageMemoryBarrierCount = 1;
        copy_dependency.pImageMemoryBarriers = &to_copy;
        vkCmdPipelineBarrier2(frame.command, &copy_dependency);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {extent_.width, extent_.height, 1u};
        vkCmdCopyImageToBuffer(frame.command, images_[image_index],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               screenshot_buffer_.buffer, 1u, &copy);

        to_present.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        to_present.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    VkDependencyInfo present_dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    present_dependency.imageMemoryBarrierCount = 1;
    present_dependency.pImageMemoryBarriers = &to_present;
    vkCmdPipelineBarrier2(frame.command, &present_dependency);
    write_gpu_timestamp(frame.command, frame_index_, false);
    vkcheck(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = frame.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = frame.command;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = image_rendered_[image_index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    vkcheck(vkQueueSubmit2(graphics_queue_, 1, &submit, frame.fence), "vkQueueSubmit2");
    const double submission_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - submission_start).count();

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &image_rendered_[image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index;
    const auto present_result = vkQueuePresentKHR(present_queue_, &present);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    } else {
        vkcheck(present_result, "vkQueuePresentKHR");
    }
    if (screenshot_path) {
        vkcheck(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences(screenshot)");
        write_screenshot_bmp(*screenshot_path);
    }

    const double cpu_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start_).count();
    if (frame_ms > 0.0) {
        update_stats(frame_ms, gpu_ms, cpu_ms, submission_ms);
    }
    stats_.draw_calls_last_frame = draw_calls_;

    frame_index_ = (frame_index_ + 1u) % frames_in_flight;
    ++frames_presented_;
}

void VulkanDesktopBackend::wait_idle() {
    if (device_ != VK_NULL_HANDLE) {
        vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

void VulkanDesktopBackend::write_screenshot_bmp(const std::filesystem::path& path) const {
    if (screenshot_buffer_.mapped == nullptr || extent_.width == 0u || extent_.height == 0u)
        throw std::runtime_error("screenshot buffer is unavailable");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create screenshot: " + path.string());

    const std::uint32_t pixel_bytes = extent_.width * extent_.height * 4u;
    const std::uint32_t file_bytes = 54u + pixel_bytes;
    std::array<std::byte, 54> header{};
    const auto put16 = [&](std::size_t offset, std::uint16_t value) {
        header[offset] = static_cast<std::byte>(value & 0xffu);
        header[offset + 1u] = static_cast<std::byte>(value >> 8u);
    };
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4u; ++byte)
            header[offset + byte] = static_cast<std::byte>((value >> (byte * 8u)) & 0xffu);
    };
    header[0] = static_cast<std::byte>('B');
    header[1] = static_cast<std::byte>('M');
    put32(2u, file_bytes);
    put32(10u, 54u);
    put32(14u, 40u);
    put32(18u, extent_.width);
    put32(22u, extent_.height);
    put16(26u, 1u);
    put16(28u, 32u);
    put32(34u, pixel_bytes);
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));

    const auto* source = static_cast<const std::uint8_t*>(screenshot_buffer_.mapped);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(extent_.width) * 4u);
    const bool rgba = swapchain_format_ == VK_FORMAT_R8G8B8A8_SRGB ||
                      swapchain_format_ == VK_FORMAT_R8G8B8A8_UNORM;
    for (std::uint32_t output_y = 0; output_y < extent_.height; ++output_y) {
        const std::uint32_t source_y = extent_.height - 1u - output_y;
        const auto* source_row = source + static_cast<std::size_t>(source_y) * row.size();
        for (std::uint32_t x = 0; x < extent_.width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * 4u;
            row[offset + 0u] = rgba ? source_row[offset + 2u] : source_row[offset + 0u];
            row[offset + 1u] = source_row[offset + 1u];
            row[offset + 2u] = rgba ? source_row[offset + 0u] : source_row[offset + 2u];
            row[offset + 3u] = 255u;
        }
        output.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size()));
    }
    if (!output) throw std::runtime_error("failed writing screenshot: " + path.string());
}



} // namespace thunder
