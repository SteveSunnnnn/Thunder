#pragma once

#include "thunder/presentation/ui/ScriptedGuiRuntime.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include "thunder/presentation/ui/UiTheme.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace thunder {

// Presentation palette used by the generic scripted GUI painter. The
// defaults mirror the engine's Victorian house theme; construct from
// make_scripted_gui_paint_theme(UiTheme::victorian()) or a custom UiTheme.
// Games may override any field; Thunder owns no era or title.
struct ScriptedGuiPaintTheme {
    std::uint32_t panel = 0xf2263029u;
    std::uint32_t panel_raised = 0xf2313d34u;
    std::uint32_t panel_dark = 0xf0192019u;
    std::uint32_t panel_header = 0xff201a12u;
    std::uint32_t border = 0xff6d5f42u;
    std::uint32_t border_light = 0xff9b8a63u;
    std::uint32_t border_gold = 0xffd4af37u;
    std::uint32_t border_selected = 0xffe6c86au;
    std::uint32_t accent = 0xffd4af37u;
    std::uint32_t emerald = 0xff3f8060u;
    std::uint32_t text = 0xffede4cdu;
    std::uint32_t text_muted = 0xff97907du;
    std::uint32_t text_gold = 0xffe6c86au;
    std::uint32_t positive = 0xff7fbf8eu;
    std::uint32_t negative = 0xffcf7a68u;
    std::uint32_t hover_overlay = 0x22ffffffu;
    std::uint32_t selected_fill = 0xffe6c86au;
    std::uint32_t chart_fill = 0x403f8060u;
    float font_size = 13.0f;
    float header_font_size = 16.0f;
    float padding = 8.0f;
    float gap = 6.0f;
};

// Maps a full UiTheme token set onto the painter palette.
[[nodiscard]] ScriptedGuiPaintTheme make_scripted_gui_paint_theme(const UiTheme& theme) noexcept;

using ScriptedGuiTextResolver = std::function<std::string(UiStableKey)>;

// Converts a refreshed retained ScriptedGuiRuntime into backend-neutral
// UiDrawList geometry. Layout and labels come from the compiled content tree;
// only reusable layout/render algorithms live in Thunder.
class ScriptedGuiPainter {
public:
    explicit ScriptedGuiPainter(const ScriptedGuiBlueprint& blueprint,
                                ScriptedGuiPaintTheme theme = {});

    void paint(const ScriptedGuiRuntime& runtime,
               UiDrawList& draw_list,
               UiRect screen,
               const ScriptedGuiTextResolver& resolve_text) const;

private:
    struct LayoutStyle {
        float width = -1.0f;
        float height = -1.0f;
        float min_width = 0.0f;
        float min_height = 0.0f;
        float max_width = 1'000'000.0f;
        float max_height = 1'000'000.0f;
        float grow = 0.0f;
        float gap = -1.0f;
    };

    [[nodiscard]] LayoutStyle style_for(const UiRuntimeNode& node) const noexcept;
    void paint_node(const ScriptedGuiRuntime& runtime,
                    std::uint32_t node_index,
                    UiDrawList& draw_list,
                    UiRect rect,
                    const ScriptedGuiTextResolver& resolve_text,
                    std::uint16_t depth) const;
    void paint_children(const ScriptedGuiRuntime& runtime,
                        const UiRuntimeNode& node,
                        UiDrawList& draw_list,
                        UiRect content,
                        const ScriptedGuiTextResolver& resolve_text,
                        std::uint16_t depth) const;
    void draw_icon(UiDrawList& draw_list,
                   UiStableKey icon_key,
                   UiRect rect,
                   std::uint32_t color,
                   UiRect scissor) const;
    void draw_tooltip(UiDrawList& draw_list,
                      UiRect screen,
                      UiRect anchor,
                      const std::string& text) const;

    struct TooltipCandidate {
        UiStableKey key = 0;
        UiRect anchor{};
    };

    const ScriptedGuiBlueprint* blueprint_ = nullptr;
    ScriptedGuiPaintTheme theme_{};
    mutable std::optional<TooltipCandidate> tooltip_;
};

} // namespace thunder
