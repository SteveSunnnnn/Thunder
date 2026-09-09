#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "thunder/presentation/ui/UiTheme.hpp"

namespace thunder {

struct UiRect { float x=0, y=0, w=0, h=0; };
struct UiControlVisualState {
    bool enabled = true;
    bool selected = false;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    float hover_mix = 0.0f;
    float press_mix = 0.0f;
    float selected_mix = 0.0f;
    float focus_mix = 0.0f;
};
struct UiInsets { float left=0, top=0, right=0, bottom=0; };
struct UiNineSlice { UiRect outer_uv{0,0,1,1}; UiRect inner_uv{0,0,1,1}; UiInsets border{}; };
struct UiVertex { float x=0, y=0, u=0, v=0; std::uint32_t rgba=0xffffffffu; };
enum class UiBatchKind : std::uint8_t { Solid, Textured, MsdfText, MapMsdfText, Polyline };
// Stable client-owned texture key reserved for a full-world political map.
// Render clients may bind a different image to this key without making the
// simulation or UI draw-list data depend on a particular game/content pack.
inline constexpr std::uint64_t kUiWorldMapTextureKey = 0x5448554e4452574dull; // "THUNDRWM"
inline constexpr std::uint64_t kUiFontTextureKey = 0x5448554e4452464eull; // "THUNDRFN"
struct UiBatch { UiBatchKind kind=UiBatchKind::Solid; std::uint32_t first_index=0, index_count=0; std::uint64_t texture=0; UiRect scissor{}; std::uint64_t order=0; };
struct UiHitRegion { std::uint64_t id=0; UiRect rect{}; };
struct UiTextRun {
    std::string utf8;
    float x = 0;
    float y = 0;
    float size = 16;
    std::uint32_t rgba = 0xffffffffu;
    UiRect scissor{};
    float angle_rad = 0.0f;
    float letter_spacing = 0.0f;
    bool centered = false;
    bool map_space = false;
    std::uint64_t order = 0;
};
// Backend-rendered module slot authored by the declarative UI. The draw list
// owns placement, clipping and ordering; render backends only resolve the
// stable module key to an installed module implementation.
struct UiModuleSlot { std::uint64_t module=0; UiRect rect{}; UiRect scissor{}; std::uint64_t order=0; };

class UiDrawList {
public:
    void clear() noexcept;
    void quad(UiRect rect,
              std::uint32_t rgba,
              UiRect scissor = {},
              std::uint64_t texture = 0,
              UiBatchKind kind = UiBatchKind::Solid);
    void quad_uv(UiRect rect,
                 float u0,
                 float v0,
                 float u1,
                 float v1,
                 std::uint32_t rgba,
                 UiRect scissor = {},
                 std::uint64_t texture = 0,
                 UiBatchKind kind = UiBatchKind::Textured);
    void quad_points(float x0, float y0,
                     float x1, float y1,
                     float x2, float y2,
                     float x3, float y3,
                     std::uint32_t rgba,
                     UiRect scissor = {},
                     std::uint64_t texture = 0,
                     UiBatchKind kind = UiBatchKind::Solid);
    void quad_points_colors(float x0, float y0, std::uint32_t c0,
                            float x1, float y1, std::uint32_t c1,
                            float x2, float y2, std::uint32_t c2,
                            float x3, float y3, std::uint32_t c3,
                            UiRect scissor = {},
                            std::uint64_t texture = 0,
                            UiBatchKind kind = UiBatchKind::Solid);
    void polyline(std::span<const float> xy, std::uint32_t rgba, UiRect scissor = {});
    void radial_disc(float cx, float cy, float radius,
                     std::uint32_t center_rgba, std::uint32_t edge_rgba,
                     UiRect scissor = {}, std::uint32_t segments = 32);
    void append_geometry(std::span<const UiVertex> vertices,
                         std::span<const std::uint32_t> indices,
                         UiRect scissor = {},
                         UiBatchKind kind = UiBatchKind::Solid);
    void nine_slice(UiRect rect, const UiNineSlice& slice, std::uint32_t rgba,
                    std::uint64_t texture, UiRect scissor = {});

    // Smooth shading primitives. quad_gradient colors the two edges with
    // different rgba values and lets the rasterizer interpolate between
    // them (one quad, no banding). drop_shadow emits stacked translucent
    // layers to approximate a soft penumbra around a raised surface.
    void quad_gradient(UiRect rect, std::uint32_t start_rgba, std::uint32_t end_rgba,
                       bool vertical = true, UiRect scissor = {},
                       std::uint64_t texture = 0, UiBatchKind kind = UiBatchKind::Solid);
    void drop_shadow(UiRect rect, std::uint32_t rgba, float blur_radius,
                     float offset_x = 0.0f, float offset_y = 2.0f, UiRect scissor = {});

    // Theme control. Every material and component below reads its palette,
    // metrics and typography from this theme. A null pointer resolves to the
    // engine default (UiTheme::victorian), so freshly created draw lists and
    // dynamically generated UI always render in the house style.
    void set_theme(const UiTheme* theme) noexcept { theme_ = theme; }
    [[nodiscard]] const UiTheme& theme() const noexcept { return theme_ ? *theme_ : UiTheme::victorian(); }

    // Ornamental strategy-game materials and panels
    void panel(UiRect rect, std::uint32_t background_rgba, std::uint32_t border_rgba,
               std::uint32_t shadow_rgba = 0x40000000u, float shadow_offset = 3.0f,
               UiRect scissor = {});
    void wood_panel(UiRect rect, UiRect scissor = {});
    void parchment_panel(UiRect rect, UiRect scissor = {});
    void leather_panel(UiRect rect, std::uint32_t leather_color = 0, UiRect scissor = {});
    void brass_button(UiRect rect, const std::string& label, bool pressed = false, UiRect scissor = {});
    void medallion_button(UiRect rect, bool selected = false, bool hovered = false,
                          bool enabled = true, UiRect scissor = {});
    void medallion_button(UiRect rect, const UiControlVisualState& state,
                          bool primary = false, UiRect scissor = {});
    void mechanical_button(UiRect rect, const UiControlVisualState& state,
                           UiRect scissor = {});
    void category_row(UiRect rect, const UiControlVisualState& state,
                      UiRect scissor = {});
    void wax_seal(float cx, float cy, float radius = 18.0f, UiRect scissor = {});
    void progress_bar(UiRect rect, float fraction, std::uint32_t fill_color = 0,
                      std::uint32_t bg_color = 0, UiRect scissor = {});
    void parliament_arc(float cx, float cy, float inner_radius, float outer_radius,
                        std::span<const std::pair<std::uint32_t, int>> seat_groups,
                        UiRect scissor = {});
    void gauge_balance(UiRect rect, float buy_orders, float sell_orders, UiRect scissor = {});
    void ink_chart(UiRect rect, std::span<const float> values,
                   std::uint32_t ink_rgba = 0,
                   std::uint32_t fill_rgba = 0,
                   UiRect scissor = {});
    void construction_queue_row(UiRect rect, const std::string& name, const std::string& kind_label,
                                float progress_ratio, const std::string& eta_text,
                                bool paused = false, UiRect scissor = {});
    void tariff_slider_input_row(UiRect rect, const std::string& label, float tariff_fraction,
                                 const std::string& input_text, bool is_import,
                                 UiRect scissor = {});
    void financial_kpi_card(UiRect rect, const std::string& title, const std::string& value_text,
                            double delta_value = 0.0, float meter_fraction = -1.0f,
                            std::uint32_t meter_color = 0, UiRect scissor = {});
    void gold_reserve_meter(UiRect rect, double current_reserves, double reserve_cap,
                            double max_debt = 0.0, UiRect scissor = {});
    void treasury_balance_ticker(UiRect rect, double weekly_income, double weekly_expense,
                                 UiRect scissor = {}, std::string_view currency_symbol = "£");

    // Generic themed components. All of them take content only; the visual
    // language (materials, borders, typography, states) comes from the theme.
    void v_gradient(UiRect rect, std::uint32_t top_rgba, std::uint32_t bottom_rgba,
                    int bands = 6, UiRect scissor = {});
    void corner_ornaments(UiRect rect, std::uint32_t rgba = 0, float size = 5.0f, UiRect scissor = {});
    void divider_ornament(UiRect rect, UiRect scissor = {});
    void separator(UiRect rect, UiRect scissor = {});
    void ornate_header(UiRect rect, const std::string& title, UiRect scissor = {});
    void window_frame(UiRect rect, const std::string& title, UiRect scissor = {});
    void tab(UiRect rect, const std::string& label, bool active = false,
             bool hovered = false, UiRect scissor = {});
    void dropdown_row(UiRect rect, const std::string& label, bool hovered = false,
                      bool selected = false, bool disabled = false, UiRect scissor = {});
    void checkbox(UiRect rect, bool checked = false, bool hovered = false,
                  bool disabled = false, UiRect scissor = {});
    void radio(UiRect rect, bool selected = false, bool hovered = false,
               bool disabled = false, UiRect scissor = {});
    void slider(UiRect rect, float fraction, bool hovered = false, UiRect scissor = {});
    void scrollbar(UiRect rect, float thumb_start_fraction, float thumb_size_fraction,
                   bool hovered = false, UiRect scissor = {});
    void input_box(UiRect rect, const std::string& text, bool focused = false,
                   UiRect scissor = {});
    void list_row(UiRect rect, const std::string& primary, const std::string& secondary,
                  const std::string& value = {}, bool hovered = false, bool selected = false,
                  bool striped = false, UiRect scissor = {});
    void table_header_cell(UiRect rect, const std::string& label, bool right_aligned = false,
                           UiRect scissor = {});
    void stat_row(UiRect rect, const std::string& label, const std::string& value,
                  const std::string& delta = {}, UiRect scissor = {});
    void notification_card(UiRect rect, const std::string& title, const std::string& body,
                           int severity = 3, UiRect scissor = {});
    void modal_window(UiRect rect, const std::string& title, const std::string& body,
                      UiRect scissor = {});

    void text(std::string utf8, float x, float y, float size, std::uint32_t rgba, UiRect scissor = {}, bool centered = false);
    // Geography-bound MSDF text. The backend composites these runs into the
    // HDR map pass (before HUD/tonemap), supports rotation and tracking, and
    // interprets x/y as the run centre.
    void map_text(std::string utf8, float center_x, float center_y, float size,
                  std::uint32_t rgba, float angle_rad = 0.0f,
                  float letter_spacing = 0.0f, UiRect scissor = {});
    void module(std::uint64_t module_key, UiRect rect, UiRect scissor = {});
    void hit(std::uint64_t id, UiRect rect);
    [[nodiscard]] std::optional<std::uint64_t> hit_test(float x, float y) const noexcept;
    [[nodiscard]] std::span<const UiVertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept { return indices_; }
    [[nodiscard]] std::span<const UiBatch> batches() const noexcept { return batches_; }
    [[nodiscard]] std::span<const UiTextRun> text_runs() const noexcept { return text_; }
    [[nodiscard]] std::span<const UiModuleSlot> modules() const noexcept { return modules_; }
    [[nodiscard]] std::span<const UiHitRegion> hits() const noexcept { return hits_; }
private:
    void append_quad_batch(UiBatchKind kind, std::uint32_t first, std::uint32_t count,
                           std::uint64_t texture, UiRect scissor);
    std::vector<UiVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<UiBatch> batches_;
    std::vector<UiTextRun> text_;
    std::vector<UiModuleSlot> modules_;
    std::vector<UiHitRegion> hits_;
    const UiTheme* theme_ = nullptr;
    std::uint64_t next_order_ = 0;
    bool last_was_geometry_ = false;
};

struct UiVirtualWindow { std::size_t first=0, count=0; float top_padding=0, bottom_padding=0; };
[[nodiscard]] UiVirtualWindow virtualize_rows(std::size_t total,
                                              float row_height,
                                              float scroll_y,
                                              float viewport_height,
                                              std::size_t overscan = 2);

[[nodiscard]] inline UiVirtualWindow virtualize_variable_rows(std::span<const float> row_offsets,
                                                              float scroll_y,
                                                              float viewport_height,
                                                              std::size_t overscan = 2) {
    UiVirtualWindow w;
    if (row_offsets.size() < 2 || !std::isfinite(scroll_y) ||
        !std::isfinite(viewport_height) || viewport_height <= 0.0f) return w;
    for (std::size_t i = 0; i < row_offsets.size(); ++i) {
        if (!std::isfinite(row_offsets[i]) ||
            (i > 0 && row_offsets[i] < row_offsets[i - 1])) return w;
    }
    scroll_y = std::max(0.0f, scroll_y);
    const float viewport_end = scroll_y + viewport_height;
    if (!std::isfinite(viewport_end)) return w;
    const std::size_t total = row_offsets.size() - 1;
    // Binary search for first visible: lower_bound on row_offsets
    std::size_t first_vis = total;
    {
        auto it = std::upper_bound(row_offsets.begin(), row_offsets.begin() + static_cast<std::ptrdiff_t>(total + 1), scroll_y);
        if (it != row_offsets.begin()) {
            const auto idx = static_cast<std::size_t>(it - row_offsets.begin()) - 1;
            if (idx < total && row_offsets[idx + 1] > scroll_y) first_vis = idx;
        }
    }
    std::size_t last_vis = 0;
    {
        auto it = std::lower_bound(row_offsets.begin(), row_offsets.begin() + static_cast<std::ptrdiff_t>(total + 1), viewport_end);
        if (it == row_offsets.begin() + static_cast<std::ptrdiff_t>(total + 1)) last_vis = total - 1;
        else {
            const auto idx = static_cast<std::size_t>(it - row_offsets.begin());
            last_vis = idx > 0 ? idx - 1 : 0;
            // Extend while row still intersects viewport (variable height)
            while (last_vis + 1 < total && row_offsets[last_vis + 1] < viewport_end) ++last_vis;
        }
    }
    if (first_vis == total) {
        // Viewport beyond content: show empty
        w.first = total;
        w.count = 0;
        w.top_padding = row_offsets[total];
        w.bottom_padding = 0;
        return w;
    }
    const auto first = first_vis > overscan ? first_vis - overscan : 0u;
    const auto remaining = total - last_vis;
    const auto extension = overscan >= remaining - 1u ? remaining - 1u : overscan;
    const auto last = last_vis + 1u + extension;
    w.first = first;
    w.count = last > first ? last - first : 0u;
    w.top_padding = row_offsets[first];
    w.bottom_padding = row_offsets[total] - row_offsets[std::min(total, first + w.count)];
    return w;
}

[[nodiscard]] inline std::pair<float, float> chart_range(std::span<const float> series, bool robust_clamp = true) {
    if (series.empty()) return {0.0f, 1.0f};
    float min_v = 0.0f, max_v = 0.0f;
    bool found_finite = false;
    for (float v : series) {
        if (!std::isfinite(v)) continue;
        if (!found_finite) {
            min_v = max_v = v;
            found_finite = true;
            continue;
        }
        min_v = std::min(min_v, v);
        max_v = std::max(max_v, v);
    }
    if (!found_finite) return {0.0f, 1.0f};
    if (robust_clamp && series.size() > 20) {
        std::vector<float> sorted(series.begin(), series.end());
        sorted.erase(std::remove_if(sorted.begin(), sorted.end(),
                                     [](float v) { return !std::isfinite(v); }), sorted.end());
        if (sorted.empty()) return {0.0f, 1.0f};
        std::sort(sorted.begin(), sorted.end());
        const std::size_t p95_idx = std::min(sorted.size() - 1, static_cast<std::size_t>(static_cast<float>(sorted.size()) * 0.95f));
        const std::size_t p05_idx = static_cast<std::size_t>(static_cast<float>(sorted.size()) * 0.05f);
        max_v = sorted[p95_idx];
        min_v = sorted[std::min(p05_idx, p95_idx)];
    }
    if (max_v <= min_v) max_v = min_v + 1.0f;
    return {min_v, max_v};
}

inline void build_chart_polyline(std::span<const float> series, UiRect rect, std::size_t max_points,
                                  std::vector<float>& polyline, std::pair<float, float> range) {
    polyline.clear();
    if (series.size() < 2 || !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.w) || !std::isfinite(rect.h) || rect.w <= 0.0f ||
        rect.h <= 0.0f || !std::isfinite(range.first) || !std::isfinite(range.second)) return;
    const std::size_t n = std::min(series.size(), max_points > 0 ? max_points : series.size());
    if (n < 2) return;
    const float step_idx = static_cast<float>(series.size() - 1) / static_cast<float>(n - 1);
    const float dx = rect.w / static_cast<float>(n - 1);
    const float range_span = std::max(1e-4f, range.second - range.first);

    for (std::size_t i = 0; i < n; ++i) {
        const auto start_idx = std::min(
            series.size() - 1u,
            static_cast<std::size_t>(static_cast<float>(i) * step_idx));
        const auto end_idx = std::min(
            series.size() - 1u,
            static_cast<std::size_t>(static_cast<float>(i + 1u) * step_idx));
        float val = std::isfinite(series[start_idx]) ? series[start_idx] : range.first;
        for (std::size_t k = start_idx; k <= end_idx && k < series.size(); ++k) {
            if (std::isfinite(series[k]) && series[k] > val) val = series[k];
        }
        const float px = rect.x + static_cast<float>(i) * dx;
        const float norm_y = std::clamp((val - range.first) / range_span, 0.0f, 1.0f);
        const float py = rect.y + rect.h - norm_y * rect.h;
        polyline.push_back(px);
        polyline.push_back(py);
    }
}

[[nodiscard]] inline UiRect place_tooltip(UiRect target_rect, float tw, float th, UiRect screen, float padding = 5.0f) {
    if (!std::isfinite(target_rect.x) || !std::isfinite(target_rect.y) ||
        !std::isfinite(target_rect.w) || !std::isfinite(target_rect.h) ||
        !std::isfinite(tw) || !std::isfinite(th) || !std::isfinite(screen.x) ||
        !std::isfinite(screen.y) || !std::isfinite(screen.w) ||
        !std::isfinite(screen.h) || !std::isfinite(padding) || tw <= 0.0f ||
        th <= 0.0f || target_rect.w < 0.0f || target_rect.h < 0.0f ||
        screen.w <= 0.0f || screen.h <= 0.0f || padding < 0.0f ||
        !std::isfinite(target_rect.x + target_rect.w) ||
        !std::isfinite(target_rect.y + target_rect.h) ||
        !std::isfinite(screen.x + screen.w) || !std::isfinite(screen.y + screen.h))
        return {};
    float x = target_rect.x + target_rect.w + padding;
    float y = target_rect.y;
    if (x + tw > screen.x + screen.w - padding) {
        x = target_rect.x - tw - padding;
    }
    if (x < screen.x + padding) x = screen.x + padding;
    if (y + th > screen.y + screen.h - padding) {
        y = screen.y + screen.h - padding - th;
    }
    if (y < screen.y + padding) y = screen.y + padding;
    return {x, y, tw, th};
}

} // namespace thunder
