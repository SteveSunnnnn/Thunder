#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include <algorithm>
#include <array>

namespace thunder {

void VulkanDesktopBackend::record_tonemap(VkCommandBuffer command) const {
    if (tonemap_pipeline_ == VK_NULL_HANDLE) {
        return;
    }
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = extent_;
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_pipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_layout_,
                            0, 1, &hdr_descriptor_set_, 0, nullptr);
    // params: x = srgb_output, y = dither, z = exposure, w = vignette strength
    const std::array<float, 8> tonemap_push{{
        0.5f, 0.5f, 0.5f, 0.5f,
        srgb_swapchain_ ? 1.0f : 0.0f,
        settings_.dither ? 1.0f : 0.0f,
        1.0f,
        0.08f}};
    vkCmdPushConstants(command, tonemap_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(tonemap_push), tonemap_push.data());
    vkCmdDraw(command, 3, 1, 0, 0);
    ++draw_calls_;
}

// FXAA consumes the tonemapped LDR intermediate, not the HDR scene: edge
// detection has to run in display space to be perceptually meaningful.
void VulkanDesktopBackend::record_fxaa(VkCommandBuffer command) const {
    if (fxaa_pipeline_ == VK_NULL_HANDLE || fxaa_descriptor_set_ == VK_NULL_HANDLE) {
        return;
    }
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = extent_;
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaa_pipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_layout_,
                            0, 1, &fxaa_descriptor_set_, 0, nullptr);
    const std::array<float, 8> fxaa_push{{
        0.5f, 0.5f, 0.5f, 0.5f,
        1.0f / static_cast<float>(std::max(extent_.width, 1u)),
        1.0f / static_cast<float>(std::max(extent_.height, 1u)),
        0.0f, 0.0f}};
    vkCmdPushConstants(command, tonemap_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(fxaa_push), fxaa_push.data());
    vkCmdDraw(command, 3, 1, 0, 0);
    ++draw_calls_;
}

} // namespace thunder
