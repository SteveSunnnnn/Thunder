#include "thunder/presentation/ui/ScriptedGuiPainter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace thunder {
namespace {

[[nodiscard]] float bounded(float value, float minimum, float maximum) noexcept {
    if (!std::isfinite(value)) return minimum;
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] UiRect inset(UiRect rect, float amount) noexcept {
    const float inset_x = std::min(std::max(amount, 0.0f), rect.w * 0.5f);
    const float inset_y = std::min(std::max(amount, 0.0f), rect.h * 0.5f);
    return {rect.x + inset_x, rect.y + inset_y,
            std::max(0.0f, rect.w - inset_x * 2.0f),
            std::max(0.0f, rect.h - inset_y * 2.0f)};
}

[[nodiscard]] bool is_hud_panel(UiSurfaceStyle style) noexcept {
    return style == UiSurfaceStyle::Hud || style == UiSurfaceStyle::Dock ||
           style == UiSurfaceStyle::Outliner;
}

[[nodiscard]] float estimated_text_width(std::string_view text, float size) noexcept {
    float em = 0.0f;
    for (std::size_t index = 0; index < text.size();) {
        const auto c = static_cast<unsigned char>(text[index]);
        if (c < 0x80u) {
            if (c == ' ') em += .31f;
            else if (c >= '0' && c <= '9') em += .53f;
            else if (c >= 'A' && c <= 'Z') em += .61f;
            else if ((c >= 'a' && c <= 'z')) em += .48f;
            else em += .34f;
            ++index;
            continue;
        }
        em += .96f;
        if ((c & 0xe0u) == 0xc0u) index += 2u;
        else if ((c & 0xf0u) == 0xe0u) index += 3u;
        else if ((c & 0xf8u) == 0xf0u) index += 4u;
        else ++index;
        index = std::min(index, text.size());
    }
    return em * size;
}

[[nodiscard]] float fit_text_size(UiRect rect, std::string_view text, float desired,
                                  float minimum = 12.0f, float padding = 0.0f) noexcept {
    const float available = std::max(1.0f, rect.w - padding * 2.0f);
    const float estimated = estimated_text_width(text, desired);
    if (estimated <= available || estimated <= 0.0f) return desired;
    return std::clamp(desired * available / estimated, minimum, desired);
}

[[nodiscard]] float centered_text_x(UiRect rect, std::string_view text, float size) noexcept {
    const float estimated_width = estimated_text_width(text, size);
    return rect.x + std::max(0.0f, (rect.w - estimated_width) * 0.5f);
}

[[nodiscard]] std::vector<std::string> wrap_tooltip_text(std::string_view text,
                                                         float width, float size) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto c = static_cast<unsigned char>(text[cursor]);
        if (c == ' ') {
            if (!line.empty() && line.back() != ' ') line.push_back(' ');
            ++cursor;
            continue;
        }
        // Extract next unit: if CJK/multibyte, unit is one codepoint; if ASCII word, unit is word up to next space or CJK
        std::size_t unit_len = 1;
        if ((c & 0x80u) != 0u) {
            if ((c & 0xe0u) == 0xc0u) unit_len = 2;
            else if ((c & 0xf0u) == 0xe0u) unit_len = 3;
            else if ((c & 0xf8u) == 0xf0u) unit_len = 4;
            unit_len = std::min(unit_len, text.size() - cursor);
        } else {
            while (cursor + unit_len < text.size()) {
                const auto next_c = static_cast<unsigned char>(text[cursor + unit_len]);
                if (next_c == ' ' || (next_c & 0x80u) != 0u) break;
                ++unit_len;
            }
        }
        const std::string unit{text.substr(cursor, unit_len)};
        cursor += unit_len;

        std::string candidate = line + unit;
        if (!line.empty() && estimated_text_width(candidate, size) > width) {
            lines.push_back(std::move(line));
            line = unit;
        } else {
            line = std::move(candidate);
        }
    }
    if (!line.empty()) lines.push_back(std::move(line));
    if (lines.empty() && !text.empty()) lines.emplace_back(text);
    if (lines.size() > 2u) {
        lines.resize(2u);
        auto& last = lines.back();
        while (!last.empty() && estimated_text_width(last + "...", size) > width) {
            while (!last.empty() && (static_cast<unsigned char>(last.back()) & 0xc0u) == 0x80u) {
                last.pop_back();
            }
            if (!last.empty()) last.pop_back();
        }
        last += "...";
    }
    return lines;
}

[[nodiscard]] UiControlVisualState control_state(const UiRuntimeNode& node) noexcept {
    return UiControlVisualState{
        .enabled = node.enabled,
        .selected = node.selected,
        .hovered = node.hovered,
        .pressed = node.pressed,
        .focused = node.focused,
        .hover_mix = node.hover_mix,
        .press_mix = node.press_mix,
        .selected_mix = node.selected_mix,
        .focus_mix = node.focus_mix
    };
}

} // namespace

ScriptedGuiPainter::ScriptedGuiPainter(const ScriptedGuiBlueprint& blueprint,
                                       ScriptedGuiPaintTheme theme)
    : blueprint_(&blueprint), theme_(theme) {}

ScriptedGuiPaintTheme make_scripted_gui_paint_theme(const UiTheme& theme) noexcept {
    ScriptedGuiPaintTheme paint;
    paint.panel = theme.colors.bg_panel;
    paint.panel_raised = theme.colors.bg_panel_raised;
    paint.panel_dark = theme.colors.bg_panel_recessed;
    paint.panel_header = theme.colors.bg_header;
    paint.border = theme.colors.border_normal;
    paint.border_light = theme.colors.border_light;
    paint.border_gold = theme.colors.border_gold;
    paint.border_selected = theme.colors.border_selected;
    paint.accent = theme.colors.gold;
    paint.emerald = theme.colors.emerald;
    paint.text = theme.colors.text_primary;
    paint.text_muted = theme.colors.text_muted;
    paint.text_gold = theme.colors.text_gold;
    paint.positive = theme.colors.text_positive;
    paint.negative = theme.colors.text_negative;
    paint.hover_overlay = theme.colors.state_hover;
    paint.selected_fill = theme.colors.state_selected;
    paint.chart_fill = ui_blend(0x00000000u, theme.colors.emerald, 0.25f);
    paint.font_size = theme.type.body;
    paint.header_font_size = theme.type.window_title;
    paint.padding = theme.metrics.space_md;
    paint.gap = theme.metrics.space_sm;
    return paint;
}

ScriptedGuiPainter::LayoutStyle
ScriptedGuiPainter::style_for(const UiRuntimeNode& node) const noexcept {
    LayoutStyle style;
    if (blueprint_ == nullptr || !node.blueprint_node.valid() ||
        node.blueprint_node.value() >= blueprint_->nodes().size()) return style;
    const auto& compiled = blueprint_->nodes()[node.blueprint_node.value()];
    for (std::size_t index = 0; index < compiled.constant_count; ++index) {
        const auto& value = blueprint_->constants()[compiled.first_constant + index];
        if (value.value_type != UiValueType::Number) continue;
        const float number = static_cast<float>(value.number());
        switch (value.target) {
        case UiConstantTarget::Width: style.width = number; break;
        case UiConstantTarget::Height: style.height = number; break;
        case UiConstantTarget::MinWidth: style.min_width = number; break;
        case UiConstantTarget::MinHeight: style.min_height = number; break;
        case UiConstantTarget::MaxWidth: style.max_width = number; break;
        case UiConstantTarget::MaxHeight: style.max_height = number; break;
        case UiConstantTarget::Grow: style.grow = std::max(number, 0.0f); break;
        case UiConstantTarget::Gap: style.gap = std::max(number, 0.0f); break;
        default: break;
        }
    }
    return style;
}

void ScriptedGuiPainter::paint(const ScriptedGuiRuntime& runtime,
                               UiDrawList& draw_list,
                               UiRect screen,
                               const ScriptedGuiTextResolver& resolve_text) const {
    if (blueprint_ == nullptr || runtime.root_index() == kInvalidUiRuntimeNode ||
        runtime.root_index() >= runtime.nodes().size() || screen.w <= 0.0f || screen.h <= 0.0f) return;
    tooltip_.reset();
    paint_node(runtime, runtime.root_index(), draw_list, screen, resolve_text, 0u);
    if (tooltip_ && resolve_text) {
        const auto tooltip_text = resolve_text(tooltip_->key);
        if (!tooltip_text.empty()) draw_tooltip(draw_list, screen, tooltip_->anchor, tooltip_text);
    }
    tooltip_.reset();
}

void ScriptedGuiPainter::paint_children(const ScriptedGuiRuntime& runtime,
                                        const UiRuntimeNode& node,
                                        UiDrawList& draw_list,
                                        UiRect content,
                                        const ScriptedGuiTextResolver& resolve_text,
                                        std::uint16_t depth) const {
    // Collect visible children without a per-node heap allocation. A small
    // inline buffer covers the overwhelmingly common case (rows/columns/stacks
    // with a handful of children); deeper trees spill to a heap vector that is
    // reused across the recursion. This is a pure allocation-count change and
    // does not alter the resulting layout or draw order.
    std::array<std::uint32_t, 24> inline_children{};
    std::vector<std::uint32_t> overflow_children;
    std::uint32_t count = 0;
    auto child = node.first_child;
    while (child != kInvalidUiRuntimeNode && child < runtime.nodes().size()) {
        if (runtime.nodes()[child].visible) {
            if (count < inline_children.size()) {
                inline_children[count] = child;
            } else {
                if (count == inline_children.size()) {
                    overflow_children.assign(inline_children.begin(), inline_children.end());
                }
                overflow_children.push_back(child);
            }
            ++count;
        }
        child = runtime.nodes()[child].next_sibling;
    }
    if (count == 0) return;
    const std::uint32_t* const children =
        count <= inline_children.size() ? inline_children.data() : overflow_children.data();

    if (node.kind == UiWidgetKind::Stack || node.kind == UiWidgetKind::Panel) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto index = children[i];
            const auto child_style = style_for(runtime.nodes()[index]);
            UiRect child_rect = content;
            if (child_style.width >= 0.0f)
                child_rect.w = bounded(child_style.width, child_style.min_width, child_style.max_width);
            if (child_style.height >= 0.0f)
                child_rect.h = bounded(child_style.height, child_style.min_height, child_style.max_height);
            paint_node(runtime, index, draw_list, child_rect, resolve_text, depth + 1u);
        }
        return;
    }

    const bool horizontal = node.kind == UiWidgetKind::Row;
    const float gap = [&] {
        const auto own = style_for(node);
        return own.gap >= 0.0f ? own.gap : theme_.gap;
    }();
    const float available = std::max(0.0f, (horizontal ? content.w : content.h) -
        gap * static_cast<float>(count - 1u));
    float fixed = 0.0f;
    float total_grow = 0.0f;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = children[i];
        const auto child_style = style_for(runtime.nodes()[index]);
        const float explicit_size = horizontal ? child_style.width : child_style.height;
        if (explicit_size >= 0.0f) fixed += explicit_size;
        else total_grow += child_style.grow > 0.0f ? child_style.grow : 1.0f;
    }
    const float flexible = std::max(0.0f, available - fixed);
    float cursor = horizontal ? content.x : content.y;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = children[i];
        const auto child_style = style_for(runtime.nodes()[index]);
        const float explicit_size = horizontal ? child_style.width : child_style.height;
        const float weight = child_style.grow > 0.0f ? child_style.grow : 1.0f;
        float size = explicit_size >= 0.0f ? explicit_size :
            (total_grow > 0.0f ? flexible * weight / total_grow : 0.0f);
        size = horizontal ? bounded(size, child_style.min_width, child_style.max_width)
                          : bounded(size, child_style.min_height, child_style.max_height);
        UiRect child_rect = content;
        if (horizontal) {
            child_rect.x = cursor;
            child_rect.w = size;
        } else {
            child_rect.y = cursor;
            child_rect.h = size;
        }
        paint_node(runtime, index, draw_list, child_rect, resolve_text, depth + 1u);
        cursor += size + gap;
    }
}

void ScriptedGuiPainter::draw_icon(UiDrawList& draw_list,
                                   UiStableKey icon_key,
                                   UiRect rect,
                                   std::uint32_t color,
                                   UiRect scissor) const {
    if (icon_key == 0 || rect.w <= 0.0f || rect.h <= 0.0f) return;
    const float s = std::min(rect.w, rect.h);
    const float x = rect.x + (rect.w - s) * 0.5f;
    const float y = rect.y + (rect.h - s) * 0.5f;
    const auto line = [&](std::initializer_list<float> points) {
        std::vector<float> xy(points);
        draw_list.polyline(xy, color, scissor);
    };
    const auto bar = [&](float bx, float by, float bw, float bh) {
        draw_list.quad({x + bx*s, y + by*s, bw*s, bh*s}, color, scissor);
    };
    const auto circle = [&](float cx, float cy, float radius) {
        std::vector<float> xy;
        constexpr int segments = 12;
        xy.reserve((segments + 1) * 2);
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) * 6.283185307f / static_cast<float>(segments);
            xy.push_back(x + (cx + std::cos(a)*radius)*s);
            xy.push_back(y + (cy + std::sin(a)*radius)*s);
        }
        draw_list.polyline(xy, color, scissor);
    };

    if (icon_key == ui_stable_key("icon.country")) {
        line({x+s*.24f,y+s*.16f, x+s*.76f,y+s*.16f, x+s*.70f,y+s*.70f,
              x+s*.50f,y+s*.86f, x+s*.30f,y+s*.70f, x+s*.24f,y+s*.16f});
        bar(.38f,.26f,.08f,.38f); bar(.54f,.26f,.08f,.38f);
    } else if (icon_key == ui_stable_key("icon.rank")) {
        line({x+s*.16f,y+s*.68f, x+s*.22f,y+s*.30f, x+s*.42f,y+s*.50f,
              x+s*.50f,y+s*.22f, x+s*.60f,y+s*.50f, x+s*.80f,y+s*.30f,
              x+s*.84f,y+s*.68f, x+s*.16f,y+s*.68f});
        bar(.22f,.73f,.56f,.07f);
    } else if (icon_key == ui_stable_key("icon.treasury")) {
        circle(.50f,.50f,.30f); circle(.50f,.50f,.23f); bar(.47f,.30f,.06f,.40f);
        bar(.38f,.38f,.24f,.05f); bar(.38f,.57f,.24f,.05f);
    } else if (icon_key == ui_stable_key("icon.economy")) {
        circle(.50f,.43f,.25f); bar(.47f,.18f,.06f,.50f);
        bar(.36f,.31f,.28f,.05f); bar(.36f,.51f,.28f,.05f);
        bar(.26f,.73f,.48f,.06f);
    } else if (icon_key == ui_stable_key("icon.balance") || icon_key == ui_stable_key("icon.market")) {
        bar(.47f,.20f,.06f,.55f); bar(.25f,.28f,.50f,.05f);
        line({x+s*.25f,y+s*.32f, x+s*.14f,y+s*.57f, x+s*.36f,y+s*.57f, x+s*.25f,y+s*.32f});
        line({x+s*.75f,y+s*.32f, x+s*.64f,y+s*.57f, x+s*.86f,y+s*.57f, x+s*.75f,y+s*.32f});
        bar(.33f,.76f,.34f,.06f);
    } else if (icon_key == ui_stable_key("icon.population")) {
        circle(.50f,.31f,.12f); circle(.25f,.42f,.09f); circle(.75f,.42f,.09f);
        bar(.37f,.49f,.26f,.25f); bar(.14f,.55f,.18f,.18f); bar(.68f,.55f,.18f,.18f);
    } else if (icon_key == ui_stable_key("icon.gdp")) {
        bar(.18f,.58f,.12f,.22f); bar(.36f,.45f,.12f,.35f); bar(.54f,.31f,.12f,.49f);
        line({x+s*.16f,y+s*.43f, x+s*.35f,y+s*.32f, x+s*.53f,y+s*.39f, x+s*.78f,y+s*.18f});
    } else if (icon_key == ui_stable_key("icon.calendar") || icon_key == ui_stable_key("icon.time")) {
        line({x+s*.18f,y+s*.28f, x+s*.82f,y+s*.28f, x+s*.82f,y+s*.80f,
              x+s*.18f,y+s*.80f, x+s*.18f,y+s*.28f});
        bar(.18f,.38f,.64f,.05f); bar(.30f,.18f,.06f,.20f); bar(.64f,.18f,.06f,.20f);
        bar(.31f,.50f,.10f,.09f); bar(.47f,.50f,.10f,.09f); bar(.63f,.50f,.10f,.09f);
    } else if (icon_key == ui_stable_key("icon.politics")) {
        line({x+s*.18f,y+s*.36f, x+s*.50f,y+s*.17f, x+s*.82f,y+s*.36f});
        bar(.18f,.38f,.64f,.06f); bar(.24f,.48f,.08f,.27f);
        bar(.46f,.48f,.08f,.27f); bar(.68f,.48f,.08f,.27f); bar(.16f,.76f,.68f,.07f);
    } else if (icon_key == ui_stable_key("icon.buildings")) {
        line({x+s*.16f,y+s*.80f, x+s*.16f,y+s*.44f, x+s*.36f,y+s*.56f,
              x+s*.36f,y+s*.38f, x+s*.58f,y+s*.52f, x+s*.58f,y+s*.30f,
              x+s*.82f,y+s*.30f, x+s*.82f,y+s*.80f, x+s*.16f,y+s*.80f});
        bar(.25f,.65f,.10f,.15f); bar(.47f,.65f,.10f,.15f); bar(.68f,.44f,.07f,.12f);
    } else if (icon_key == ui_stable_key("icon.technology")) {
        circle(.50f,.45f,.23f); circle(.50f,.45f,.08f); bar(.46f,.68f,.08f,.14f);
        bar(.34f,.80f,.32f,.05f); bar(.46f,.12f,.08f,.10f); bar(.18f,.42f,.10f,.06f);
        bar(.72f,.42f,.10f,.06f);
    } else if (icon_key == ui_stable_key("icon.military")) {
        line({x+s*.20f,y+s*.18f, x+s*.73f,y+s*.71f});
        line({x+s*.80f,y+s*.18f, x+s*.27f,y+s*.71f});
        bar(.17f,.68f,.22f,.06f); bar(.61f,.68f,.22f,.06f);
    } else if (icon_key == ui_stable_key("icon.diplomacy")) {
        circle(.38f,.48f,.22f); circle(.62f,.48f,.22f); bar(.32f,.74f,.36f,.06f);
    } else if (icon_key == ui_stable_key("icon.location")) {
        circle(.50f,.36f,.18f); circle(.50f,.36f,.05f);
        line({x+s*.34f,y+s*.43f, x+s*.50f,y+s*.82f, x+s*.66f,y+s*.43f});
    } else if (icon_key == ui_stable_key("icon.infrastructure")) {
        bar(.15f,.66f,.70f,.06f); bar(.23f,.48f,.06f,.23f); bar(.71f,.48f,.06f,.23f);
        line({x+s*.20f,y+s*.50f, x+s*.34f,y+s*.36f, x+s*.50f,y+s*.50f,
              x+s*.66f,y+s*.36f, x+s*.80f,y+s*.50f});
    } else if (icon_key == ui_stable_key("icon.journal")) {
        line({x+s*.18f,y+s*.22f, x+s*.47f,y+s*.27f, x+s*.47f,y+s*.80f,
              x+s*.18f,y+s*.73f, x+s*.18f,y+s*.22f});
        line({x+s*.82f,y+s*.22f, x+s*.53f,y+s*.27f, x+s*.53f,y+s*.80f,
              x+s*.82f,y+s*.73f, x+s*.82f,y+s*.22f});
    } else {
        line({x+s*.50f,y+s*.18f, x+s*.82f,y+s*.50f, x+s*.50f,y+s*.82f,
              x+s*.18f,y+s*.50f, x+s*.50f,y+s*.18f});
    }
}

void ScriptedGuiPainter::draw_tooltip(UiDrawList& draw_list,
                                      UiRect screen,
                                      UiRect anchor,
                                      const std::string& text) const {
    const auto& t = draw_list.theme();
    const auto split = text.find('|');
    const std::string title = split == std::string::npos ? text : text.substr(0, split);
    const std::string body = split == std::string::npos ? std::string{} : text.substr(split + 1u);
    const float width = 336.0f;
    const auto body_lines = wrap_tooltip_text(body, width - 24.0f, t.type.caption);
    const float height = body.empty() ? 50.0f : (body_lines.size() > 1u ? 94.0f : 76.0f);
    UiRect rect = place_tooltip(anchor, width, height, screen, 8.0f);
    if (anchor.y < screen.y + 96.0f) {
        rect.x = std::clamp(anchor.x, screen.x + 8.0f, screen.x + screen.w - width - 8.0f);
        rect.y = std::min(screen.y + screen.h - height - 8.0f, anchor.y + anchor.h + 8.0f);
    }
    draw_list.drop_shadow(rect, t.colors.shadow_floating, 7.0f, 2.0f, 4.0f, screen);
    draw_list.quad(rect, t.colors.border_dark, screen);
    draw_list.quad({rect.x+1,rect.y+1,rect.w-2,rect.h-2},t.colors.border_normal,screen);
    draw_list.quad_gradient({rect.x+3,rect.y+3,rect.w-6,rect.h-6},
                            ui_blend(t.colors.bg_floating,t.materials.wood_base,.16f),
                            t.colors.bg_deep, true, screen);
    draw_list.quad({rect.x+5,rect.y+4,rect.w-10,1}, t.materials.brass_highlight, screen);
    draw_list.quad({rect.x+9,rect.y+31,rect.w-18,1},0x69765f3bu,screen);
    draw_list.radial_disc(rect.x+12,rect.y+17,4,t.materials.brass_highlight,t.colors.border_dark,screen,16);
    const UiRect title_rect{rect.x+22,rect.y+7,rect.w-34,24};
    const float title_size = fit_text_size(title_rect,title,t.type.major_header,13.0f);
    draw_list.text(title,title_rect.x,rect.y+9,title_size,t.colors.text_gold,screen);
    for (std::size_t index = 0; index < body_lines.size(); ++index) {
        const UiRect line_rect{rect.x+12,rect.y+39+static_cast<float>(index)*18.0f,
                               rect.w-24,18};
        const float line_size=fit_text_size(line_rect,body_lines[index],t.type.caption,12.0f);
        draw_list.text(body_lines[index],line_rect.x,line_rect.y+3,line_size,
                       t.colors.text_secondary,screen);
    }
}

void ScriptedGuiPainter::paint_node(const ScriptedGuiRuntime& runtime,
                                    std::uint32_t node_index,
                                    UiDrawList& draw_list,
                                    UiRect rect,
                                    const ScriptedGuiTextResolver& resolve_text,
                                    std::uint16_t depth) const {
    if (depth > 128u || node_index >= runtime.nodes().size()) return;
    const auto& node = runtime.nodes()[node_index];
    if (!node.visible || rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto layout = style_for(node);
    rect.w = bounded(rect.w, layout.min_width, layout.max_width);
    rect.h = bounded(rect.h, layout.min_height, layout.max_height);
    const auto& t = draw_list.theme();
    const auto text = [&]() -> std::string {
        if (!node.text.empty()) return node.text;
        return node.text_key != 0 && resolve_text ? resolve_text(node.text_key) : std::string{};
    }();

    UiRect content = rect;
    switch (node.kind) {
    case UiWidgetKind::Panel: {
        switch (node.surface_style) {
        case UiSurfaceStyle::Wood:
            draw_list.wood_panel(rect, rect); content = inset(rect, theme_.padding); break;
        case UiSurfaceStyle::Parchment:
            draw_list.parchment_panel(rect, rect); content = inset(rect, theme_.padding); break;
        case UiSurfaceStyle::Leather:
            draw_list.leather_panel(rect, 0, rect); content = inset(rect, theme_.padding); break;
        case UiSurfaceStyle::Recessed:
            draw_list.quad(rect, t.colors.border_dark, rect);
            draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
                                    t.colors.bg_deep, t.colors.bg_panel_recessed, true, rect);
            content = inset(rect, theme_.padding); break;
        case UiSurfaceStyle::Hud:
            draw_list.quad(rect, t.colors.border_dark, rect);
            draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
                                    ui_blend(t.colors.bg_panel,t.materials.wood_base,.08f),
                                    t.colors.bg_deep, false, rect);
            draw_list.quad({rect.x+2,rect.y+2,1,rect.h-4}, 0x607f633du, rect);
            draw_list.quad({rect.x+rect.w-3,rect.y+2,2,rect.h-4}, 0x72000000u, rect);
            content = inset(rect, 12); break;
        case UiSurfaceStyle::Top: {
            const auto walnut_green = ui_blend(t.colors.bg_header, t.materials.wood_base, .42f);
            draw_list.quad(rect, t.colors.border_dark, rect);
            draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
                                    walnut_green, t.colors.bg_deep, true, rect);
            draw_list.quad({rect.x+2,rect.y+2,rect.w-4,1}, t.materials.brass_highlight, rect);
            // Broad, low-frequency polished wood variation; these are not
            // literal grain lines, only cabinet-like tonal movement.
            draw_list.quad_gradient({rect.x+rect.w*.16f,rect.y+4,rect.w*.20f,rect.h-9},
                                    0x102f170bu,0x00100000u,false,rect);
            draw_list.quad_gradient({rect.x+rect.w*.58f,rect.y+4,rect.w*.24f,rect.h-9},
                                    0x00100000u,0x122f170bu,false,rect);
            draw_list.quad({rect.x+2,rect.y+rect.h*.37f,rect.w-4,1}, 0x17100000u, rect);
            draw_list.quad({rect.x+2,rect.y+rect.h*.72f,rect.w-4,1}, 0x11100000u, rect);
            draw_list.quad({rect.x+2,rect.y+rect.h-4,rect.w-4,1}, t.colors.border_dark, rect);
            draw_list.quad({rect.x+2,rect.y+rect.h-3,rect.w-4,2}, t.materials.brass_rim, rect);
            content = {rect.x+5,rect.y+5,rect.w-10,rect.h-10}; break;
        }
        case UiSurfaceStyle::Dock:
            draw_list.quad(rect, t.colors.border_dark, rect);
            draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
                                    ui_blend(t.colors.bg_header,t.materials.wood_base,.32f),
                                    t.colors.bg_deep, false, rect);
            draw_list.quad_gradient({rect.x+3,rect.y+rect.h*.16f,rect.w-6,rect.h*.24f},
                                    0x162f170bu,0x00100000u,true,rect);
            draw_list.quad({rect.x+3,rect.y+2,rect.w-6,1},0x477f633du,rect);
            draw_list.quad({rect.x+rect.w-2,rect.y+2,1,rect.h-4}, t.colors.border_normal, rect);
            content = inset(rect, 7); break;
        case UiSurfaceStyle::Stat: {
            // Resource values live on the shared top instrument band. They
            // receive only a faint hover wash and engraved separators rather
            // than becoming individual cards.
            if (node.hovered)
                draw_list.quad_gradient({rect.x+3,rect.y+7,rect.w-6,rect.h-14},
                    t.colors.state_hover,0x00100000u,true,rect);
            draw_list.quad({rect.x+rect.w-2,rect.y+10,1,std::max(0.0f,rect.h-20)},0x760d0906u,rect);
            draw_list.quad({rect.x+rect.w-1,rect.y+12,1,std::max(0.0f,rect.h-24)},0x8aa77e42u,rect);
            draw_list.quad({rect.x+10,rect.y+4,rect.w-20,1},0x24c8a35fu,rect);
            content = inset(rect,9.0f); break;
        }
        case UiSurfaceStyle::Time: {
            auto top = ui_blend(t.colors.bg_header,t.materials.wood_base,.34f);
            if (node.hovered) top = ui_apply_overlay(top,t.colors.state_hover);
            draw_list.quad(rect,t.colors.border_dark,rect);
            draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},top,
                                    ui_blend(t.colors.bg_header,t.colors.bg_deep,.40f),true,rect);
            draw_list.quad({rect.x+4,rect.y+3,rect.w-8,1},t.materials.brass_highlight,rect);
            draw_list.quad({rect.x+1,rect.y+10,2,rect.h-20},t.colors.border_brass,rect);
            content = inset(rect,8.0f); break;
        }
        case UiSurfaceStyle::Country: {
            const auto top = ui_blend(t.colors.bg_header,t.materials.wood_base,.42f);
            draw_list.quad(rect,t.colors.border_brass,rect);
            draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},top,t.colors.bg_deep,true,rect);
            draw_list.quad({rect.x+3,rect.y+3,3,rect.h-6},t.colors.border_selected,rect);
            draw_list.quad({rect.x+9,rect.y+rect.h-3,rect.w-18,1},0x88c8a35fu,rect);
            content = inset(rect,9); break;
        }
        case UiSurfaceStyle::Section:
            draw_list.quad_gradient(rect,ui_blend(t.colors.bg_panel_raised,t.colors.bg_panel,.42f),
                                    ui_blend(t.colors.bg_panel_recessed,t.colors.bg_panel,.12f),true,rect);
            draw_list.quad({rect.x,rect.y,2,rect.h},0x426d5f42u,rect);
            draw_list.quad({rect.x+8,rect.y+rect.h-1,rect.w-16,1},0x586d5f42u,rect);
            content = inset(rect,10); break;
        case UiSurfaceStyle::Empty:
            draw_list.quad_gradient(rect,ui_blend(t.colors.bg_panel_recessed,t.colors.bg_panel,.20f),
                                    ui_blend(t.colors.bg_deep,t.materials.wood_base,.08f),true,rect);
            draw_list.quad({rect.x+24,rect.y+18,rect.w-48,1},0x557f633du,rect);
            draw_list.quad({rect.x+24,rect.y+rect.h-19,rect.w-48,1},0x557f633du,rect);
            content = inset(rect,18); break;
        case UiSurfaceStyle::Footer:
            draw_list.quad_gradient(rect,ui_blend(t.colors.bg_header,t.materials.wood_base,.20f),
                                    t.colors.bg_deep,true,rect);
            draw_list.quad({rect.x,rect.y,rect.w,1},t.colors.border_dark,rect);
            draw_list.quad({rect.x+8,rect.y+1,rect.w-16,1},t.colors.border_brass,rect);
            draw_list.quad({rect.x+10,rect.y+6,2,rect.h-12},0xa0a77e42u,rect);
            content = {rect.x+16,rect.y+7,rect.w-24,rect.h-14}; break;
        case UiSurfaceStyle::Utility: {
            // Stepped historical instrument mount. It deliberately avoids the
            // modern rounded-pill silhouette while retaining a floating map
            // control anchor and individually mounted medallions.
            draw_list.drop_shadow({rect.x+12,rect.y+9,rect.w-24,rect.h-14},
                                  t.colors.shadow_floating,6,1.5f,3.0f,rect);
            draw_list.quad({rect.x+18,rect.y+7,rect.w-36,rect.h-14},t.colors.border_dark,rect);
            draw_list.quad({rect.x+15,rect.y+12,rect.w-30,rect.h-24},
                           ui_blend(t.colors.bg_header,t.materials.brass_rim,.28f),rect);
            draw_list.quad_gradient({rect.x+18,rect.y+10,rect.w-36,rect.h-20},
                                    ui_blend(t.colors.bg_header,t.materials.wood_base,.38f),
                                    t.colors.bg_deep,true,rect);
            draw_list.quad({rect.x+25,rect.y+11,rect.w-50,1},t.materials.brass_highlight,rect);
            draw_list.quad({rect.x+8,rect.y+18,10,rect.h-36},
                           ui_blend(t.colors.bg_header,t.materials.brass_rim,.34f),rect);
            draw_list.quad({rect.x+rect.w-18,rect.y+18,10,rect.h-36},
                           ui_blend(t.colors.bg_header,t.materials.brass_rim,.34f),rect);
            draw_list.radial_disc(rect.x+12,rect.y+rect.h*.5f,5,t.materials.brass_highlight,t.colors.border_dark,rect,20);
            draw_list.radial_disc(rect.x+rect.w-12,rect.y+rect.h*.5f,5,t.materials.brass_highlight,t.colors.border_dark,rect,20);
            draw_list.quad({rect.x+rect.w*.5f-25,rect.y+rect.h-7,50,4},t.colors.border_normal,rect);
            content={rect.x+20,rect.y+7,rect.w-40,rect.h-14}; break;
        }
        case UiSurfaceStyle::Outliner:
            draw_list.drop_shadow(rect,t.colors.shadow_floating,4,1,2,rect);
            draw_list.quad_gradient(rect,ui_blend(t.colors.bg_deep,t.materials.wood_base,.18f),
                                    ui_blend(t.colors.bg_panel,t.colors.bg_deep,.44f),false,rect);
            draw_list.quad({rect.x,rect.y+8,3,rect.h-16},t.colors.border_dark,rect);
            draw_list.quad({rect.x+rect.w-3,rect.y+8,3,rect.h-16},t.materials.brass_rim,rect);
            content=inset(rect,9); break;
        case UiSurfaceStyle::OutlinerGroup:
            draw_list.quad_gradient({rect.x+4,rect.y+25,rect.w-8,rect.h-27},
                                    ui_blend(t.colors.bg_panel,t.materials.wood_base,.07f),
                                    ui_blend(t.colors.bg_deep,t.colors.bg_panel_recessed,.34f),true,rect);
            draw_list.quad({rect.x+8,rect.y+rect.h-1,rect.w-16,1},0x3b7f633du,rect);
            content=inset(rect,8); break;
        case UiSurfaceStyle::OutlinerRow:
            draw_list.quad_gradient({rect.x+2,rect.y+1,rect.w-4,rect.h-2},
                                    ui_blend(t.colors.bg_panel_raised,t.colors.bg_panel,.14f),
                                    ui_blend(t.colors.bg_panel_recessed,t.colors.bg_deep,.28f),true,rect);
            draw_list.quad({rect.x+6,rect.y+rect.h-1,rect.w-12,1},0x4f7f633du,rect);
            if(node.hovered) {
                draw_list.quad({rect.x+2,rect.y+6,2,rect.h-12},t.colors.border_selected,rect);
                draw_list.quad({rect.x+5,rect.y+2,rect.w-10,1},0x72c8a35fu,rect);
            } else draw_list.quad({rect.x+2,rect.y+7,2,rect.h-14},0x668d7448u,rect);
            content={rect.x+8,rect.y+5,rect.w-16,rect.h-10}; break;
        case UiSurfaceStyle::ModalBackdrop:
            draw_list.quad(rect, 0xb0120e0au, rect);
            draw_list.quad_gradient(rect, 0x32100806u, 0x58000000u, true, rect);
            content = rect;
            break;
        case UiSurfaceStyle::Modal:
            draw_list.modal_window(rect, text, {}, rect);
            draw_list.quad({rect.x+20,rect.y+51,rect.w-40,1},t.colors.border_dark,rect);
            draw_list.quad({rect.x+32,rect.y+52,rect.w-64,1},t.colors.border_brass,rect);
            content={rect.x+24,rect.y+64,std::max(0.0f,rect.w-48),std::max(0.0f,rect.h-86)};
            break;
        default:
            draw_list.panel(rect,theme_.panel,t.colors.border_dark,0x48000000u,2,rect);
            content = inset(rect,theme_.padding); break;
        }

        if (!text.empty() && node.surface_style != UiSurfaceStyle::Modal) {
            if (node.surface_style == UiSurfaceStyle::Section) {
                constexpr float header_h = 32;
                draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,header_h},
                                        ui_blend(t.colors.bg_header,t.materials.wood_base,.22f),
                                        t.colors.bg_panel_recessed,true,rect);
                const UiRect title_rect{rect.x+12,rect.y+7,rect.w-24,24};
                const float title_size=fit_text_size(title_rect,text,t.type.major_header,12.5f);
                draw_list.text(text,title_rect.x,rect.y+9,title_size,t.colors.text_secondary,rect);
                draw_list.quad({rect.x+10,rect.y+header_h-1,rect.w-20,1},t.colors.border_dark,rect);
                draw_list.quad({rect.x+16,rect.y+header_h,rect.w-32,1},t.colors.border_brass,rect);
                content.y=rect.y+header_h+9; content.h=std::max(0.0f,rect.h-header_h-18);
            } else if (node.surface_style == UiSurfaceStyle::OutlinerGroup) {
                constexpr float header_h=30;
                draw_list.quad_gradient({rect.x+5,rect.y+2,rect.w-10,header_h},
                                        ui_blend(t.colors.bg_header,t.materials.wood_base,.24f),
                                        t.colors.bg_deep,true,rect);
                draw_list.quad({rect.x+2,rect.y+8,3,header_h-12},t.colors.border_brass,rect);
                draw_list.quad({rect.x+11,rect.y+header_h+1,rect.w-22,1},t.materials.brass_rim,rect);
                const UiRect title_rect{rect.x+14,rect.y+6,rect.w-44,24};
                const float title_size=fit_text_size(title_rect,text,t.type.major_header,12.5f);
                draw_list.text(text,title_rect.x,rect.y+8,title_size,t.colors.text_gold,rect);
                const std::array<float,6> fold{{rect.x+rect.w-19,rect.y+13,
                                                rect.x+rect.w-15,rect.y+17,
                                                rect.x+rect.w-11,rect.y+13}};
                draw_list.polyline(fold,t.colors.text_muted,rect);
                content.y=rect.y+header_h+7; content.h=std::max(0.0f,rect.h-header_h-14);
            } else if (is_hud_panel(node.surface_style)) {
                constexpr float header_h = 44;
                draw_list.quad_gradient({rect.x+7,rect.y+1,rect.w-14,header_h},
                                        ui_blend(t.colors.bg_header,t.materials.wood_base,.27f),
                                        t.colors.bg_deep,true,rect);
                draw_list.quad_gradient({rect.x+2,rect.y+7,5,header_h-12},t.colors.bg_header,t.colors.bg_deep,true,rect);
                draw_list.quad_gradient({rect.x+rect.w-7,rect.y+7,5,header_h-12},t.colors.bg_header,t.colors.bg_deep,true,rect);
                draw_list.quad({rect.x+9,rect.y+1,rect.w-18,1},0x5ec8a35fu,rect);
                draw_list.quad({rect.x+7,rect.y+header_h-1,rect.w-14,1},t.colors.border_dark,rect);
                draw_list.quad({rect.x+14,rect.y+header_h,rect.w-28,2},t.colors.border_brass,rect);
                draw_list.radial_disc(rect.x+10,rect.y+22,3,t.materials.brass_highlight,t.colors.border_dark,rect,16);
                draw_list.radial_disc(rect.x+rect.w-10,rect.y+22,3,t.materials.brass_highlight,t.colors.border_dark,rect,16);
                const float reserve=node.surface_style==UiSurfaceStyle::Outliner?82.0f:36.0f;
                const UiRect title_rect{rect.x+18,rect.y+8,rect.w-reserve,30};
                const float title_size=fit_text_size(title_rect,text,t.type.window_title,13.5f);
                draw_list.text(text,title_rect.x,rect.y+12,title_size,t.colors.text_gold,rect);
                if(node.surface_style==UiSurfaceStyle::Outliner){
                    // Functional system header: collapse affordance and a
                    // recessed filter/settings fitting, without adding cards.
                    const std::array<float,6> collapse{{rect.x+rect.w-53,rect.y+18,
                                                       rect.x+rect.w-48,rect.y+23,
                                                       rect.x+rect.w-43,rect.y+18}};
                    draw_list.polyline(collapse,t.colors.text_secondary,rect);
                    draw_list.radial_disc(rect.x+rect.w-25,rect.y+22,10,
                                          t.colors.bg_panel_recessed,t.materials.brass_rim,rect,24);
                    draw_list.radial_disc(rect.x+rect.w-25,rect.y+22,3,
                                          t.materials.brass_highlight,t.colors.bg_deep,rect,16);
                }
                content.y=rect.y+header_h+10; content.h=std::max(0.0f,rect.h-header_h-20);
            } else {
                const float header_h=theme_.header_font_size+14;
                draw_list.ornate_header({rect.x+2,rect.y+2,rect.w-4,header_h},text,rect);
                content.y+=header_h+4; content.h=std::max(0.0f,content.h-header_h-4);
            }
        }
        if (node.command_key != 0 || node.tooltip_key != 0) draw_list.hit(node.instance_key,rect);
        break;
    }
    case UiWidgetKind::Button: {
        const bool nav=node.surface_style==UiSurfaceStyle::Nav;
        const bool section=node.surface_style==UiSurfaceStyle::Section;
        const bool time=node.surface_style==UiSurfaceStyle::Time;
        const bool tab=node.surface_style==UiSurfaceStyle::Tab;
        const bool menu=node.surface_style==UiSurfaceStyle::MenuRow;
        const bool tool_primary=node.surface_style==UiSurfaceStyle::ToolPrimary;
        const bool tool_secondary=node.surface_style==UiSurfaceStyle::ToolSecondary;
        const bool tool_utility=node.surface_style==UiSurfaceStyle::ToolUtility;
        const bool medallion=node.surface_style==UiSurfaceStyle::Medallion ||
                              tool_primary || tool_secondary || tool_utility;
        const auto state=control_state(node);
        const float press_shift=state.enabled?std::clamp(state.press_mix,0.0f,1.0f)*1.6f:0.0f;
        UiRect control_rect=rect;
        if(tool_secondary) control_rect=inset(rect,1.5f);
        if(tool_utility) control_rect=inset(rect,4.0f);
        if(medallion){
            draw_list.medallion_button(control_rect,state,tool_primary,rect);
        }else if(tab){
            draw_list.tab(rect,text,node.selected,node.hovered,rect);
            if(node.pressed) draw_list.quad(inset(rect,2),t.colors.state_pressed,rect);
            if(node.focused) draw_list.quad({rect.x+7,rect.y+2,rect.w-14,1},t.colors.state_focus,rect);
        }else if(menu){
            draw_list.category_row(rect,state,rect);
        }else if(nav){
            draw_list.category_row(rect,state,rect);
        }else if(section){
            draw_list.mechanical_button(rect,state,rect);
        }else if(time){
            draw_list.mechanical_button(rect,state,rect);
        }else{
            draw_list.mechanical_button(rect,state,rect);
        }
        const float emphasis=std::clamp(std::max(node.selected?1.0f:node.selected_mix,
                                                 node.hover_mix*.72f),0.0f,1.0f);
        const auto icon_color=!node.enabled?t.colors.text_disabled:
            ui_blend(t.colors.text_secondary,t.colors.text_gold,emphasis);
        if(node.icon_key!=0 && !tab){
            const float icon_size=tool_primary?32.0f:medallion?29.0f:menu?28.0f:(nav?32.0f:24.0f);
            const float icon_x=(medallion||(nav&&text.empty()))?rect.x+(rect.w-icon_size)*.5f:rect.x+12;
            const float icon_y=rect.y+(rect.h-icon_size)*.5f+press_shift;
            // Engraved two-pass icon: dark intaglio offset plus warm metal face.
            draw_icon(draw_list,node.icon_key,{icon_x+1.0f,icon_y+1.2f,icon_size,icon_size},
                      0xa0080604u,rect);
            draw_icon(draw_list,node.icon_key,{icon_x,icon_y,icon_size,icon_size},icon_color,rect);
        }
        if(!text.empty() && !tab){
            const auto label_color=!node.enabled?t.colors.text_disabled:
                ui_blend(t.colors.text_primary,t.colors.text_gold,emphasis*.72f);
            float tx=rect.x+theme_.padding+(node.icon_key!=0?(menu?40.0f:30.0f):0.0f);
            const float right_padding=menu?27.0f:8.0f;
            UiRect label_rect{tx,rect.y,std::max(1.0f,rect.x+rect.w-right_padding-tx),rect.h};
            float label_size=fit_text_size(label_rect,text,time?t.type.body:theme_.font_size,12.0f);
            if(time) {
                tx=centered_text_x({rect.x+4,rect.y,rect.w-8,rect.h},text,label_size);
                label_rect.x=tx;
            }
            draw_list.text(text,tx,rect.y+(rect.h-label_size)*.5f+press_shift,
                           label_size,label_color,rect);
            if(menu) {
                const float ax=rect.x+rect.w-17.0f;
                const float ay=rect.y+rect.h*.5f+press_shift;
                const std::array<float,6> arrow{{ax-3,ay-4,ax+1,ay,ax-3,ay+4}};
                draw_list.polyline(arrow,ui_blend(t.colors.text_muted,t.colors.text_gold,emphasis),rect);
            }
        }
        if(node.enabled) draw_list.hit(node.instance_key,rect);
        content=inset(rect,theme_.padding); break;
    }
    case UiWidgetKind::Label:{
        float size=theme_.font_size; auto color=node.enabled?theme_.text:theme_.text_muted;
        if(node.surface_style==UiSurfaceStyle::Primary){size=t.type.stat_value;color=t.colors.text_primary;}
        else if(node.surface_style==UiSurfaceStyle::Secondary){size=t.type.secondary;color=t.colors.text_muted;}
        else if(node.surface_style==UiSurfaceStyle::Positive){size=t.type.stat_value;color=t.colors.text_positive;}
        else if(node.surface_style==UiSurfaceStyle::Section){size=t.type.major_header;color=t.colors.text_secondary;}
        else if(node.surface_style==UiSurfaceStyle::Center){size=t.type.stat_value;color=t.colors.text_primary;}
        else if(node.surface_style==UiSurfaceStyle::CenterMuted){size=t.type.secondary;color=t.colors.text_muted;}
        if(!text.empty()) {
            const bool centered=node.surface_style==UiSurfaceStyle::Center ||
                                node.surface_style==UiSurfaceStyle::CenterMuted;
            size=fit_text_size(rect,text,size,12.0f,centered?4.0f:1.0f);
            draw_list.text(text,centered?centered_text_x(rect,text,size):rect.x,
                           rect.y+std::max(0.0f,(rect.h-size)*.5f),size,color,rect);
        }
        break;
    }
    case UiWidgetKind::Image:{
        const bool empty=node.surface_style==UiSurfaceStyle::Empty;
        const bool medallion=node.surface_style==UiSurfaceStyle::Medallion;
        const bool icon_inset=node.surface_style==UiSurfaceStyle::IconInset;
        if(medallion){
            draw_list.medallion_button(rect,false,false,node.enabled,rect);
        }else if(icon_inset){
            const float radius=std::max(6.0f,std::min(rect.w,rect.h)*.5f-2.0f);
            const float cx=rect.x+rect.w*.5f,cy=rect.y+rect.h*.5f;
            draw_list.radial_disc(cx,cy+1,radius,t.colors.bg_deep,t.materials.brass_rim,rect,28);
            draw_list.radial_disc(cx,cy,radius-3,
                                  ui_blend(t.colors.bg_panel_recessed,t.materials.wood_base,.12f),
                                  t.colors.border_dark,rect,28);
        }else if(!empty){
            draw_list.quad(rect,t.colors.border_dark,rect);
            draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
                                    ui_blend(t.colors.bg_panel_raised,t.colors.brass,.08f),
                                    t.colors.bg_deep,true,rect);
        }
        const auto icon_rect=inset(rect,empty?3.0f:(medallion?8.0f:(icon_inset?9.0f:6.0f)));
        draw_icon(draw_list,node.icon_key,{icon_rect.x+1,icon_rect.y+1,icon_rect.w,icon_rect.h},
                  0x9a080604u,rect);
        draw_icon(draw_list,node.icon_key,icon_rect,
                  empty?t.colors.text_muted:t.colors.text_gold,rect); break;
    }
    case UiWidgetKind::Module:{
        // Modules are independent render surfaces. The declarative layout
        // provides their frame, content rectangle and draw order.
        draw_list.quad(rect,t.colors.border_dark,rect);
        draw_list.quad_gradient({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
                                ui_blend(t.colors.bg_panel_raised,t.colors.brass,.08f),
                                t.colors.bg_deep,true,rect);
        draw_list.module(node.module_key,inset(rect,1.0f),inset(rect,1.0f));
        break;
    }
    case UiWidgetKind::Progress:
        draw_list.progress_bar(rect,static_cast<float>(std::clamp(node.value,0.0,1.0)),
                               theme_.accent,theme_.panel_dark,rect); break;
    case UiWidgetKind::Chart:
        if(!node.chart_values.empty()) draw_list.ink_chart(rect,node.chart_values,theme_.accent,theme_.chart_fill,rect);
        break;
    default: break;
    }

    if(node.hovered&&node.tooltip_key!=0) tooltip_=TooltipCandidate{node.tooltip_key,rect};
    if(node.first_child!=kInvalidUiRuntimeNode)
        paint_children(runtime,node,draw_list,content,resolve_text,depth);
}

} // namespace thunder
