#pragma once

#include <cstdint>
#include <string>

namespace thunder {

// Central design-token system for the engine default UI.
//
// The engine ships one visual language: a nineteenth-century grand-strategy
// look (deep desaturated greens, aged brass, warm ivory text, parchment and
// wood materials). Every drawing primitive in UiDrawList, the scripted GUI
// painter and the tooltip stack read from this structure, so business code
// never sets colors, borders or font sizes itself. Games may supply an
// altered theme; the token roles stay generic.
//
// Color packing is 0xAARRGGBB.

struct UiThemeColors {
    // Backgrounds / depth
    std::uint32_t bg_base = 0xff1a1711u;             // warm charcoal beneath the cabinet
    std::uint32_t bg_deep = 0xff100d09u;             // dark wood recesses and grooves
    std::uint32_t bg_panel = 0xf23a3829u;            // warm administrative olive
    std::uint32_t bg_panel_raised = 0xf2494531u;     // parchment-influenced raised plane
    std::uint32_t bg_panel_recessed = 0xf0242117u;   // ledger recess
    std::uint32_t bg_header = 0xff2e190eu;           // stained walnut titleplate
    std::uint32_t bg_floating = 0xfa393124u;         // warm archival popup
    std::uint32_t bg_modal = 0xf52b2419u;            // leather-toned modal backdrop

    // Borders
    std::uint32_t border_dark = 0xff0d0906u;
    std::uint32_t border_normal = 0xff706049u;       // dark aged bronze edge
    std::uint32_t border_light = 0xff8f7a5au;
    std::uint32_t border_gold = 0xff9d8355u;         // muted antique gold line
    std::uint32_t border_brass = 0xff846a47u;
    std::uint32_t border_selected = 0xffb49a68u;

    // Accents
    std::uint32_t emerald = 0xff617453u;
    std::uint32_t emerald_dark = 0xff3d4d36u;
    std::uint32_t emerald_light = 0xff829071u;
    std::uint32_t gold = 0xffa08758u;
    std::uint32_t brass = 0xff826a49u;
    std::uint32_t burgundy = 0xff5a2328u;
    std::uint32_t steel = 0xff827a68u;              // neutral oxidized support metal

    // Text
    std::uint32_t text_primary = 0xfff0e3c5u;        // warm ledger ivory
    std::uint32_t text_secondary = 0xffd0bd97u;
    std::uint32_t text_muted = 0xffa6987cu;
    std::uint32_t text_disabled = 0xff746b58u;
    std::uint32_t text_gold = 0xffc5ae81u;
    std::uint32_t text_positive = 0xff8eae78u;
    std::uint32_t text_negative = 0xffcf7a68u;
    std::uint32_t text_warning = 0xffd9a94eu;

    // Interaction states (overlays unless noted)
    std::uint32_t state_hover = 0x36d8bd82u;         // warm local light catch
    std::uint32_t state_pressed = 0x3a000000u;       // darkening overlay
    std::uint32_t state_selected = 0xffb49a68u;      // persistent bronze emphasis
    std::uint32_t state_disabled = 0x8020241fu;
    std::uint32_t state_focus = 0xffa08758u;

    // Shadows per elevation: BASE < RECESSED < RAISED < FLOATING < MODAL
    std::uint32_t shadow_raised = 0x50000000u;
    std::uint32_t shadow_floating = 0x75000000u;
    std::uint32_t shadow_modal = 0x90000000u;

    // Financial & Monetary tokens (Victoria 3 Financial mechanics & Smooth design)
    std::uint32_t gold_reserve = 0xffdfba52u;       // Radiant bullion gold
    std::uint32_t gold_reserve_dark = 0xff8c6e28u;  // Shaded bullion edge
    std::uint32_t silver_standard = 0xffc2cbd4u;    // Silver standard patina
    std::uint32_t bimetallic_copper = 0xffb87333u;  // Bimetallic bronze/copper
    std::uint32_t fiscal_surplus = 0xff6aa864u;     // Victorian ledger credit green
    std::uint32_t fiscal_deficit = 0xffcf544eu;     // Victorian ledger debit red
    std::uint32_t debt_ceiling_track = 0xff281618u; // Deep warning red well
    std::uint32_t investment_pool = 0xff5b90b8u;    // Commercial investment slate blue
    std::uint32_t bond_paper = 0xfff6efe0u;         // Treasury bond vellum
    std::uint32_t ink_ruler = 0x28604c36u;          // Ledger grid rule
};

struct UiThemeMaterials {
    // Polished dark wood (HUD bars, window frames)
    std::uint32_t wood_bevel = 0xff160a05u;
    std::uint32_t wood_base = 0xff4a2b18u;
    std::uint32_t wood_thunder = 0xff261208u;
    std::uint32_t wood_filigree = 0xffb9904fu;
    std::uint32_t wood_rosette = 0xffcaa565u;
    std::uint32_t wood_screw = 0xff352013u;

    // Aged vellum parchment (tooltips, documents, charts)
    std::uint32_t parchment_margin = 0xff3b2413u;
    std::uint32_t parchment_base = 0xfff4ebd7u;
    std::uint32_t parchment_sheet = 0xfff8f2e4u;
    std::uint32_t parchment_pinstripe = 0x407a5232u;
    std::uint32_t parchment_shadow = 0x451e1208u;
    std::uint32_t parchment_text = 0xff160b06u;
    std::uint32_t parchment_text_muted = 0xff6a482cu;
    std::uint32_t parchment_rule = 0x207a5232u;

    // Morocco leather (plaques, headers inside windows)
    std::uint32_t leather_base = 0xff2b1014u;
    std::uint32_t leather_fillet = 0x90d4af37u;

    // Brushed brass (buttons, fittings)
    std::uint32_t brass_rim = 0xff604c36u;
    std::uint32_t brass_rim_pressed = 0xff49382au;
    std::uint32_t brass_face = 0xff806b4cu;
    std::uint32_t brass_face_pressed = 0xff66543eu;
    std::uint32_t brass_highlight = 0xffaa936bu;
    std::uint32_t brass_highlight_pressed = 0xff827154u;

    // Imperial wax seal
    std::uint32_t wax_base = 0xff8c1e1cu;
    std::uint32_t wax_face = 0xffaa2626u;
    std::uint32_t wax_crest = 0xffd4af37u;
    std::uint32_t wax_crest_thunder = 0xfffff2b0u;
};

// Typography scale. The engine rasterizes whichever .thunderfont assets the
// game supplies; roles below map to font asset keys chosen by the game
// (display serif for titles, legible serif/sans for body, tabular numerals
// for stat values). Sizes and line heights are owned by the theme so no
// caller hard-codes a font size.
struct UiThemeTypography {
    float display = 26.0f;        // splash, event titles
    float window_title = 21.0f;   // window header titles
    float major_header = 17.5f;   // section headers
    float body = 17.0f;           // default body text
    float secondary = 15.75f;     // secondary descriptions and row labels
    float caption = 14.0f;        // badges, hints
    float stat_value = 22.0f;     // right-aligned stat numbers
    float stat_delta = 13.0f;     // +/− deltas under values
    float line_height = 21.5f;
    float line_height_dense = 18.5f;
};

// Compact grand-strategy metrics (dense, 1920x1080-first).
struct UiThemeMetrics {
    float space_xxs = 2.0f;
    float space_xs = 4.0f;
    float space_sm = 6.0f;
    float space_md = 10.0f;
    float space_lg = 14.0f;
    float space_xl = 20.0f;

    float border_thin = 1.0f;
    float border_normal = 2.0f;
    float border_frame = 4.0f;

    float header_height = 44.0f;   // window title header
    float top_bar_height = 78.0f;  // HUD top strip
    float tab_height = 26.0f;
    float row_height = 24.0f;      // dense list/table rows
    float table_header_height = 22.0f;
    float button_height = 26.0f;
    float toolbar_button_size = 48.0f;
    float scrollbar_thickness = 8.0f;
    float slider_track_height = 8.0f;
    float control_size = 14.0f;    // checkbox/radio box
    float tooltip_width = 320.0f;
    float shadow_offset = 3.0f;
};

// Restrained, functional motion (milliseconds).
struct UiThemeMotion {
    float hover_ms = 90.0f;
    float tooltip_ms = 120.0f;
    float panel_ms = 160.0f;
    float notification_ms = 220.0f;
};

struct UiTheme {
    UiThemeColors colors{};
    UiThemeMaterials materials{};
    UiThemeTypography type{};
    UiThemeMetrics metrics{};
    UiThemeMotion motion{};

    // The engine default: Victorian grand-strategy visual language.
    [[nodiscard]] static const UiTheme& victorian() noexcept;
};

// Linear blend of two packed colors: result = (1-t)*base + t*overlay.
[[nodiscard]] std::uint32_t ui_blend(std::uint32_t base, std::uint32_t overlay, float t) noexcept;

// Applies a state overlay color on top of a base color (alpha-weighted).
[[nodiscard]] std::uint32_t ui_apply_overlay(std::uint32_t base, std::uint32_t overlay) noexcept;

// Strategy-game number presentation: thousands separators, optional
// decimals, compact suffixes (K/M/B) for large magnitudes.
[[nodiscard]] std::string ui_format_number(double value, int decimals = 0, bool compact = false);

// Signed delta, e.g. "+1,250" / "−32". The returned color token comes from
// the theme (positive/negative/neutral) via the out parameter.
[[nodiscard]] std::string ui_format_delta(double value, int decimals = 0);
[[nodiscard]] std::uint32_t ui_delta_color(const UiTheme& theme, double value) noexcept;

// Victorian financial currency presentation: prefix symbol, thousands grouping,
// compact magnitude abbreviations (e.g. "£14.2K", "−£2.5M"), optional signed delta.
[[nodiscard]] std::string ui_format_currency(double value, std::string_view symbol = "£", bool compact = true, int decimals = -1, bool explicit_plus = false);

// UTF-8 and multi-line aware text width estimation for Victorian UI layouts.
[[nodiscard]] float ui_estimate_text_width(std::string_view text, float font_size) noexcept;

} // namespace thunder
