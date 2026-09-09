#include "thunder/presentation/ui/StrategyUi.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace thunder {

void UiDrawList::v_gradient(UiRect rect, std::uint32_t top_rgba, std::uint32_t bottom_rgba,
                            int /*bands*/, UiRect scissor) {
    quad_gradient(rect, top_rgba, bottom_rgba, true, scissor);
}

void UiDrawList::corner_ornaments(UiRect rect, std::uint32_t rgba, float size, UiRect scissor) {
    if (rect.w <= size * 2.0f || rect.h <= size * 2.0f || size <= 0.0f) return;
    const auto color = rgba != 0 ? rgba : theme().colors.border_gold;
    const float thick = std::max(1.0f, size * 0.25f);
    const float corners[4][2] = {
        {rect.x, rect.y},
        {rect.x + rect.w - size, rect.y},
        {rect.x, rect.y + rect.h - size},
        {rect.x + rect.w - size, rect.y + rect.h - size},
    };
    for (const auto& c : corners) {
        quad({c[0], c[1], size, thick}, color, scissor);
        quad({c[0], c[1], thick, size}, color, scissor);
    }
}

void UiDrawList::divider_ornament(UiRect rect, UiRect scissor) {
    if (rect.w <= 8.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float mid_y = rect.y + rect.h * 0.5f;
    const float cx = rect.x + rect.w * 0.5f;
    // Hairlines fading toward the center, diamond boss in the middle
    quad({rect.x, mid_y, rect.w * 0.5f - 8.0f, 1.0f}, t.colors.border_normal, scissor);
    quad({cx + 8.0f, mid_y, rect.w * 0.5f - 8.0f, 1.0f}, t.colors.border_normal, scissor);
    quad({cx - 3.0f, mid_y - 3.0f, 6.0f, 6.0f}, t.colors.border_gold, scissor);
    quad({cx - 1.0f, mid_y - 1.0f, 2.0f, 2.0f}, t.colors.border_selected, scissor);
}

void UiDrawList::separator(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    quad({rect.x, rect.y, rect.w, std::max(1.0f, rect.h * 0.5f)}, t.colors.border_dark, scissor);
    quad({rect.x, rect.y + std::max(1.0f, rect.h * 0.5f), rect.w, std::max(1.0f, rect.h * 0.5f)},
         ui_blend(t.colors.border_light, 0xff000000u, 0.5f), scissor);
}

void UiDrawList::ornate_header(UiRect rect, const std::string& title, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Dark walnut slab with top light and bottom shade
    v_gradient(rect, ui_blend(t.colors.bg_header, 0xffffffffu, 0.05f),
               ui_blend(t.colors.bg_header, 0xff000000u, 0.25f), 4, scissor);
    // Leather inlay strip
    if (rect.h > 8.0f)
        quad({rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f},
             ui_blend(t.materials.leather_base, t.colors.bg_header, 0.5f), scissor);
    // Gold termination lines
    quad({rect.x, rect.y, rect.w, 1.0f}, t.colors.border_gold, scissor);
    quad({rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f}, t.colors.border_gold, scissor);
    corner_ornaments(rect, 0, 4.0f, scissor);

    if (!title.empty()) {
        // Engraved title: dark shadow pass under bright serif pass
        text(title, rect.x + 10.0f, rect.y + (rect.h - t.type.window_title) * 0.5f + 1.0f,
             t.type.window_title, 0x80000000u, scissor);
        text(title, rect.x + 10.0f, rect.y + (rect.h - t.type.window_title) * 0.5f,
             t.type.window_title, t.colors.text_gold, scissor);
    }
}

void UiDrawList::window_frame(UiRect rect, const std::string& title, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Soft floating shadow + dark outer frame + panel body
    drop_shadow(rect, t.colors.shadow_modal, 6.0f, 3.0f, 4.0f, scissor);
    quad(rect, t.colors.border_dark, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, t.colors.border_normal, scissor);
    if (rect.w > 4.0f && rect.h > 4.0f)
        quad({rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f}, t.colors.bg_panel, scissor);

    // Ornate header band; content starts below it
    const float header_h = std::min(t.metrics.header_height, rect.h * 0.4f);
    ornate_header({rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, header_h}, title, scissor);
    // Gold rule under the header
    quad({rect.x + 4.0f, rect.y + 2.0f + header_h, rect.w - 8.0f, 1.0f}, t.colors.border_gold, scissor);
    corner_ornaments(rect, 0, 5.0f, scissor);
}

void UiDrawList::tab(UiRect rect, const std::string& label, bool active, bool hovered, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    if (active) {
        // Active tab steps forward and fuses with the content area below:
        // raised leather-toned face, gold frame open at the bottom edge.
        const UiRect face{rect.x, rect.y, rect.w, rect.h + 2.0f};
        quad({face.x + 2.0f, face.y + 2.0f, face.w, face.h}, t.colors.shadow_raised, scissor);
        quad(face, t.colors.border_gold, scissor);
        if (face.w > 2.0f && face.h > 2.0f)
            quad({face.x + 1.0f, face.y + 1.0f, face.w - 2.0f, face.h - 1.0f},
                 t.colors.bg_panel_raised, scissor);
        quad({face.x + 2.0f, face.y + 1.0f, face.w - 4.0f, 1.0f}, t.colors.border_selected, scissor);
    } else {
        // Inactive tab recedes: recessed dark face, bronze edge
        auto fill = t.colors.bg_panel_recessed;
        if (hovered) fill = ui_apply_overlay(fill, t.colors.state_hover);
        quad(rect, t.colors.border_normal, scissor);
        if (rect.w > 2.0f && rect.h > 2.0f)
            quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 1.0f}, fill, scissor);
    }
    if (!label.empty()) {
        const auto color = active ? t.colors.text_gold :
                           hovered ? t.colors.text_primary : t.colors.text_secondary;
        text(label, rect.x + 8.0f, rect.y + (rect.h - t.type.body) * 0.5f, t.type.body, color, scissor);
    }
}

void UiDrawList::dropdown_row(UiRect rect, const std::string& label, bool hovered,
                              bool selected, bool disabled, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    if (hovered && !disabled)
        quad(rect, ui_apply_overlay(t.colors.bg_panel_raised, t.colors.state_hover), scissor);
    else if (selected)
        quad(rect, ui_blend(t.colors.bg_panel_raised, t.colors.state_selected, 0.18f), scissor);

    if (!label.empty()) {
        const auto color = disabled ? t.colors.text_disabled :
                           selected ? t.colors.text_gold : t.colors.text_primary;
        text(label, rect.x + 8.0f, rect.y + (rect.h - t.type.body) * 0.5f, t.type.body, color, scissor);
    }
    // Selected mark (brass tick) and submenu indicator drawn as simple geometry
    if (selected && !disabled)
        quad({rect.x + 2.0f, rect.y + rect.h * 0.5f - 2.0f, 4.0f, 4.0f}, t.colors.state_selected, scissor);
}

void UiDrawList::checkbox(UiRect rect, bool checked, bool hovered, bool disabled, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float s = std::min(rect.w, rect.h);
    const float x = rect.x + (rect.w - s) * 0.5f;
    const float y = rect.y + (rect.h - s) * 0.5f;

    const auto rim = disabled ? t.colors.text_disabled :
                     hovered ? t.colors.border_selected : t.colors.border_brass;
    quad({x, y, s, s}, rim, scissor);
    const auto well = disabled ? ui_blend(t.colors.bg_deep, 0xffffffffu, 0.1f) : t.colors.bg_deep;
    if (s > 2.0f) quad({x + 1.0f, y + 1.0f, s - 2.0f, s - 2.0f}, well, scissor);
    if (checked && s > 6.0f)
        quad({x + 3.0f, y + 3.0f, s - 6.0f, s - 6.0f}, disabled ? t.colors.text_disabled : t.colors.gold, scissor);
}

void UiDrawList::radio(UiRect rect, bool selected, bool hovered, bool disabled, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float s = std::min(rect.w, rect.h);
    const float x = rect.x + (rect.w - s) * 0.5f;
    const float y = rect.y + (rect.h - s) * 0.5f;

    const auto rim = disabled ? t.colors.text_disabled :
                     hovered ? t.colors.border_selected : t.colors.border_brass;
    // Ring approximation from four edge quads over a dark well
    quad({x, y, s, s}, rim, scissor);
    if (s > 2.0f) quad({x + 1.0f, y + 1.0f, s - 2.0f, s - 2.0f}, t.colors.bg_deep, scissor);
    if (selected && s > 6.0f)
        quad({x + 3.0f, y + 3.0f, s - 6.0f, s - 6.0f}, disabled ? t.colors.text_disabled : t.colors.gold, scissor);
}

void UiDrawList::slider(UiRect rect, float fraction, bool hovered, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float f = std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 0.0f;
    const float track_h = std::min(t.metrics.slider_track_height, rect.h);
    const float track_y = rect.y + (rect.h - track_h) * 0.5f;

    // Recessed groove
    quad({rect.x, track_y, rect.w, track_h}, t.colors.border_dark, scissor);
    if (track_h > 2.0f)
        quad({rect.x + 1.0f, track_y + 1.0f, rect.w - 2.0f, track_h - 2.0f}, t.colors.bg_deep, scissor);
    const float fill_w = (rect.w - 2.0f) * f;
    if (fill_w > 0.0f && track_h > 2.0f)
        quad({rect.x + 1.0f, track_y + 1.0f, fill_w, track_h - 2.0f}, t.colors.gold, scissor);

    // Brass thumb
    const float thumb_w = 8.0f;
    const float thumb_h = track_h + 8.0f;
    const float thumb_x = rect.x + fill_w - thumb_w * 0.5f + 1.0f;
    const float thumb_y = track_y - 4.0f;
    quad({thumb_x, thumb_y, thumb_w, thumb_h}, hovered ? t.colors.border_selected : t.materials.brass_rim, scissor);
    if (thumb_w > 2.0f && thumb_h > 2.0f)
        quad({thumb_x + 1.0f, thumb_y + 1.0f, thumb_w - 2.0f, thumb_h - 2.0f}, t.materials.brass_highlight, scissor);
}

void UiDrawList::scrollbar(UiRect rect, float thumb_start_fraction, float thumb_size_fraction,
                           bool hovered, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float start = std::isfinite(thumb_start_fraction) ? std::clamp(thumb_start_fraction, 0.0f, 1.0f) : 0.0f;
    const float size = std::isfinite(thumb_size_fraction) ? std::clamp(thumb_size_fraction, 0.05f, 1.0f) : 1.0f;
    const bool vertical = rect.h >= rect.w;

    // Discreet recessed track
    quad(rect, ui_blend(t.colors.bg_deep, 0xff000000u, 0.25f), scissor);

    const float track_len = vertical ? rect.h : rect.w;
    const float thumb_len = std::max(16.0f, track_len * size);
    const float thumb_off = (track_len - thumb_len) * start;
    const auto thumb_color = hovered ? t.colors.border_light : t.colors.border_normal;
    if (vertical) {
        quad({rect.x + 1.0f, rect.y + thumb_off, rect.w - 2.0f, thumb_len}, thumb_color, scissor);
        quad({rect.x + 1.0f, rect.y + thumb_off, 1.0f, thumb_len}, t.colors.border_light, scissor);
    } else {
        quad({rect.x + thumb_off, rect.y + 1.0f, thumb_len, rect.h - 2.0f}, thumb_color, scissor);
        quad({rect.x + thumb_off, rect.y + 1.0f, thumb_len, 1.0f}, t.colors.border_light, scissor);
    }
}

void UiDrawList::input_box(UiRect rect, const std::string& text_value, bool focused, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    quad(rect, focused ? t.colors.state_focus : t.colors.border_normal, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, t.colors.bg_deep, scissor);
    // Inner top shadow for the recessed feel
    if (rect.w > 2.0f && rect.h > 3.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, 2.0f}, 0x40000000u, scissor);
    if (!text_value.empty())
        text(text_value, rect.x + 6.0f, rect.y + (rect.h - t.type.body) * 0.5f,
             t.type.body, t.colors.text_primary, scissor);
    else if (focused)
        quad({rect.x + 6.0f, rect.y + 4.0f, 1.0f, rect.h - 8.0f}, t.colors.text_primary, scissor);
}

void UiDrawList::list_row(UiRect rect, const std::string& primary, const std::string& secondary,
                          const std::string& value, bool hovered, bool selected,
                          bool striped, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    if (selected)
        quad(rect, ui_blend(t.colors.bg_panel_raised, t.colors.state_selected, 0.22f), scissor);
    else if (hovered)
        quad(rect, ui_apply_overlay(t.colors.bg_panel, t.colors.state_hover), scissor);
    else if (striped)
        quad(rect, ui_blend(t.colors.bg_panel, 0xff000000u, 0.16f), scissor);

    if (selected)
        quad({rect.x, rect.y, 2.0f, rect.h}, t.colors.state_selected, scissor);

    const float baseline_y = rect.y + (rect.h - t.type.body) * 0.5f;
    if (!primary.empty())
        text(primary, rect.x + 8.0f, baseline_y, t.type.body,
             selected ? t.colors.text_gold : t.colors.text_primary, scissor);
    if (!secondary.empty())
        text(secondary, rect.x + rect.w * 0.45f, baseline_y, t.type.secondary, t.colors.text_muted, scissor);
    if (!value.empty()) {
        // Numeric values right-align for scannable columns
        const float est_w = static_cast<float>(value.size()) * t.type.stat_value * 0.55f;
        text(value, rect.x + rect.w - 8.0f - est_w, baseline_y, t.type.stat_value, t.colors.text_primary, scissor);
    }
    // Hairline separator under the row
    quad({rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f},
         ui_blend(t.colors.border_dark, 0xffffffffu, 0.06f), scissor);
}

void UiDrawList::table_header_cell(UiRect rect, const std::string& label, bool right_aligned, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    v_gradient(rect, ui_blend(t.colors.bg_header, 0xffffffffu, 0.04f), t.colors.bg_header, 3, scissor);
    quad({rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f}, t.colors.border_gold, scissor);
    if (!label.empty()) {
        const float est_w = static_cast<float>(label.size()) * t.type.caption * 0.6f;
        const float x = right_aligned ? rect.x + rect.w - 8.0f - est_w : rect.x + 8.0f;
        text(label, x, rect.y + (rect.h - t.type.caption) * 0.5f, t.type.caption, t.colors.text_secondary, scissor);
    }
}

void UiDrawList::stat_row(UiRect rect, const std::string& label, const std::string& value,
                          const std::string& delta, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float baseline_y = rect.y + (rect.h - t.type.body) * 0.5f;

    if (!label.empty())
        text(label, rect.x, baseline_y, t.type.body, t.colors.text_secondary, scissor);
    if (!value.empty()) {
        const float est_w = static_cast<float>(value.size()) * t.type.stat_value * 0.55f;
        text(value, rect.x + rect.w - est_w, baseline_y, t.type.stat_value, t.colors.text_primary, scissor);
        if (!delta.empty()) {
            const bool positive = !delta.empty() && delta[0] == '+';
            const bool negative = !delta.empty() && delta[0] == '-';
            const auto delta_color = positive ? t.colors.text_positive :
                                     negative ? t.colors.text_negative : t.colors.text_muted;
            const float delta_w = static_cast<float>(delta.size()) * t.type.stat_delta * 0.55f;
            text(delta, rect.x + rect.w - est_w - 8.0f - delta_w,
                 rect.y + (rect.h - t.type.stat_delta) * 0.5f, t.type.stat_delta, delta_color, scissor);
        }
    }
}

void UiDrawList::notification_card(UiRect rect, const std::string& title, const std::string& body,
                                   int severity, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Severity 0 = critical ... 4 = information
    std::uint32_t accent;
    std::uint32_t shadow;
    switch (std::clamp(severity, 0, 4)) {
    case 0: accent = t.colors.text_negative; shadow = t.colors.shadow_modal; break;
    case 1: accent = t.colors.text_warning; shadow = t.colors.shadow_floating; break;
    case 2: accent = t.colors.text_warning; shadow = t.colors.shadow_raised; break;
    default: accent = t.colors.border_normal; shadow = t.colors.shadow_raised; break;
    }

    drop_shadow(rect, shadow, 4.0f, 2.0f, 2.5f, scissor);
    quad(rect, severity <= 1 ? accent : t.colors.border_normal, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, t.colors.bg_floating, scissor);
    // Severity accent bar
    quad({rect.x + 1.0f, rect.y + 1.0f, 3.0f, rect.h - 2.0f}, accent, scissor);

    if (!title.empty())
        text(title, rect.x + 12.0f, rect.y + 6.0f, t.type.major_header,
             severity == 0 ? t.colors.text_negative : t.colors.text_gold, scissor);
    if (!body.empty())
        text(body, rect.x + 12.0f, rect.y + 6.0f + t.type.major_header + 3.0f,
             t.type.secondary, t.colors.text_secondary, scissor);
}

void UiDrawList::modal_window(UiRect rect, const std::string& title, const std::string& body, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Dimming veil is the caller's responsibility; the window itself gets the
    // deepest shadow tier and the highest ornament density.
    drop_shadow(rect, t.colors.shadow_modal, 7.0f, 3.0f, 5.0f, scissor);
    parchment_panel(rect, scissor);

    // Leather plaque title
    const float plaque_h = std::min(26.0f, rect.h * 0.2f);
    if (plaque_h > 8.0f && rect.w > 16.0f) {
        leather_panel({rect.x + rect.w * 0.15f, rect.y + 8.0f, rect.w * 0.7f, plaque_h}, 0, scissor);
        if (!title.empty()) {
            const float est_w = static_cast<float>(title.size()) * t.type.window_title * 0.6f;
            text(title, rect.x + (rect.w - est_w) * 0.5f, rect.y + 8.0f + (plaque_h - t.type.window_title) * 0.5f,
                 t.type.window_title, t.colors.text_gold, scissor);
        }
    }

    if (!body.empty())
        text(body, rect.x + 16.0f, rect.y + 8.0f + plaque_h + 10.0f,
             t.type.body, t.materials.parchment_text, scissor);

    // Wax seal crest in the bottom corner
    wax_seal(rect.x + rect.w - 22.0f, rect.y + rect.h - 22.0f, 14.0f, scissor);
}

void UiDrawList::text(std::string utf8, float x, float y, float size, std::uint32_t rgba, UiRect scissor, bool centered) {
    if (utf8.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size <= 0.0f) return;
    text_.push_back(UiTextRun{std::move(utf8), x, y, size, rgba, scissor,
                              0.0f, 0.0f, centered, false, next_order_++});
    last_was_geometry_ = false;
}

void UiDrawList::map_text(std::string utf8, float center_x, float center_y, float size,
                          std::uint32_t rgba, float angle_rad,
                          float letter_spacing, UiRect scissor) {
    if (utf8.empty() || !std::isfinite(center_x) || !std::isfinite(center_y) ||
        !std::isfinite(size) || size <= 0.0f || !std::isfinite(angle_rad) ||
        !std::isfinite(letter_spacing)) return;
    text_.push_back(UiTextRun{std::move(utf8), center_x, center_y, size, rgba, scissor,
                              angle_rad, letter_spacing, true, true, next_order_++});
    last_was_geometry_ = false;
}

void UiDrawList::hit(std::uint64_t id, UiRect rect) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(rect.x + rect.w) || !std::isfinite(rect.y + rect.h)) return;
    hits_.push_back(UiHitRegion{id, rect});
}

std::optional<std::uint64_t> UiDrawList::hit_test(float x, float y) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (x >= it->rect.x && x <= it->rect.x + it->rect.w &&
            y >= it->rect.y && y <= it->rect.y + it->rect.h) {
            return it->id;
        }
    }
    return std::nullopt;
}

UiVirtualWindow virtualize_rows(std::size_t total,
                                float row_height,
                                float scroll_y,
                                float viewport_height,
                                std::size_t overscan) {
    UiVirtualWindow w;
    if (total == 0 || !std::isfinite(row_height) || row_height <= 0.0f ||
        !std::isfinite(viewport_height) || viewport_height <= 0.0f) return w;

    const float safe_scroll = std::isfinite(scroll_y) ? std::max(0.0f, scroll_y) : 0.0f;
    const double first_exact = static_cast<double>(safe_scroll) /
        static_cast<double>(row_height);
    const auto first_raw = first_exact >= static_cast<double>(total)
        ? total : static_cast<std::size_t>(first_exact);
    const double visible_exact = std::ceil(static_cast<double>(viewport_height) /
                                           static_cast<double>(row_height));
    const auto vis_count = visible_exact >= static_cast<double>(total)
        ? total : static_cast<std::size_t>(visible_exact);

    const auto first = first_raw > overscan ? first_raw - overscan : 0u;
    const auto visible_plus_one = vis_count == std::numeric_limits<std::size_t>::max()
        ? std::numeric_limits<std::size_t>::max() : vis_count + 1u;
    const auto extra = visible_plus_one > std::numeric_limits<std::size_t>::max() - overscan
        ? std::numeric_limits<std::size_t>::max() : visible_plus_one + overscan;
    const auto last = first_raw > total - std::min(total, extra)
        ? total : first_raw + std::min(total - first_raw, extra);

    w.first = first;
    w.count = last >= first ? (last - first) : 0u;
    w.top_padding = static_cast<float>(first) * row_height;
    w.bottom_padding = static_cast<float>(total - last) * row_height;
    return w;
}

} // namespace thunder
