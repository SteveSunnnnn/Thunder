#include "thunder/presentation/ui/StrategyUi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace thunder {

void UiDrawList::clear() noexcept {
    vertices_.clear();
    indices_.clear();
    batches_.clear();
    text_.clear();
    modules_.clear();
    hits_.clear();
    next_order_ = 0;
    last_was_geometry_ = false;
}

void UiDrawList::module(std::uint64_t module_key, UiRect rect, UiRect scissor) {
    if (module_key == 0 || !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.w) || !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f)
        return;
    modules_.push_back(UiModuleSlot{module_key, rect, scissor, next_order_++});
    last_was_geometry_ = false;
}

void UiDrawList::append_quad_batch(UiBatchKind kind, std::uint32_t first, std::uint32_t count,
                                   std::uint64_t texture, UiRect scissor) {
    if (last_was_geometry_ && !batches_.empty()) {
        auto& b = batches_.back();
        if (b.kind == kind && b.texture == texture &&
            b.scissor.x == scissor.x && b.scissor.y == scissor.y &&
            b.scissor.w == scissor.w && b.scissor.h == scissor.h) {
            b.index_count += count;
            return;
        }
    }
    batches_.push_back(UiBatch{kind, first, count, texture, scissor, next_order_++});
    last_was_geometry_ = true;
}

void UiDrawList::quad(UiRect rect, std::uint32_t rgba, UiRect scissor,
                      std::uint64_t texture, UiBatchKind kind) {
    quad_uv(rect, 0.0f, 0.0f, 1.0f, 1.0f, rgba, scissor, texture, kind);
}

void UiDrawList::quad_uv(UiRect rect, float u0, float v0, float u1, float v1,
                         std::uint32_t rgba, UiRect scissor, std::uint64_t texture,
                         UiBatchKind kind) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || !std::isfinite(u0) || !std::isfinite(v0) ||
        !std::isfinite(u1) || !std::isfinite(v1) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(rect.x + rect.w) || !std::isfinite(rect.y + rect.h)) return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    vertices_.push_back(UiVertex{rect.x,          rect.y,          u0, v0, rgba});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y,          u1, v0, rgba});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y + rect.h, u1, v1, rgba});
    vertices_.push_back(UiVertex{rect.x,          rect.y + rect.h, u0, v1, rgba});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    append_quad_batch(kind, first_idx, 6, texture, scissor);
}

void UiDrawList::quad_points(float x0, float y0,
                             float x1, float y1,
                             float x2, float y2,
                             float x3, float y3,
                             std::uint32_t rgba,
                             UiRect scissor,
                             std::uint64_t texture,
                             UiBatchKind kind) {
    if (!std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) ||
        !std::isfinite(x2) || !std::isfinite(y2) ||
        !std::isfinite(x3) || !std::isfinite(y3)) return;

    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    vertices_.push_back(UiVertex{x0, y0, 0.0f, 0.0f, rgba});
    vertices_.push_back(UiVertex{x1, y1, 1.0f, 0.0f, rgba});
    vertices_.push_back(UiVertex{x2, y2, 1.0f, 1.0f, rgba});
    vertices_.push_back(UiVertex{x3, y3, 0.0f, 1.0f, rgba});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    append_quad_batch(kind, first_idx, 6, texture, scissor);
}

void UiDrawList::quad_points_colors(float x0, float y0, std::uint32_t c0,
                                    float x1, float y1, std::uint32_t c1,
                                    float x2, float y2, std::uint32_t c2,
                                    float x3, float y3, std::uint32_t c3,
                                    UiRect scissor,
                                    std::uint64_t texture,
                                    UiBatchKind kind) {
    if (!std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) ||
        !std::isfinite(x2) || !std::isfinite(y2) ||
        !std::isfinite(x3) || !std::isfinite(y3)) return;

    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    vertices_.push_back(UiVertex{x0, y0, 0.0f, 0.0f, c0});
    vertices_.push_back(UiVertex{x1, y1, 1.0f, 0.0f, c1});
    vertices_.push_back(UiVertex{x2, y2, 1.0f, 1.0f, c2});
    vertices_.push_back(UiVertex{x3, y3, 0.0f, 1.0f, c3});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    append_quad_batch(kind, first_idx, 6, texture, scissor);
}

void UiDrawList::polyline(std::span<const float> xy, std::uint32_t rgba, UiRect scissor) {
    if (xy.size() < 4) return;
    constexpr float half_w = 1.0f;
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    for (std::size_t i = 0; i + 3 < xy.size(); i += 2) {
        const float x0 = xy[i],     y0 = xy[i + 1];
        const float x1 = xy[i + 2], y1 = xy[i + 3];
        if (!std::isfinite(x0) || !std::isfinite(y0) ||
            !std::isfinite(x1) || !std::isfinite(y1)) continue;
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(len) || len < 1e-4f) continue;
        const float nx = -dy / len * half_w;
        const float ny =  dx / len * half_w;

        const auto base = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(UiVertex{x0 + nx, y0 + ny, 0, 0, rgba});
        vertices_.push_back(UiVertex{x1 + nx, y1 + ny, 1, 0, rgba});
        vertices_.push_back(UiVertex{x1 - nx, y1 - ny, 1, 1, rgba});
        vertices_.push_back(UiVertex{x0 - nx, y0 - ny, 0, 1, rgba});

        indices_.push_back(base + 0);
        indices_.push_back(base + 1);
        indices_.push_back(base + 2);
        indices_.push_back(base + 0);
        indices_.push_back(base + 2);
        indices_.push_back(base + 3);
    }
    const auto count = static_cast<std::uint32_t>(indices_.size()) - first_idx;
    if (count > 0) {
        append_quad_batch(UiBatchKind::Polyline, first_idx, count, 0, scissor);
    }
}

void UiDrawList::append_geometry(std::span<const UiVertex> vertices,
                                 std::span<const std::uint32_t> indices,
                                 UiRect scissor,
                                 UiBatchKind kind) {
    if (vertices.empty() || indices.empty()) return;
    const auto base_idx = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
    indices_.reserve(indices_.size() + indices.size());
    for (auto idx : indices) {
        indices_.push_back(base_idx + idx);
    }
    append_quad_batch(kind, first_idx, static_cast<std::uint32_t>(indices.size()), 0, scissor);
}

void UiDrawList::radial_disc(float cx, float cy, float radius,
                             std::uint32_t center_rgba, std::uint32_t edge_rgba,
                             UiRect scissor, std::uint32_t segments) {
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius) || radius <= 0.0f)
        return;
    segments = std::clamp<std::uint32_t>(segments, 8u, 96u);
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());
    vertices_.push_back(UiVertex{cx, cy, 0.5f, 0.5f, center_rgba});
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float angle = static_cast<float>(i) * 6.283185307f / static_cast<float>(segments);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        vertices_.push_back(UiVertex{cx + cosine * radius, cy + sine * radius,
            0.5f + cosine * 0.5f, 0.5f + sine * 0.5f, edge_rgba});
    }
    for (std::uint32_t i = 0; i < segments; ++i) {
        indices_.push_back(base);
        indices_.push_back(base + i + 1u);
        indices_.push_back(base + i + 2u);
    }
    append_quad_batch(UiBatchKind::Solid, first_idx, segments * 3u, 0, scissor);
}

void UiDrawList::nine_slice(UiRect rect, const UiNineSlice& slice, std::uint32_t rgba,
                            std::uint64_t texture, UiRect scissor) {
    const float xs[4] = {rect.x, rect.x + slice.border.left,
                         rect.x + rect.w - slice.border.right, rect.x + rect.w};
    const float ys[4] = {rect.y, rect.y + slice.border.top,
                         rect.y + rect.h - slice.border.bottom, rect.y + rect.h};
    const float us[4] = {slice.outer_uv.x, slice.inner_uv.x,
                         slice.inner_uv.x + slice.inner_uv.w, slice.outer_uv.x + slice.outer_uv.w};
    const float vs[4] = {slice.outer_uv.y, slice.inner_uv.y,
                         slice.inner_uv.y + slice.inner_uv.h, slice.outer_uv.y + slice.outer_uv.h};

    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            const UiRect cell{xs[x], ys[y], xs[x + 1] - xs[x], ys[y + 1] - ys[y]};
            if (cell.w <= 0.0f || cell.h <= 0.0f) continue;
            quad_uv(cell, us[x], vs[y], us[x + 1], vs[y + 1], rgba,
                    scissor, texture, UiBatchKind::Textured);
        }
    }
}

void UiDrawList::quad_gradient(UiRect rect, std::uint32_t start_rgba, std::uint32_t end_rgba,
                               bool vertical, UiRect scissor, std::uint64_t texture, UiBatchKind kind) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(rect.x + rect.w) || !std::isfinite(rect.y + rect.h)) return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    // Rasterizers interpolate per-vertex color, so one quad gives a smooth
    // ramp without banding. Start color sits on the top (vertical) or left
    // (horizontal) edge.
    const std::uint32_t tl = start_rgba;
    const std::uint32_t br = end_rgba;
    const std::uint32_t tr = vertical ? end_rgba : start_rgba;
    const std::uint32_t bl = vertical ? end_rgba : start_rgba;

    vertices_.push_back(UiVertex{rect.x,          rect.y,          0, 0, tl});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y,          1, 0, tr});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y + rect.h, 1, 1, br});
    vertices_.push_back(UiVertex{rect.x,          rect.y + rect.h, 0, 1, bl});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    append_quad_batch(kind, first_idx, 6, texture, scissor);
}

void UiDrawList::drop_shadow(UiRect rect, std::uint32_t rgba, float blur_radius,
                             float offset_x, float offset_y, UiRect scissor) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(blur_radius) || !std::isfinite(offset_x) || !std::isfinite(offset_y)) return;
    const std::uint32_t base_alpha = rgba >> 24u;
    if (base_alpha == 0u || blur_radius <= 0.0f) {
        quad({rect.x + offset_x, rect.y + offset_y, rect.w, rect.h}, rgba, scissor);
        return;
    }
    // Stacked layers from outermost to innermost: a pixel at distance d from
    // the surface edge is covered by every layer whose growth reaches d, so
    // accumulated alpha falls off roughly linearly across the penumbra.
    const int layers = std::clamp(static_cast<int>(std::lround(blur_radius)), 3, 7);
    const std::uint32_t rgb = rgba & 0x00ffffffu;
    for (int i = layers; i >= 1; --i) {
        const float grow = blur_radius * static_cast<float>(i) / static_cast<float>(layers);
        const auto layer_alpha = static_cast<std::uint32_t>(
            std::lround(static_cast<float>(base_alpha) / static_cast<float>(layers)));
        quad({rect.x + offset_x - grow, rect.y + offset_y - grow,
              rect.w + grow * 2.0f, rect.h + grow * 2.0f},
             rgb | (layer_alpha << 24u), scissor);
    }
}

void UiDrawList::panel(UiRect rect, std::uint32_t background_rgba, std::uint32_t border_rgba,
                       std::uint32_t shadow_rgba, float shadow_offset, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const float offset = std::max(0.0f, shadow_offset);
    if ((shadow_rgba >> 24u) != 0u)
        quad({rect.x + offset, rect.y + offset, rect.w, rect.h}, shadow_rgba, scissor);
    quad(rect, border_rgba, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, background_rgba, scissor);
}

// ── Ornamental Wood Material ──
} // namespace thunder
