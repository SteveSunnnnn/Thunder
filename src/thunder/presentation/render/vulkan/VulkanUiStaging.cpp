#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include "thunder/presentation/ui/FontAtlas.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include "thunder/presentation/ui/Utf8Decode.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace thunder {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
    }
}

[[nodiscard]] UiGpuVertex ui_gpu_vertex(const UiVertex& vertex) noexcept {
    // Thunder UI colors are stored as AARRGGBB (the high byte is alpha).  Keep
    // the public draw-list representation compact and expand only at the GPU
    // upload boundary, where the shader consumes four normalized floats.
    const auto rgba = vertex.rgba;
    constexpr float inv = 1.0f / 255.0f;
    return UiGpuVertex{
        vertex.x, vertex.y, vertex.u, vertex.v,
        static_cast<float>((rgba >> 16u) & 0xffu) * inv,
        static_cast<float>((rgba >> 8u) & 0xffu) * inv,
        static_cast<float>(rgba & 0xffu) * inv,
        static_cast<float>((rgba >> 24u) & 0xffu) * inv};
}

[[nodiscard]] std::array<std::uint8_t, 7> glyph_rows(char input) noexcept {
    const auto upper = (input >= 'a' && input <= 'z')
        ? static_cast<char>(input - ('a' - 'A')) : input;
    switch (upper) {
    case ' ': return {0,0,0,0,0,0,0};
    case 'A': return {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11};
    case 'B': return {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e};
    case 'C': return {0x0f,0x10,0x10,0x10,0x10,0x10,0x0f};
    case 'D': return {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e};
    case 'E': return {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f};
    case 'F': return {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10};
    case 'G': return {0x0f,0x10,0x10,0x17,0x11,0x11,0x0f};
    case 'H': return {0x11,0x11,0x11,0x1f,0x11,0x11,0x11};
    case 'I': return {0x1f,0x04,0x04,0x04,0x04,0x04,0x1f};
    case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0e};
    case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1f};
    case 'M': return {0x11,0x1b,0x15,0x15,0x11,0x11,0x11};
    case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    case 'O': return {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e};
    case 'P': return {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10};
    case 'Q': return {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d};
    case 'R': return {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11};
    case 'S': return {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e};
    case 'T': return {0x1f,0x04,0x04,0x04,0x04,0x04,0x04};
    case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0e};
    case 'V': return {0x11,0x11,0x11,0x11,0x11,0x0a,0x04};
    case 'W': return {0x11,0x11,0x11,0x15,0x15,0x1b,0x11};
    case 'X': return {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11};
    case 'Y': return {0x11,0x11,0x0a,0x04,0x04,0x04,0x04};
    case 'Z': return {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f};
    case '0': return {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e};
    case '1': return {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e};
    case '2': return {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f};
    case '3': return {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e};
    case '4': return {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02};
    case '5': return {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e};
    case '6': return {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e};
    case '7': return {0x1f,0x01,0x02,0x04,0x08,0x08,0x08};
    case '8': return {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e};
    case '9': return {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e};
    case '-': return {0x00,0x00,0x00,0x1f,0x00,0x00,0x00};
    case '#': return {0x0a,0x1f,0x0a,0x0a,0x1f,0x0a,0x00};
    case ':': return {0x00,0x04,0x04,0x00,0x04,0x04,0x00};
    case '.': return {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c};
    case '/': return {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    case '%': return {0x19,0x19,0x02,0x04,0x08,0x13,0x13};
    case '+': return {0x00,0x04,0x04,0x1f,0x04,0x04,0x00};
    default: return {0x1f,0x11,0x15,0x11,0x15,0x11,0x1f};
    }
}

// decode_utf8 moved to thunder/presentation/ui/Utf8Decode.hpp (shared with FontAtlas).

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

void VulkanDesktopBackend::submit_ui(const UiDrawList& ui) {
    ui_submitted_ = true;
    ui_staging_vertices_.clear();
    ui_staging_indices_.clear();
    ui_staging_batches_.clear();
    ui_staging_modules_.clear();

    int logical_width = 0;
    int logical_height = 0;
    if (window_ != nullptr) SDL_GetWindowSize(window_, &logical_width, &logical_height);
    ui_logical_width_ = static_cast<float>(std::max(logical_width, 1));
    ui_logical_height_ = static_cast<float>(std::max(logical_height, 1));
    ui_scale_x_ = static_cast<float>(std::max(extent_.width, 1u)) / ui_logical_width_;
    ui_scale_y_ = static_cast<float>(std::max(extent_.height, 1u)) / ui_logical_height_;

    const auto source_vertices = ui.vertices();
    const auto source_indices = ui.indices();
    ui_staging_vertices_.reserve(source_vertices.size());
    ui_staging_indices_.reserve(source_indices.size());
    ui_staging_batches_.reserve(ui.batches().size() + ui.text_runs().size());
    ui_staging_modules_.reserve(ui.modules().size());
    for (const auto& vertex : source_vertices) {
        ui_staging_vertices_.push_back(ui_gpu_vertex(vertex));
    }
    for (const auto& module : ui.modules()) {
        if (module.module == 0 || !std::isfinite(module.rect.x) ||
            !std::isfinite(module.rect.y) || !std::isfinite(module.rect.w) ||
            !std::isfinite(module.rect.h) || module.rect.w <= 0.0f || module.rect.h <= 0.0f)
            continue;
        ui_staging_modules_.push_back(module);
    }
    std::stable_sort(ui_staging_modules_.begin(), ui_staging_modules_.end(),
                     [](const UiModuleSlot& left, const UiModuleSlot& right) {
                         return left.order < right.order;
                     });

    // Validate each batch at the content boundary.  A malformed mod/UI script
    // must not be able to feed an out-of-range index to the GPU.
    for (const auto& batch : ui.batches()) {
        if (batch.first_index >= source_indices.size()) continue;
        const auto end = std::min<std::size_t>(source_indices.size(),
                                               static_cast<std::size_t>(batch.first_index) + batch.index_count);
        const auto first = static_cast<std::uint32_t>(ui_staging_indices_.size());
        for (std::size_t index = batch.first_index; index < end; ++index) {
            const auto value = source_indices[index];
            if (value < source_vertices.size()) ui_staging_indices_.push_back(value);
        }
        const auto count = static_cast<std::uint32_t>(ui_staging_indices_.size()) - first;
        if (count != 0u) {
            ui_staging_batches_.push_back(UiGpuBatch{first, count, ui_scissor(batch.scissor, extent_, ui_scale_x_, ui_scale_y_),
                                                     batch.kind, batch.texture, batch.order});
        }
    }

    // Text runs stay out of UiDrawList's geometry arrays so each backend can
    // choose its own atlas. Production clients should install a .thunderfont MSDF
    // package; the fixed-cell and 5x7 branches exist only for old tools.
    for (const auto& run : ui.text_runs()) {
        if (!std::isfinite(run.x) || !std::isfinite(run.y) || !std::isfinite(run.size) || run.size <= 0.0f) continue;
        const auto first = static_cast<std::uint32_t>(ui_staging_indices_.size());
        const auto color = ui_gpu_vertex(UiVertex{0.0f, 0.0f, 0.0f, 0.0f, run.rgba});
        if (ui_font_image_ != VK_NULL_HANDLE && ui_font_metrics_) {
            const auto& atlas = *ui_font_metrics_;
            const auto glyph_for = [&atlas](std::uint32_t codepoint) {
                const FontGlyph* glyph = atlas.find(codepoint);
                if (glyph == nullptr) glyph = atlas.find(0xfffdu);
                if (glyph == nullptr) glyph = atlas.find(static_cast<std::uint32_t>('?'));
                return glyph;
            };
            float line_width = 0.0f;
            auto measure_line = [&](std::size_t start_idx) -> float {
                float w = 0.0f;
                std::size_t idx = start_idx;
                while (idx < run.utf8.size()) {
                    const auto codepoint = decode_utf8(run.utf8, idx);
                    if (codepoint == static_cast<std::uint32_t>('\n')) break;
                    if (const auto* glyph = glyph_for(codepoint))
                        w += glyph->advance * run.size + run.letter_spacing;
                }
                if (w > 0.0f) w -= run.letter_spacing;
                return w;
            };
            if (run.centered) {
                line_width = measure_line(0);
            }
            float pen_x = run.centered ? (run.map_space ? -line_width * 0.5f : run.x - line_width * 0.5f) : run.x;
            // MSDF metrics are baseline-relative. A baseline around +0.30em
            // vertically centres typical serif caps around a map anchor.
            float baseline_y = run.map_space ? run.size * 0.30f : run.y + run.size * 0.82f;
            const float angle_cos = std::cos(run.angle_rad);
            const float angle_sin = std::sin(run.angle_rad);
            const auto place = [&](float px, float py) {
                if (!run.map_space) return std::array<float, 2>{{px, py}};
                return std::array<float, 2>{{
                    run.x + px * angle_cos - py * angle_sin,
                    run.y + px * angle_sin + py * angle_cos}};
            };
            std::size_t index = 0;
            while (index < run.utf8.size()) {
                const auto codepoint = decode_utf8(run.utf8, index);
                if (codepoint == static_cast<std::uint32_t>('\n')) {
                    line_width = run.centered ? measure_line(index) : 0.0f;
                    pen_x = run.centered ? (run.map_space ? -line_width * 0.5f : run.x - line_width * 0.5f) : run.x;
                    baseline_y += run.size * 1.25f;
                    continue;
                }
                const FontGlyph* glyph = glyph_for(codepoint);
                if (glyph == nullptr) continue;

                const float x0 = pen_x + glyph->plane_left * run.size;
                const float y0 = baseline_y - glyph->plane_top * run.size;
                const float x1 = pen_x + glyph->plane_right * run.size;
                const float y1 = baseline_y - glyph->plane_bottom * run.size;
                if (x1 > x0 && y1 > y0) {
                    const float u0 = glyph->atlas_left / static_cast<float>(atlas.width());
                    const float u1 = glyph->atlas_right / static_cast<float>(atlas.width());
                    const float v0 = 1.0f - glyph->atlas_top / static_cast<float>(atlas.height());
                    const float v1 = 1.0f - glyph->atlas_bottom / static_cast<float>(atlas.height());
                    const auto base = static_cast<std::uint32_t>(ui_staging_vertices_.size());
                    const auto p0 = place(x0, y0);
                    const auto p1 = place(x1, y0);
                    const auto p2 = place(x1, y1);
                    const auto p3 = place(x0, y1);
                    ui_staging_vertices_.push_back(UiGpuVertex{p0[0], p0[1], u0, v0, color.r, color.g, color.b, color.a});
                    ui_staging_vertices_.push_back(UiGpuVertex{p1[0], p1[1], u1, v0, color.r, color.g, color.b, color.a});
                    ui_staging_vertices_.push_back(UiGpuVertex{p2[0], p2[1], u1, v1, color.r, color.g, color.b, color.a});
                    ui_staging_vertices_.push_back(UiGpuVertex{p3[0], p3[1], u0, v1, color.r, color.g, color.b, color.a});
                    ui_staging_indices_.insert(ui_staging_indices_.end(), {base + 0u, base + 1u, base + 2u,
                                                                             base + 0u, base + 2u, base + 3u});
                }
                pen_x += glyph->advance * run.size + run.letter_spacing;
            }
            const auto count = static_cast<std::uint32_t>(ui_staging_indices_.size()) - first;
            if (count != 0u) ui_staging_batches_.push_back(UiGpuBatch{
                first, count, ui_scissor(run.scissor, extent_, ui_scale_x_, ui_scale_y_),
                run.map_space ? UiBatchKind::MapMsdfText : UiBatchKind::MsdfText,
                atlas.texture_hash(), run.order});
        } else if (!run.map_space && std::getenv("THUNDER_ALLOW_LEGACY_BITMAP_FONT") != nullptr &&
                   ui_font_image_ != VK_NULL_HANDLE && ui_font_cell_ != 0u && ui_font_columns_ != 0u &&
            !ui_font_slots_.empty()) {
            auto measure_cell_line = [&](std::size_t start_idx) -> float {
                float w = 0.0f;
                std::size_t idx = start_idx;
                while (idx < run.utf8.size()) {
                    const auto cp = decode_utf8(run.utf8, idx);
                    if (cp == static_cast<std::uint32_t>('\n')) break;
                    const bool wide = cp >= 0x2e80u;
                    w += run.size * (wide ? 1.02f : 0.66f);
                }
                return w;
            };
            float line_w = run.centered ? measure_cell_line(0) : 0.0f;
            float x = run.centered ? run.x - line_w * 0.5f : run.x;
            float y = run.y;
            std::size_t index = 0;
            while (index < run.utf8.size()) {
                const auto codepoint = decode_utf8(run.utf8, index);
                if (codepoint == static_cast<std::uint32_t>('\n')) {
                    line_w = run.centered ? measure_cell_line(index) : 0.0f;
                    x = run.centered ? run.x - line_w * 0.5f : run.x;
                    y += run.size * 1.35f;
                    continue;
                }
                auto slot = ui_font_slots_.find(codepoint);
                if (slot == ui_font_slots_.end()) slot = ui_font_slots_.find(static_cast<std::uint32_t>('?'));
                const bool wide = codepoint >= 0x2e80u;
                const float advance = run.size * (wide ? 1.02f : 0.66f);
                if (slot == ui_font_slots_.end()) {
                    x += advance;
                    continue;
                }
                const auto slot_index = slot->second;
                const auto column = slot_index % ui_font_columns_;
                const auto row_index = slot_index / ui_font_columns_;
                const float u0 = static_cast<float>(column * ui_font_cell_) /
                                 static_cast<float>(std::max(ui_font_width_, 1u));
                const float v0 = static_cast<float>(row_index * ui_font_cell_) /
                                 static_cast<float>(std::max(ui_font_height_, 1u));
                const float u1 = static_cast<float>((column + 1u) * ui_font_cell_) /
                                 static_cast<float>(std::max(ui_font_width_, 1u));
                const float v1 = static_cast<float>((row_index + 1u) * ui_font_cell_) /
                                 static_cast<float>(std::max(ui_font_height_, 1u));
                const auto base = static_cast<std::uint32_t>(ui_staging_vertices_.size());
                ui_staging_vertices_.push_back(UiGpuVertex{x, y, u0, v0, color.r, color.g, color.b, color.a});
                ui_staging_vertices_.push_back(UiGpuVertex{x + advance, y, u1, v0, color.r, color.g, color.b, color.a});
                ui_staging_vertices_.push_back(UiGpuVertex{x + advance, y + run.size, u1, v1, color.r, color.g, color.b, color.a});
                ui_staging_vertices_.push_back(UiGpuVertex{x, y + run.size, u0, v1, color.r, color.g, color.b, color.a});
                ui_staging_indices_.insert(ui_staging_indices_.end(), {base + 0u, base + 1u, base + 2u,
                                                                         base + 0u, base + 2u, base + 3u});
                x += advance;
            }
            const auto count = static_cast<std::uint32_t>(ui_staging_indices_.size()) - first;
            if (count != 0u) ui_staging_batches_.push_back(UiGpuBatch{first, count, ui_scissor(run.scissor, extent_, ui_scale_x_, ui_scale_y_),
                                                                       UiBatchKind::Textured, kUiFontTextureKey, run.order});
        } else if (!run.map_space && std::getenv("THUNDER_ALLOW_LEGACY_BITMAP_FONT") != nullptr) {
            const float cell = std::max(1.0f, run.size / 7.0f);
            auto measure_5x7_line = [&](std::size_t start_idx) -> float {
                float w = 0.0f;
                for (std::size_t i = start_idx; i < run.utf8.size(); ++i) {
                    if (run.utf8[i] == '\n') break;
                    w += cell * 6.0f;
                }
                return w;
            };
            float line_w = run.centered ? measure_5x7_line(0) : 0.0f;
            float x = run.centered ? run.x - line_w * 0.5f : run.x;
            float y = run.y;
            for (std::size_t i = 0; i < run.utf8.size(); ++i) {
                const unsigned char byte = static_cast<unsigned char>(run.utf8[i]);
                if (byte == '\n') {
                    line_w = run.centered ? measure_5x7_line(i + 1) : 0.0f;
                    x = run.centered ? run.x - line_w * 0.5f : run.x;
                    y += cell * 9.0f;
                    continue;
                }
                if (byte >= 0x80u) {
                    x += cell * 6.0f;
                    continue;
                }
                const auto rows = glyph_rows(static_cast<char>(byte));
                for (std::uint32_t row = 0; row < rows.size(); ++row) {
                    for (std::uint32_t column = 0; column < 5u; ++column) {
                        if ((rows[row] & static_cast<std::uint8_t>(1u << (4u - column))) == 0u) continue;
                        const auto base = static_cast<std::uint32_t>(ui_staging_vertices_.size());
                        const float px = x + static_cast<float>(column) * cell;
                        const float py = y + static_cast<float>(row) * cell;
                        ui_staging_vertices_.push_back(UiGpuVertex{px, py, 0.0f, 0.0f, color.r, color.g, color.b, color.a});
                        ui_staging_vertices_.push_back(UiGpuVertex{px + cell, py, 1.0f, 0.0f, color.r, color.g, color.b, color.a});
                        ui_staging_vertices_.push_back(UiGpuVertex{px + cell, py + cell, 1.0f, 1.0f, color.r, color.g, color.b, color.a});
                        ui_staging_vertices_.push_back(UiGpuVertex{px, py + cell, 0.0f, 1.0f, color.r, color.g, color.b, color.a});
                        ui_staging_indices_.insert(ui_staging_indices_.end(), {base + 0u, base + 1u, base + 2u,
                                                                                 base + 0u, base + 2u, base + 3u});
                    }
                }
                x += cell * 6.0f;
            }
            const auto count = static_cast<std::uint32_t>(ui_staging_indices_.size()) - first;
            if (count != 0u) ui_staging_batches_.push_back(UiGpuBatch{first, count, ui_scissor(run.scissor, extent_, ui_scale_x_, ui_scale_y_),
                                                                       UiBatchKind::Solid, 0, run.order});
        } else {
            // Shipping desktop builds never silently substitute a pixel font.
            // Missing MSDF content leaves the run out and keeps the visual
            // contract explicit; development can opt into the old diagnostic
            // path with THUNDER_ALLOW_LEGACY_BITMAP_FONT=1.
        }
    }

    // Preserve the draw-list's original geometry/text interleave. Tooltips,
    // modals and other overlays are authored last and must remain above both
    // the geometry and the text of every lower HUD layer.
    std::stable_sort(ui_staging_batches_.begin(), ui_staging_batches_.end(),
                     [](const UiGpuBatch& left, const UiGpuBatch& right) {
                         return left.order < right.order;
                     });

    if (!ui_staging_vertices_.empty() && !ui_staging_indices_.empty() && device_ != VK_NULL_HANDLE) {
        ensure_ui_frame_buffers();
    }
}

void VulkanDesktopBackend::submit_map_overlay(std::span<const UiVertex> vertices,
                                              std::span<const std::uint32_t> indices) {
    // Keep camera-driven overlay changes on CPU staging. The current frame
    // slot is uploaded after its fence is waited in draw_frame(), so camera
    // movement never stalls on vkDeviceWaitIdle().

    // Convert the compact AARRGGBB draw-list vertices to the GPU layout once.
    // The staging vector is reused across camera moves so the only allocation
    // is the resident GPU buffers themselves.
    map_overlay_staging_vertices_.clear();
    map_overlay_staging_indices_.assign(indices.begin(), indices.end());
    if (!vertices.empty() && !indices.empty())
        map_overlay_staging_vertices_.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        map_overlay_staging_vertices_.push_back(ui_gpu_vertex(vertex));
    }

    ++map_overlay_generation_;
}

void VulkanDesktopBackend::upload_map_overlay_frame() {
    if (!runtime_renderer_enabled_ || device_ == VK_NULL_HANDLE) return;

    auto& frame = map_overlay_frames_[frame_index_];
    if (frame.generation == map_overlay_generation_) return;
    if (map_overlay_staging_vertices_.empty() || map_overlay_staging_indices_.empty()) {
        frame.index_count = 0u;
        frame.generation = map_overlay_generation_;
        return;
    }

    const auto vertex_bytes = static_cast<VkDeviceSize>(
        map_overlay_staging_vertices_.size() * sizeof(UiGpuVertex));
    const auto index_bytes = static_cast<VkDeviceSize>(
        map_overlay_staging_indices_.size() * sizeof(std::uint32_t));
    const auto next_capacity = [](VkDeviceSize current, VkDeviceSize required) {
        VkDeviceSize capacity = current == 0u ? 4096u : current;
        while (capacity < required) capacity *= 2u;
        return capacity;
    };

    if (frame.vertex_capacity < vertex_bytes) {
        destroy_mapped_host_buffer(frame.vertex_buffer, frame.vertex_memory, frame.vertex_mapped);
        frame.vertex_capacity = next_capacity(frame.vertex_capacity, vertex_bytes);
        create_mapped_host_buffer(frame.vertex_capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  frame.vertex_buffer, frame.vertex_memory, frame.vertex_mapped);
    }
    if (frame.index_capacity < index_bytes) {
        destroy_mapped_host_buffer(frame.index_buffer, frame.index_memory, frame.index_mapped);
        frame.index_capacity = next_capacity(frame.index_capacity, index_bytes);
        create_mapped_host_buffer(frame.index_capacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  frame.index_buffer, frame.index_memory, frame.index_mapped);
    }
    std::memcpy(frame.vertex_mapped, map_overlay_staging_vertices_.data(),
                static_cast<std::size_t>(vertex_bytes));
    std::memcpy(frame.index_mapped, map_overlay_staging_indices_.data(),
                static_cast<std::size_t>(index_bytes));
    frame.index_count = static_cast<std::uint32_t>(map_overlay_staging_indices_.size());
    frame.generation = map_overlay_generation_;
}

void VulkanDesktopBackend::ensure_ui_frame_buffers() {
    if (device_ == VK_NULL_HANDLE) return;
    const auto vertex_bytes = std::max<std::size_t>(sizeof(UiGpuVertex), ui_staging_vertices_.size() * sizeof(UiGpuVertex));
    const auto index_bytes = std::max<std::size_t>(sizeof(std::uint32_t), ui_staging_indices_.size() * sizeof(std::uint32_t));

    auto next_capacity = [](VkDeviceSize current, std::size_t required) {
        VkDeviceSize capacity = current == 0 ? 4096u : current;
        while (capacity < required) capacity = std::min<VkDeviceSize>(capacity * 2u, static_cast<VkDeviceSize>(std::numeric_limits<std::uint32_t>::max()));
        return capacity;
    };
    auto grow = [&](VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
                    VkDeviceSize& capacity, VkDeviceSize required, VkBufferUsageFlags usage) {
        if (capacity >= required && buffer != VK_NULL_HANDLE && mapped != nullptr) return;
        if (mapped != nullptr && memory != VK_NULL_HANDLE) vkUnmapMemory(device_, memory);
        mapped = nullptr;
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
        capacity = next_capacity(capacity, static_cast<std::size_t>(required));

        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = capacity;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkcheck(vkCreateBuffer(device_, &info, nullptr, &buffer), "vkCreateBuffer(ui)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vkAllocateMemory(device_, &allocation, nullptr, &memory), "vkAllocateMemory(ui)");
        vkcheck(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory(ui)");
        vkcheck(vkMapMemory(device_, memory, 0, requirements.size, 0, &mapped), "vkMapMemory(ui)");
    };

    bool resize = false;
    for (const auto& frame : ui_frame_buffers_) {
        resize = resize || frame.vertex_capacity < vertex_bytes || frame.index_capacity < index_bytes;
    }
    if (resize) vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(ui resize)");
    for (auto& frame : ui_frame_buffers_) {
        grow(frame.vertex_buffer, frame.vertex_memory, frame.vertex_mapped, frame.vertex_capacity,
             static_cast<VkDeviceSize>(vertex_bytes), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        grow(frame.index_buffer, frame.index_memory, frame.index_mapped, frame.index_capacity,
             static_cast<VkDeviceSize>(index_bytes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    // submit_ui() runs before draw_frame().  Protect the persistently mapped
    // buffer for the current frame from the previous GPU submission; waiting
    // only in draw_frame() would leave a write-after-submit race when the
    // buffers have already reached their steady-state capacity.
    if (frames_[frame_index_].fence != VK_NULL_HANDLE)
        vkcheck(vkWaitForFences(device_, 1, &frames_[frame_index_].fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences(ui upload)");
    auto& frame = ui_frame_buffers_[frame_index_];
    std::memcpy(frame.vertex_mapped, ui_staging_vertices_.data(), ui_staging_vertices_.size() * sizeof(UiGpuVertex));
    std::memcpy(frame.index_mapped, ui_staging_indices_.data(), ui_staging_indices_.size() * sizeof(std::uint32_t));
}



} // namespace thunder
