#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"
#include "thunder/presentation/ui/ScriptedGui.hpp"
#include "thunder/presentation/ui/FontAtlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace thunder {
namespace {

struct FlagGpuPush {
    std::array<float, 4> placement{};
    std::array<float, 4> motion{};
    std::array<float, 4> primary{};
    std::array<float, 4> secondary{};
    std::array<float, 4> accent{};
};

[[nodiscard]] std::array<float, 4> flag_color(std::uint32_t rgba) noexcept {
    constexpr float inv = 1.0f / 255.0f;
    return {{static_cast<float>((rgba >> 16u) & 0xffu) * inv,
             static_cast<float>((rgba >> 8u) & 0xffu) * inv,
             static_cast<float>(rgba & 0xffu) * inv,
             static_cast<float>((rgba >> 24u) & 0xffu) * inv}};
}

[[nodiscard]] VkRect2D ui_scissor(UiRect rect,
                                  VkExtent2D extent,
                                  float scale_x,
                                  float scale_y) noexcept {
    const auto full = VkRect2D{{0, 0}, extent};
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.w) || !std::isfinite(rect.h) ||
        rect.w <= 0.0f || rect.h <= 0.0f) return full;
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x <= 0.0f || scale_y <= 0.0f) {
        return full;
    }
    const auto x0 = std::clamp(std::floor(rect.x * scale_x), 0.0f, static_cast<float>(extent.width));
    const auto y0 = std::clamp(std::floor(rect.y * scale_y), 0.0f, static_cast<float>(extent.height));
    const auto x1 = std::clamp(std::ceil((rect.x + rect.w) * scale_x), x0, static_cast<float>(extent.width));
    const auto y1 = std::clamp(std::ceil((rect.y + rect.h) * scale_y), y0, static_cast<float>(extent.height));
    return VkRect2D{
        {static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
        {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)}};
}

} // namespace

void VulkanDesktopBackend::record_ui_draws(VkCommandBuffer command) const {
    const auto& overlay = map_overlay_frames_[frame_index_];
    const bool has_overlay = overlay.index_count != 0u &&
                             overlay.vertex_buffer != VK_NULL_HANDLE &&
                             overlay.index_buffer != VK_NULL_HANDLE;
    const bool has_dynamic = !(ui_staging_indices_.empty() && ui_staging_modules_.empty());
    if (!runtime_renderer_enabled_ || ui_pipeline_ == VK_NULL_HANDLE ||
        (!has_overlay && !has_dynamic)) return;
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D full{{0, 0}, extent_};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &full);

    // The vector-border overlay is the bottommost UI layer. It is drawn from
    // the resident GPU buffers first, then the dynamic draw-list batches are
    // drawn on top exactly as before.
    if (has_overlay) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
        const std::array<float, 4> ui_constants{{
            1.0f / std::max(ui_logical_width_, 1.0f),
            1.0f / std::max(ui_logical_height_, 1.0f),
            0.0f, 0.0f}};
        vkCmdPushConstants(command, ui_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(ui_constants), ui_constants.data());
        const VkDeviceSize overlay_vb_offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &overlay.vertex_buffer, &overlay_vb_offset);
        vkCmdBindIndexBuffer(command, overlay.index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, overlay.index_count, 1, 0, 0, 0);
        ++draw_calls_;
    }

    if (!has_dynamic) return;
    const auto& frame = ui_frame_buffers_[frame_index_];
    if (frame.vertex_buffer == VK_NULL_HANDLE || frame.index_buffer == VK_NULL_HANDLE) return;
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &frame.vertex_buffer, &offset);
    vkCmdBindIndexBuffer(command, frame.index_buffer, 0, VK_INDEX_TYPE_UINT32);
    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    VkDescriptorSet bound_descriptor = VK_NULL_HANDLE;
    std::size_t module_index = 0;
    const auto draw_modules_before = [&](std::uint64_t order) {
        while (module_index < ui_staging_modules_.size() &&
               ui_staging_modules_[module_index].order < order) {
            const auto& module = ui_staging_modules_[module_index++];
            if (module.module == ui_stable_key("dynamic_flag"))
                record_dynamic_flag_draw(command, module);
            vkCmdBindVertexBuffers(command, 0, 1, &frame.vertex_buffer, &offset);
            vkCmdBindIndexBuffer(command, frame.index_buffer, 0, VK_INDEX_TYPE_UINT32);
            bound_pipeline = VK_NULL_HANDLE;
            bound_descriptor = VK_NULL_HANDLE;
        }
    };
    for (const auto& batch : ui_staging_batches_) {
        if (batch.index_count == 0u) continue;
        draw_modules_before(batch.order);
        const bool is_map_msdf = batch.kind == UiBatchKind::MapMsdfText &&
                                 ui_font_metrics_ != nullptr &&
                                 ui_font_descriptor_set_ != VK_NULL_HANDLE;
        const bool msdf_text = (batch.kind == UiBatchKind::MsdfText || is_map_msdf) &&
                               ui_font_metrics_ != nullptr &&
                               ui_font_descriptor_set_ != VK_NULL_HANDLE;
        const bool font_texture = batch.kind == UiBatchKind::Textured &&
                                  batch.texture == kUiFontTextureKey &&
                                  ui_font_descriptor_set_ != VK_NULL_HANDLE;
        const bool sampled = font_texture || msdf_text;
        const auto pipeline = msdf_text ? ui_msdf_pipeline_ :
                              (sampled ? ui_textured_pipeline_ : ui_pipeline_);
        if (pipeline == VK_NULL_HANDLE) continue;
        if (pipeline != bound_pipeline) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            const std::array<float, 4> ui_constants{{
                1.0f / std::max(ui_logical_width_, 1.0f),
                1.0f / std::max(ui_logical_height_, 1.0f),
                msdf_text ? ui_font_metrics_->px_range() : 0.0f,
                is_map_msdf ? 1.0f : 0.0f}};
            const auto layout = sampled ? ui_textured_layout_ : ui_layout_;
            vkCmdPushConstants(command, layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(ui_constants), ui_constants.data());
            bound_pipeline = pipeline;
        }
        vkCmdSetScissor(command, 0, 1, &batch.scissor);
        if (sampled) {
            const auto descriptor = ui_font_descriptor_set_;
            if (descriptor != bound_descriptor) {
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_textured_layout_,
                                        0, 1, &descriptor, 0, nullptr);
                bound_descriptor = descriptor;
            }
        }
        vkCmdDrawIndexed(command, batch.index_count, 1, batch.first_index, 0, 0);
    }
    draw_modules_before(std::numeric_limits<std::uint64_t>::max());
}

void VulkanDesktopBackend::record_dynamic_flag_draw(VkCommandBuffer command,
                                                     const UiModuleSlot& slot) const {
    if (!dynamic_flag_ || flag_pipeline_ == VK_NULL_HANDLE ||
        flag_vertices_ == VK_NULL_HANDLE || flag_indices_ == VK_NULL_HANDLE ||
        flag_index_count_ == 0u || extent_.width == 0u || extent_.height == 0u) return;

    const auto& config = dynamic_flag_->config();
    FlagGpuPush push;
    // Placement comes exclusively from the declarative UI module slot. The
    // flag fills the slot and is clipped by it, so changing HUD composition
    // never requires editing the Vulkan backend.
    const float logical_width = std::max(ui_logical_width_, 1.0f);
    const float logical_height = std::max(ui_logical_height_, 1.0f);
    push.placement = {{
        -1.0f + 2.0f * slot.rect.x / logical_width,
        -1.0f + 2.0f * (slot.rect.y + slot.rect.h * 0.5f) / logical_height,
        2.0f * (slot.rect.w / config.width) / logical_width,
        2.0f * (slot.rect.h / config.height) / logical_height}};
    push.motion = {{static_cast<float>(frames_presented_) * (config.wave_speed / 60.0f),
                    config.wind_strength, config.wave_frequency,
                    static_cast<float>(config.pattern)}};
    push.primary = flag_color(config.colors[0]);
    push.secondary = flag_color(config.colors[1]);
    push.accent = flag_color(config.colors[2]);

    const auto scissor = ui_scissor(slot.scissor, extent_, ui_scale_x_, ui_scale_y_);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const VkDeviceSize flag_offset = 0;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, flag_pipeline_);
    vkCmdBindVertexBuffers(command, 0, 1, &flag_vertices_, &flag_offset);
    vkCmdBindIndexBuffer(command, flag_indices_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(command, flag_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDrawIndexed(command, flag_index_count_, 1, 0, 0, 0);
    ++draw_calls_;
}



} // namespace thunder
