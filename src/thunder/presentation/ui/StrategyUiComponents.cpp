#include "thunder/presentation/ui/StrategyUi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace thunder {

void UiDrawList::wood_panel(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& m = theme().materials;

    // 1. Ambient soft drop shadow
    drop_shadow(rect, theme().colors.shadow_floating, 5.0f, 2.0f, 3.0f, scissor);

    // 2. Dark umber outer bevel
    quad(rect, m.wood_bevel, scissor);

    // 3. Polished wood base with a smooth top-lit gradient
    if (rect.w > 4.0f && rect.h > 4.0f) {
        quad_gradient({rect.x + 1.5f, rect.y + 1.5f, rect.w - 3.0f, rect.h - 3.0f},
                      ui_blend(m.wood_base, 0xffffffffu, 0.07f),
                      ui_blend(m.wood_base, 0xff000000u, 0.22f), true, scissor);
    }

    // 4. Inlaid gold wire filigree inner frame
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 3.5f, rect.y + 3.5f, rect.w - 7.0f, rect.h - 7.0f}, m.wood_filigree, scissor);

    // 5. Deep satin lacquered thunder
    if (rect.w > 10.0f && rect.h > 10.0f)
        quad({rect.x + 4.5f, rect.y + 4.5f, rect.w - 9.0f, rect.h - 9.0f}, m.wood_thunder, scissor);

    // 6. Corner brass rosettes with steel thunder screws
    if (rect.w > 24.0f && rect.h > 24.0f) {
        const float corners[4][2] = {
            {rect.x + 6.0f, rect.y + 6.0f},
            {rect.x + rect.w - 11.0f, rect.y + 6.0f},
            {rect.x + 6.0f, rect.y + rect.h - 11.0f},
            {rect.x + rect.w - 11.0f, rect.y + rect.h - 11.0f}
        };
        for (const auto& c : corners) {
            quad({c[0], c[1], 5.0f, 5.0f}, m.wood_rosette, scissor);
            quad({c[0] + 1.5f, c[1] + 1.5f, 2.0f, 2.0f}, m.wood_screw, scissor);
        }
    }
}

// ── Aged Vellum Parchment Material ──
void UiDrawList::parchment_panel(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& m = theme().materials;

    // 1. Soft sepia contact shadow
    drop_shadow(rect, m.parchment_shadow, 3.0f, 1.0f, 2.0f, scissor);

    // 2. Aged walnut vignette margin
    quad(rect, m.parchment_margin, scissor);

    // 3. Antique vellum base with a gentle vertical tone ramp
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad_gradient({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f},
                      m.parchment_base, ui_blend(m.parchment_base, 0x00000000u, 0.05f), true, scissor);

    // 4. Subtle calligraphic double pinstripe
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, rect.h - 6.0f}, m.parchment_pinstripe, scissor);

    // 5. Radiant interior sheet
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f, rect.h - 8.0f}, m.parchment_sheet, scissor);
}

// ── Morocco Leather Material ──
void UiDrawList::leather_panel(UiRect rect, std::uint32_t leather_color, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const auto leather = leather_color != 0 ? leather_color : t.materials.leather_base;

    // 1. Deep recessed soft drop shadow
    drop_shadow(rect, t.colors.shadow_raised, 4.0f, 1.5f, 2.5f, scissor);

    // 2. Outer umber bevel frame
    quad(rect, t.materials.wood_bevel, scissor);

    // 3. Morocco base with smooth vertical shading
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad_gradient({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f},
                      ui_blend(leather, 0xffffffffu, 0.05f),
                      ui_blend(leather, 0xff000000u, 0.16f), true, scissor);

    // 4. Gold-leaf hot stamped fillet line
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, rect.h - 6.0f}, t.materials.leather_fillet, scissor);

    // 5. Embossed thunder pebble layer
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f, rect.h - 8.0f}, leather, scissor);
}

// ── Brushed Brass Button ──
void UiDrawList::brass_button(UiRect rect, const std::string& label, bool pressed, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const auto& m = t.materials;

    // Soft contact shadow (suppressed while pressed: the button sits flush)
    if (!pressed) drop_shadow(rect, t.colors.shadow_raised, 2.5f, 1.0f, 1.5f, scissor);

    // Dark brass rim
    quad(rect, pressed ? m.brass_rim_pressed : m.brass_rim, scissor);

    // Brushed face with a smooth specular ramp
    if (rect.w > 3.0f && rect.h > 3.0f)
        quad_gradient({rect.x + 1.5f, rect.y + 1.5f, rect.w - 3.0f, rect.h - 3.0f},
                      pressed ? m.brass_face_pressed : m.brass_highlight,
                      pressed ? ui_blend(m.brass_face_pressed, 0xff000000u, 0.18f) : m.brass_face,
                      true, scissor);

    // Top highlight rim
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, 1.0f},
             pressed ? m.brass_highlight_pressed : 0x60ffffffu, scissor);

    if (!label.empty()) {
        const float label_w = ui_estimate_text_width(label, t.type.body);
        const bool can_center = (rect.w > label_w + 16.0f);
        const float press_offset = pressed ? 1.0f : 0.0f;
        const float text_y = rect.y + rect.h * 0.5f - 6.0f + press_offset;
        if (can_center) {
            const float center_x = rect.x + rect.w * 0.5f;
            // Physical intaglio: recessed light catch below the dark engraved text
            text(label, center_x + 0.5f, text_y + 0.5f, t.type.body, 0x60ffffffu, scissor, true);
            text(label, center_x, text_y, t.type.body, t.materials.parchment_text, scissor, true);
        } else {
            const float text_x = rect.x + 8.0f;
            text(label, text_x + 0.5f, text_y + 0.5f, t.type.body, 0x60ffffffu, scissor, false);
            text(label, text_x, text_y, t.type.body, t.materials.parchment_text, scissor, false);
        }
    }
}

void UiDrawList::medallion_button(UiRect rect, bool selected, bool hovered,
                                  bool enabled, UiRect scissor) {
    UiControlVisualState state{};
    state.enabled = enabled;
    state.selected = selected;
    state.hovered = hovered;
    state.hover_mix = hovered ? 1.0f : 0.0f;
    state.selected_mix = selected ? 1.0f : 0.0f;
    medallion_button(rect, state, false, scissor);
}

void UiDrawList::medallion_button(UiRect rect, const UiControlVisualState& state,
                                  bool primary, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float hover = state.enabled ? std::clamp(state.hover_mix, 0.0f, 1.0f) : 0.0f;
    const float press = state.enabled ? std::clamp(state.press_mix, 0.0f, 1.0f) : 0.0f;
    const float selected = state.enabled ? std::max(state.selected ? 1.0f : 0.0f,
                                                     std::clamp(state.selected_mix, 0.0f, 1.0f)) : 0.0f;
    const float focus = state.enabled ? std::clamp(state.focus_mix, 0.0f, 1.0f) : 0.0f;
    const float radius = std::max(3.0f, std::min(rect.w, rect.h) * 0.5f -
                                  (primary ? 1.0f : 2.0f) - press * .7f);
    const float cx = rect.x + rect.w * 0.5f;
    const float cy = rect.y + rect.h * 0.5f + press * 1.6f - hover * .35f;

    if (press < .92f)
        radial_disc(cx + 1.5f, cy + 2.6f, radius + 2.0f,
                    ui_blend(t.colors.shadow_raised, t.colors.shadow_floating, .45f),
                    t.colors.shadow_floating, scissor, 40);
    if (selected > .01f || focus > .01f)
        radial_disc(cx, cy, radius + 2.2f,
                    ui_blend(0x00ffffffu, t.colors.border_selected,
                             .22f * selected + .12f * focus),
                    ui_blend(0x00000000u, t.materials.brass_highlight,
                             .38f * selected + .18f * focus), scissor, 40);

    const auto rim_center = ui_blend(t.materials.brass_rim, t.materials.brass_highlight,
                                     .24f * hover + .42f * selected);
    const auto rim_edge = ui_blend(t.colors.border_dark, t.materials.brass_rim,
                                   .16f + .28f * hover + .30f * selected);
    radial_disc(cx, cy, radius, rim_center, rim_edge, scissor, 40);
    radial_disc(cx, cy + .7f, radius - 2.2f,
                ui_blend(t.materials.brass_face, t.materials.brass_rim_pressed, press * .72f),
                t.colors.border_dark, scissor, 40);
    radial_disc(cx, cy + 1.0f, radius - 4.4f, t.colors.bg_deep,
                t.materials.wood_bevel, scissor, 40);

    auto face_center = ui_blend(t.colors.bg_panel_raised, t.materials.wood_base,
                                .14f + .16f * selected + .10f * hover);
    face_center = ui_blend(face_center, t.colors.bg_deep, .28f * press);
    const auto face_edge = ui_blend(t.colors.bg_panel_recessed, t.colors.bg_deep,
                                    .50f + .20f * press);
    radial_disc(cx, cy, radius - 6.3f, face_center, face_edge, scissor, 40);
    if (radius > 12.0f) {
        quad({cx-radius*.48f,cy-radius*.55f,radius*.96f,1.15f},
             ui_blend(0x18ffffffu,t.materials.brass_highlight,.38f+.30f*hover),scissor);
        quad({cx-radius*.43f,cy+radius*.48f,radius*.86f,1.2f},
             ui_blend(0x42000000u,t.colors.border_dark,.55f),scissor);
    }
    if (selected > .01f) {
        radial_disc(cx, cy, radius - 8.8f, 0x00ffffffu,
                    ui_blend(t.materials.brass_rim,t.colors.border_selected,.64f),scissor,40);
        radial_disc(cx, cy, radius - 10.4f, face_center, face_edge, scissor,40);
        const float marker_alpha = .55f + .40f * selected;
        quad({rect.x+1.0f,cy-4.0f,3.0f,8.0f},
             ui_blend(0x00000000u,t.colors.border_selected,marker_alpha),scissor);
        quad({rect.x+rect.w-4.0f,cy-4.0f,3.0f,8.0f},
             ui_blend(0x00000000u,t.colors.border_selected,marker_alpha),scissor);
    }
    if (!state.enabled)
        radial_disc(cx, cy, radius - 4.0f,
                    ui_blend(t.colors.bg_panel_recessed,t.colors.text_disabled,.12f),
                    t.colors.state_disabled, scissor,40);
}

void UiDrawList::mechanical_button(UiRect rect, const UiControlVisualState& state,
                                   UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float h = state.enabled ? std::clamp(state.hover_mix,0.0f,1.0f) : 0.0f;
    const float p = state.enabled ? std::clamp(state.press_mix,0.0f,1.0f) : 0.0f;
    const float s = state.enabled ? std::max(state.selected?1.0f:0.0f,state.selected_mix) : 0.0f;
    UiRect face{rect.x+2.0f,rect.y+1.0f+p,rect.w-4.0f,rect.h-3.0f-p};
    if (p < .85f) drop_shadow(face,t.colors.shadow_raised,2.5f,1.0f,1.5f,scissor);
    quad(rect,t.colors.border_dark,scissor);
    quad({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
         ui_blend(t.materials.brass_rim,t.colors.border_selected,.28f*h+.46f*s),scissor);
    quad_gradient(face,
        ui_blend(t.colors.bg_panel_raised,t.materials.wood_base,.14f+.12f*h+.16f*s),
        ui_blend(t.colors.bg_panel_recessed,t.colors.bg_deep,.28f+.25f*p),true,scissor);
    if (s>.01f)
        quad_gradient({face.x+3,face.y+3,face.w-6,face.h-6},
                      ui_blend(0x00000000u,t.colors.emerald_dark,.34f*s),
                      ui_blend(0x00000000u,t.colors.bg_deep,.18f*s),true,scissor);
    if (p>.01f)
        quad({face.x+2,face.y+2,face.w-4,face.h-4},
             ui_blend(0x00000000u,0x78000000u,p),scissor);
    quad({face.x+2,face.y+2,face.w-4,1},
         ui_blend(0x12000000u,t.materials.brass_highlight,
                  (.18f+.35f*h+.28f*s)*(1.0f-.70f*p)),scissor);
    quad({face.x+3,face.y+face.h-2,face.w-6,1},0x72000000u,scissor);
    if (s>.01f) {
        quad({rect.x+3,rect.y+4,3,rect.h-8},t.colors.border_selected,scissor);
        quad({rect.x+9,rect.y+rect.h-4,rect.w-18,2},t.materials.brass_highlight,scissor);
    }
    if (state.focused) {
        quad({rect.x+6,rect.y+2,rect.w-12,1},t.colors.state_focus,scissor);
        quad({rect.x+6,rect.y+rect.h-3,rect.w-12,1},t.colors.state_focus,scissor);
    }
    if (!state.enabled) quad(face,t.colors.state_disabled,scissor);
}

void UiDrawList::category_row(UiRect rect, const UiControlVisualState& state,
                              UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float h = state.enabled ? std::clamp(state.hover_mix,0.0f,1.0f) : 0.0f;
    const float p = state.enabled ? std::clamp(state.press_mix,0.0f,1.0f) : 0.0f;
    const float s = state.enabled ? std::max(state.selected?1.0f:0.0f,state.selected_mix) : 0.0f;
    const auto top = ui_blend(t.colors.bg_panel,t.colors.bg_panel_raised,.12f+.20f*h+.22f*s);
    const auto bottom = ui_blend(t.colors.bg_panel_recessed,t.colors.bg_deep,.18f+.18f*p);
    quad_gradient({rect.x+2,rect.y+1+p,rect.w-4,rect.h-3},top,bottom,true,scissor);
    if (s>.01f)
        quad_gradient({rect.x+5,rect.y+3+p,rect.w-10,rect.h-7},
                      ui_blend(0x00000000u,t.colors.emerald_dark,.32f*s),
                      ui_blend(0x00000000u,t.colors.bg_deep,.13f*s),true,scissor);
    quad({rect.x+14,rect.y+rect.h-1,rect.w-28,1},
         ui_blend(0x1f7f633du,t.colors.border_normal,.18f*h),scissor);
    if (h>.01f || s>.01f) {
        quad({rect.x+2,rect.y+6,s>.01f?4.0f:2.0f,rect.h-12},
             ui_blend(t.colors.border_normal,t.colors.border_selected,.48f*h+.72f*s),scissor);
        quad({rect.x+5,rect.y+3,rect.w-10,1},
             ui_blend(0x00ffffffu,t.materials.brass_highlight,.22f*h+.22f*s),scissor);
    }
    if(s>.01f)
        quad({rect.x+10,rect.y+rect.h-3,rect.w-20,2},t.materials.brass_rim,scissor);
    if (p>.01f)
        quad({rect.x+4,rect.y+3,rect.w-8,rect.h-6},
             ui_blend(0x00000000u,t.colors.state_pressed,p),scissor);
    if (!state.enabled) quad(rect,t.colors.state_disabled,scissor);
}

// ── Wax Seal with Draped Silk Ribbons ──
void UiDrawList::wax_seal(float cx, float cy, float radius, UiRect scissor) {
    if (radius <= 0.0f) return;
    const auto& m = theme().materials;

    // Draped silk ribbons
    if (radius >= 12.0f) {
        quad({cx - 6.0f, cy + radius * 0.4f, 5.0f, radius * 1.3f}, m.wax_base, scissor);
        quad({cx + 1.0f, cy + radius * 0.4f, 5.0f, radius * 1.4f}, m.wax_face, scissor);
    }

    // Outer melted puddle contour
    quad({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, m.wax_base, scissor);

    // Inner indented seal face
    if (radius > 3.0f)
        quad({cx - radius + 2.5f, cy - radius + 2.5f, (radius - 2.5f) * 2.0f, (radius - 2.5f) * 2.0f}, m.wax_face, scissor);

    // Embossed crest
    if (radius > 8.0f) {
        quad({cx - 3.0f, cy - 3.0f, 6.0f, 6.0f}, m.wax_crest, scissor);
        quad({cx - 1.0f, cy - 1.0f, 2.0f, 2.0f}, m.wax_crest_thunder, scissor);
    }
}
} // namespace thunder
