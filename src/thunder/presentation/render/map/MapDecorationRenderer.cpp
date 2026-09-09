#include "thunder/presentation/render/map/MapDecorationRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

namespace {
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] float estimate_cartouche_width(std::string_view text, float size) noexcept {
    return ui_estimate_text_width(text, size);
}

} // namespace

void MapDecorationRenderer::render_tabletop_wood_frame(UiDrawList& ui, UiRect r, float thick) {
    if (r.w <= 0.0f || r.h <= 0.0f || thick <= 0.0f || r.w < thick * 2.0f || r.h < thick * 2.0f) return;

    // 1. Soft Inner Contact Bevel Shadow onto the Map Canvas
    // (Cast only along the 4 perimeter inner edges — NEVER a solid rect over the whole map!)
    const UiRect inner_canvas{r.x + thick, r.y + thick, r.w - thick * 2.0f, r.h - thick * 2.0f};
    if (inner_canvas.w > 0.0f && inner_canvas.h > 0.0f) {
        constexpr float shadow_depth = 6.0f;
        // Top edge shadow fading downward
        ui.quad_gradient({inner_canvas.x, inner_canvas.y, inner_canvas.w, shadow_depth},
                         0x70000000u, 0x00000000u, true);
        // Bottom edge shadow fading upward
        ui.quad_gradient({inner_canvas.x, inner_canvas.y + inner_canvas.h - shadow_depth, inner_canvas.w, shadow_depth},
                         0x00000000u, 0x70000000u, true);
        // Left edge shadow fading rightward
        ui.quad_gradient({inner_canvas.x, inner_canvas.y, shadow_depth, inner_canvas.h},
                         0x60000000u, 0x00000000u, false);
        // Right edge shadow fading leftward
        ui.quad_gradient({inner_canvas.x + inner_canvas.w - shadow_depth, inner_canvas.y, shadow_depth, inner_canvas.h},
                         0x00000000u, 0x60000000u, false);
    }

    // 2. Outer Polished Mahogany Frame with Directional Bevel Lighting
    // Top bar (lit)
    ui.quad_gradient({r.x, r.y, r.w, thick}, 0xff32180du, 0xff1a0c06u, true);
    ui.quad({r.x, r.y, r.w, 1.5f}, 0x40ffffffu); // Specular top edge catch

    // Bottom bar (shaded)
    ui.quad_gradient({r.x, r.y + r.h - thick, r.w, thick}, 0xff1c0d07u, 0xff100603u, true);
    ui.quad({r.x, r.y + r.h - 1.5f, r.w, 1.5f}, 0x50000000u);

    // Left bar (lit)
    ui.quad_gradient({r.x, r.y + thick, thick, r.h - thick * 2.0f}, 0xff2c140au, 0xff180b05u, false);
    ui.quad({r.x, r.y + thick, 1.5f, r.h - thick * 2.0f}, 0x30ffffffu);

    // Right bar (shaded)
    ui.quad_gradient({r.x + r.w - thick, r.y + thick, thick, r.h - thick * 2.0f}, 0xff180b05u, 0xff0e0502u, false);
    ui.quad({r.x + r.w - 1.5f, r.y + thick, 1.5f, r.h - thick * 2.0f}, 0x60000000u);

    // 2b. Authentic Craftsman 45° Miter Joint Accents at All 4 Corners
    // Top-Left corner miter: (r.x, r.y) to (r.x + thick, r.y + thick)
    ui.quad_points(r.x, r.y, r.x + 1.0f, r.y, r.x + thick + 1.0f, r.y + thick, r.x + thick, r.y + thick, 0x60000000u);
    ui.quad_points(r.x + 1.0f, r.y, r.x + 2.0f, r.y, r.x + thick + 2.0f, r.y + thick, r.x + thick + 1.0f, r.y + thick, 0x40dfba52u);

    // Top-Right corner miter: (r.x + r.w, r.y) to (r.x + r.w - thick, r.y + thick)
    ui.quad_points(r.x + r.w, r.y, r.x + r.w - 1.0f, r.y, r.x + r.w - thick - 1.0f, r.y + thick, r.x + r.w - thick, r.y + thick, 0x60000000u);
    ui.quad_points(r.x + r.w - 1.0f, r.y, r.x + r.w - 2.0f, r.y, r.x + r.w - thick - 2.0f, r.y + thick, r.x + r.w - thick - 1.0f, r.y + thick, 0x40dfba52u);

    // Bottom-Left corner miter: (r.x, r.y + r.h) to (r.x + thick, r.y + r.h - thick)
    ui.quad_points(r.x, r.y + r.h, r.x + 1.0f, r.y + r.h, r.x + thick + 1.0f, r.y + r.h - thick, r.x + thick, r.y + r.h - thick, 0x60000000u);
    ui.quad_points(r.x + 1.0f, r.y + r.h, r.x + 2.0f, r.y + r.h, r.x + thick + 2.0f, r.y + r.h - thick, r.x + thick + 1.0f, r.y + r.h - thick, 0x40dfba52u);

    // Bottom-Right corner miter: (r.x + r.w, r.y + r.h) to (r.x + r.w - thick, r.y + r.h - thick)
    ui.quad_points(r.x + r.w, r.y + r.h, r.x + r.w - 1.0f, r.y + r.h, r.x + r.w - thick - 1.0f, r.y + r.h - thick, r.x + r.w - thick, r.y + r.h - thick, 0x60000000u);
    ui.quad_points(r.x + r.w - 1.0f, r.y + r.h, r.x + r.w - 2.0f, r.y + r.h, r.x + r.w - thick - 2.0f, r.y + r.h - thick, r.x + r.w - thick - 1.0f, r.y + r.h - thick, 0x40dfba52u);

    // 3. Gilded Brass Inlay Border
    const float brass_offset = thick - 4.0f;
    if (brass_offset > 0.0f && r.w > brass_offset * 2.0f && r.h > brass_offset * 2.0f) {
        // Gold highlight wire
        ui.quad({r.x + brass_offset, r.y + brass_offset, r.w - brass_offset * 2.0f, 2.0f}, 0xffdfba52u);
        ui.quad({r.x + brass_offset, r.y + r.h - brass_offset - 2.0f, r.w - brass_offset * 2.0f, 2.0f}, 0xffa08030u);
        ui.quad({r.x + brass_offset, r.y + brass_offset, 2.0f, r.h - brass_offset * 2.0f}, 0xffdfba52u);
        ui.quad({r.x + r.w - brass_offset - 2.0f, r.y + brass_offset, 2.0f, r.h - brass_offset * 2.0f}, 0xffa08030u);

        // Mitered shadow groove beneath brass inlay
        ui.quad({r.x + brass_offset - 1.0f, r.y + brass_offset - 1.0f, r.w - brass_offset * 2.0f + 2.0f, 1.0f}, 0x60000000u);
        ui.quad({r.x + brass_offset - 1.0f, r.y + brass_offset - 1.0f, 1.0f, r.h - brass_offset * 2.0f + 2.0f}, 0x60000000u);
    }

    // 4. Graticule Latitude/Longitude Degree Ticks (Alternating Nautical Graduation Ribbon)
    // Tiled dynamically to distribute remainder evenly with zero gap across any resolution
    if (r.w > thick * 2.0f && thick >= 12.0f) {
        const float avail_w = r.w - thick * 2.0f;
        constexpr float target_tick_sz = 16.0f;
        const auto h_ticks = static_cast<std::size_t>(std::max(1.0f, std::round(avail_w / target_tick_sz)));
        const float actual_tick_w = avail_w / static_cast<float>(h_ticks);
        for (std::size_t i = 0; i < h_ticks; ++i) {
            const std::uint32_t col = (i % 2 == 0) ? 0xff24140bu : 0xffeae0c8u;
            const float tx = r.x + thick + static_cast<float>(i) * actual_tick_w;
            ui.quad({tx, r.y + thick - 8.0f, actual_tick_w, 4.0f}, col);
            ui.quad({tx, r.y + r.h - thick + 4.0f, actual_tick_w, 4.0f}, col);
            // Fine brass tick divider
            ui.quad({tx, r.y + thick - 8.0f, 1.0f, 4.0f}, 0x50d4af37u);
            ui.quad({tx, r.y + r.h - thick + 4.0f, 1.0f, 4.0f}, 0x50d4af37u);

            // Admiralty major graduation mark every 5 ticks (gilded center pip)
            if (i % 5 == 0 && actual_tick_w >= 8.0f) {
                const float pip_x = tx + (actual_tick_w - 2.0f) * 0.5f;
                ui.quad({pip_x, r.y + thick - 7.0f, 2.0f, 2.0f}, 0xffdfba52u);
                ui.quad({pip_x, r.y + r.h - thick + 5.0f, 2.0f, 2.0f}, 0xffdfba52u);
            }
        }
    }
    if (r.h > thick * 2.0f && thick >= 12.0f) {
        const float avail_h = r.h - thick * 2.0f;
        constexpr float target_tick_sz = 16.0f;
        const auto v_ticks = static_cast<std::size_t>(std::max(1.0f, std::round(avail_h / target_tick_sz)));
        const float actual_tick_h = avail_h / static_cast<float>(v_ticks);
        for (std::size_t i = 0; i < v_ticks; ++i) {
            const std::uint32_t col = (i % 2 == 0) ? 0xff24140bu : 0xffeae0c8u;
            const float ty = r.y + thick + static_cast<float>(i) * actual_tick_h;
            ui.quad({r.x + thick - 8.0f, ty, 4.0f, actual_tick_h}, col);
            ui.quad({r.x + r.w - thick + 4.0f, ty, 4.0f, actual_tick_h}, col);
            // Fine brass tick divider
            ui.quad({r.x + thick - 8.0f, ty, 4.0f, 1.0f}, 0x50d4af37u);
            ui.quad({r.x + r.w - thick + 4.0f, ty, 4.0f, 1.0f}, 0x50d4af37u);

            // Admiralty major graduation mark every 5 ticks (gilded center pip)
            if (i % 5 == 0 && actual_tick_h >= 8.0f) {
                const float pip_y = ty + (actual_tick_h - 2.0f) * 0.5f;
                ui.quad({r.x + thick - 7.0f, pip_y, 2.0f, 2.0f}, 0xffdfba52u);
                ui.quad({r.x + r.w - thick + 5.0f, pip_y, 2.0f, 2.0f}, 0xffdfba52u);
            }
        }
    }

    // 5. Corner Brass Reinforcement Plates with 4 Countersunk Screws & Medallion
    const float plate_sz = std::min(thick * 0.9f, 26.0f);
    const float corners[4][2] = {
        {r.x + 2.0f, r.y + 2.0f},
        {r.x + r.w - plate_sz - 2.0f, r.y + 2.0f},
        {r.x + 2.0f, r.y + r.h - plate_sz - 2.0f},
        {r.x + r.w - plate_sz - 2.0f, r.y + r.h - plate_sz - 2.0f}
    };
    for (const auto& c : corners) {
        // Polished brass plate with mitered rim
        ui.quad({c[0], c[1], plate_sz, plate_sz}, 0xff8c6e28u);
        ui.quad({c[0] + 1.0f, c[1] + 1.0f, plate_sz - 2.0f, plate_sz - 2.0f}, 0xffeed065u);
        ui.quad({c[0] + 2.0f, c[1] + 2.0f, plate_sz - 4.0f, plate_sz - 4.0f}, 0xffdfba52u);

        // 4 countersunk screws in plate corners
        const float s_off = 3.0f;
        const float s_sz = 3.0f;
        const float screws[4][2] = {
            {c[0] + s_off, c[1] + s_off},
            {c[0] + plate_sz - s_off - s_sz, c[1] + s_off},
            {c[0] + s_off, c[1] + plate_sz - s_off - s_sz},
            {c[0] + plate_sz - s_off - s_sz, c[1] + plate_sz - s_off - s_sz}
        };
        for (const auto& s : screws) {
            ui.quad({s[0], s[1], s_sz, s_sz}, 0xff3a2412u);
            ui.quad({s[0] + 0.5f, s[1] + 0.5f, s_sz - 1.0f, s_sz - 1.0f}, 0xff705526u);
        }

        // Center rosette boss
        ui.radial_disc(c[0] + plate_sz * 0.5f, c[1] + plate_sz * 0.5f, plate_sz * 0.22f, 0xffeed065u, 0xff8c6e28u);
    }
}

void MapDecorationRenderer::render_brass_compass_rose(UiDrawList& ui, float cx, float cy, float radius) {
    if (radius <= 0.0f) return;

    // 0. Ocean Navigational Rhumb Lines (faint rays anchoring compass rose to the nautical map)
    constexpr int rhumb_count = 16;
    const float rhumb_inner = radius * 0.94f;
    const float rhumb_outer = radius * 2.6f;
    for (int r = 0; r < rhumb_count; ++r) {
        const float angle = static_cast<float>(r) * (kPi * 2.0f / static_cast<float>(rhumb_count));
        const float sin_a = std::sin(angle);
        const float cos_a = std::cos(angle);
        const float p0x = cx + sin_a * rhumb_inner;
        const float p0y = cy - cos_a * rhumb_inner;
        const float p1x = cx + sin_a * rhumb_outer;
        const float p1y = cy - cos_a * rhumb_outer;
        const float nx = -cos_a * 0.6f;
        const float ny = -sin_a * 0.6f;
        // Authentically fading navigational ray: warm sepia fading smoothly to zero alpha
        ui.quad_points_colors(p0x - nx, p0y - ny, 0x488b6f4eu,
                              p0x + nx, p0y + ny, 0x488b6f4eu,
                              p1x + nx, p1y + ny, 0x008b6f4eu,
                              p1x - nx, p1y - ny, 0x008b6f4eu);
    }

    // 1. Compass Rose Parchment Dial Backing
    ui.radial_disc(cx, cy, radius * 0.96f, 0xf4eee4cfu, 0xe8dfceb8u);

    // 2. Concentric Brass Calibration Rings
    ui.radial_disc(cx, cy, radius, 0x00000000u, 0xff8c6e28u, {}, 48);
    ui.radial_disc(cx, cy, radius * 0.94f, 0x00000000u, 0xffdfba52u, {}, 48);
    ui.radial_disc(cx, cy, radius * 0.65f, 0x00000000u, 0x508c6e28u, {}, 40);
    ui.radial_disc(cx, cy, radius * 0.40f, 0x00000000u, 0x60dfba52u, {}, 32);

    // Degree graduation ticks around circumference (every 15° and 5°)
    constexpr int total_ticks = 72; // every 5 degrees
    for (int t = 0; t < total_ticks; ++t) {
        const float tick_angle = static_cast<float>(t) * (kPi * 2.0f / static_cast<float>(total_ticks));
        const bool major = (t % 6 == 0); // every 30 deg
        const bool medium = (t % 3 == 0); // every 15 deg
        const float tick_len = major ? 6.0f : (medium ? 4.0f : 2.5f);
        const float r_outer = radius * 0.94f;
        const float r_inner = r_outer - tick_len;

        const float sin_t = std::sin(tick_angle);
        const float cos_t = std::cos(tick_angle);
        const float x0 = cx + sin_t * r_inner;
        const float y0 = cy - cos_t * r_inner;
        const float x1 = cx + sin_t * r_outer;
        const float y1 = cy - cos_t * r_outer;

        const std::uint32_t col = major ? 0xff8c261fu : (medium ? 0xff2c1c0fu : 0x705c3c24u);
        ui.quad_points(x0 - 0.5f, y0, x0 + 0.5f, y0, x1 + 0.5f, y1, x1 - 0.5f, y1, col);
    }

    // 3. 16-Point Solid Dual-Tone Shaded Faceted Nautical Star
    constexpr std::uint32_t kGoldFacet = 0xffeed065u;
    constexpr std::uint32_t kBronzeFacet = 0xff875f24u;
    constexpr std::uint32_t kGoldSubFacet = 0xffdfba52u;
    constexpr std::uint32_t kBronzeSubFacet = 0xff69491au;

    // First draw 8 secondary sub-points (NNE, ENE, etc.) so primary points overlay cleanly
    for (int i = 0; i < 8; ++i) {
        const float angle = (static_cast<float>(i) + 0.5f) * (kPi * 0.25f);
        const float r_tip = radius * 0.65f;
        const float tip_x = cx + std::sin(angle) * r_tip;
        const float tip_y = cy - std::cos(angle) * r_tip;

        const float half_w = 0.12f;
        const float base_r = radius * 0.28f;
        const float bx_l = cx + std::sin(angle - half_w) * base_r;
        const float by_l = cy - std::cos(angle - half_w) * base_r;
        const float bx_r = cx + std::sin(angle + half_w) * base_r;
        const float by_r = cy - std::cos(angle + half_w) * base_r;

        // Solid triangular facets: quad with repeated 3rd/4th point
        ui.quad_points(cx, cy, tip_x, tip_y, bx_l, by_l, bx_l, by_l, kGoldSubFacet);
        ui.quad_points(cx, cy, tip_x, tip_y, bx_r, by_r, bx_r, by_r, kBronzeSubFacet);
    }

    // Now draw 8 primary cardinal & intercardinal points
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * (kPi * 0.25f);
        const float r_tip = (i % 2 == 0) ? radius * 0.88f : radius * 0.76f;
        const float tip_x = cx + std::sin(angle) * r_tip;
        const float tip_y = cy - std::cos(angle) * r_tip;

        const float half_w = 0.15f;
        const float base_r = radius * 0.32f;
        const float bx_l = cx + std::sin(angle - half_w) * base_r;
        const float by_l = cy - std::cos(angle - half_w) * base_r;
        const float bx_r = cx + std::sin(angle + half_w) * base_r;
        const float by_r = cy - std::cos(angle + half_w) * base_r;

        // Solid left illuminated facet & right shadowed facet
        ui.quad_points(cx, cy, tip_x, tip_y, bx_l, by_l, bx_l, by_l, kGoldFacet);
        ui.quad_points(cx, cy, tip_x, tip_y, bx_r, by_r, bx_r, by_r, kBronzeFacet);
    }

    // 4. North Heraldic Fleur-de-lis Crest (True 3-Petal Heraldic Star Head)
    const float fleur_y = cy - radius * 0.88f;
    // Central lance spearhead (dual-tone)
    ui.quad_points(cx, fleur_y - 8.0f, cx - 3.5f, fleur_y + 3.0f, cx, fleur_y + 3.0f, cx, fleur_y - 8.0f, 0xffeed065u);
    ui.quad_points(cx, fleur_y - 8.0f, cx, fleur_y + 3.0f, cx + 3.5f, fleur_y + 3.0f, cx, fleur_y - 8.0f, 0xff8c6e28u);
    // Left outward curling petal
    ui.quad_points(cx - 3.5f, fleur_y + 3.0f, cx - 7.0f, fleur_y - 2.0f, cx - 5.0f, fleur_y + 5.0f, cx - 2.0f, fleur_y + 5.0f, 0xffdfba52u);
    // Right outward curling petal
    ui.quad_points(cx + 3.5f, fleur_y + 3.0f, cx + 7.0f, fleur_y - 2.0f, cx + 5.0f, fleur_y + 5.0f, cx + 2.0f, fleur_y + 5.0f, 0xff875f24u);
    // Horizontal tie band / clasp ring
    ui.quad({cx - 5.0f, fleur_y + 3.0f, 10.0f, 2.0f}, 0xffeed065u);
    ui.quad({cx - 5.0f, fleur_y + 4.0f, 10.0f, 1.0f}, 0xff8c6e28u);
    // Ruby spearhead jewel finial
    ui.quad({cx - 1.0f, fleur_y - 7.0f, 2.0f, 2.0f}, 0xffd92b2bu);

    // 5. Central Brass Boss & Ruby Pivot Jewel
    ui.radial_disc(cx, cy, radius * 0.24f, 0xffeed065u, 0xff8c6e28u, {}, 32);
    ui.radial_disc(cx, cy, radius * 0.16f, 0xff1e1208u, 0xff3a2412u, {}, 24);
    ui.radial_disc(cx, cy, radius * 0.09f, 0xffd92b2bu, 0xff701414u, {}, 20);
    // Specular micro-glint on jewel
    ui.quad({cx - 1.0f, cy - 2.0f, 2.0f, 2.0f}, 0xc0ffffffu);

    // 6. Cardinal & Intercardinal Typography with Intaglio Engraved Shadow Pass
    // "N" in prominent regal imperial crimson
    ui.text("N", cx - 5.0f + 1.0f, cy - radius - 14.0f + 1.0f, 15.0f, 0x80100a06u);
    ui.text("N", cx - 5.0f, cy - radius - 14.0f, 15.0f, 0xffb82018u);

    // Principal cardinals E, S, W in warm antique walnut
    ui.text("E", cx + radius + 4.0f + 1.0f, cy - 6.0f + 1.0f, 13.0f, 0x80100a06u);
    ui.text("E", cx + radius + 4.0f, cy - 6.0f, 13.0f, 0xff2c1c0fu);

    ui.text("S", cx - 4.0f + 1.0f, cy + radius + 4.0f + 1.0f, 13.0f, 0x80100a06u);
    ui.text("S", cx - 4.0f, cy + radius + 4.0f, 13.0f, 0xff2c1c0fu);

    ui.text("W", cx - radius - 16.0f + 1.0f, cy - 6.0f + 1.0f, 13.0f, 0x80100a06u);
    ui.text("W", cx - radius - 16.0f, cy - 6.0f, 13.0f, 0xff2c1c0fu);

    // Intercardinal winds NE, SE, SW, NW in subtle antique bronze
    constexpr float inter_r = 0.85f;
    const float diag = radius * inter_r * 0.7071f;
    ui.text("NE", cx + diag - 6.0f + 0.5f, cy - diag - 6.0f + 0.5f, 9.5f, 0x70100a06u);
    ui.text("NE", cx + diag - 6.0f, cy - diag - 6.0f, 9.5f, 0xff69491au);

    ui.text("SE", cx + diag - 6.0f + 0.5f, cy + diag - 2.0f + 0.5f, 9.5f, 0x70100a06u);
    ui.text("SE", cx + diag - 6.0f, cy + diag - 2.0f, 9.5f, 0xff69491au);

    ui.text("SW", cx - diag - 8.0f + 0.5f, cy + diag - 2.0f + 0.5f, 9.5f, 0x70100a06u);
    ui.text("SW", cx - diag - 8.0f, cy + diag - 2.0f, 9.5f, 0xff69491au);

    ui.text("NW", cx - diag - 8.0f + 0.5f, cy - diag - 6.0f + 0.5f, 9.5f, 0x70100a06u);
    ui.text("NW", cx - diag - 8.0f, cy - diag - 6.0f, 9.5f, 0xff69491au);
}

void MapDecorationRenderer::render_corner_vignettes(UiDrawList& ui, UiRect map_rect, float sz) {
    if (sz <= 0.0f) return;

    // Authentic Victorian baroque filigree corner cartouches with scrollwork and gold leaf flourishes
    const auto draw_corner = [&](float ox, float oy, float sx, float sy) {
        auto draw_box = [&](float x, float y, float w, float h, std::uint32_t col) {
            const float rx = (sx < 0.0f) ? (x - w) : x;
            const float ry = (sy < 0.0f) ? (y - h) : y;
            ui.quad({rx, ry, w, h}, col);
        };

        // 1. Dark bronze outer structural bracket
        draw_box(ox, oy, sz, 3.5f, 0xff2a180du);
        draw_box(ox, oy, 3.5f, sz, 0xff2a180du);

        // 2. Gilded brass inner fillet
        draw_box(ox + 5.0f * sx, oy + 5.0f * sy, sz - 10.0f, 1.8f, 0xffdfba52u);
        draw_box(ox + 5.0f * sx, oy + 5.0f * sy, 1.8f, sz - 10.0f, 0xffdfba52u);

        // 3. Acanthus leaf scrollwork flourishes
        draw_box(ox + 14.0f * sx, oy + 14.0f * sy, 10.0f, 10.0f, 0xffeed065u);
        draw_box(ox + 16.0f * sx, oy + 16.0f * sy, 6.0f, 6.0f, 0xff8c6e28u);

        draw_box(ox + 26.0f * sx, oy + 9.0f * sy, 6.0f, 3.0f, 0xffdfba52u);
        draw_box(ox + 9.0f * sx, oy + 26.0f * sy, 3.0f, 6.0f, 0xffdfba52u);

        // 4. Corner brass rosette boss
        ui.radial_disc(ox + 7.0f * sx, oy + 7.0f * sy, 4.0f, 0xffeed065u, 0xff8c6e28u);
    };

    // Top-Left (sx=1, sy=1)
    draw_corner(map_rect.x, map_rect.y, 1.0f, 1.0f);
    // Top-Right (sx=-1, sy=1)
    draw_corner(map_rect.x + map_rect.w, map_rect.y, -1.0f, 1.0f);
    // Bottom-Left (sx=1, sy=-1)
    draw_corner(map_rect.x, map_rect.y + map_rect.h, 1.0f, -1.0f);
    // Bottom-Right (sx=-1, sy=-1)
    draw_corner(map_rect.x + map_rect.w, map_rect.y + map_rect.h, -1.0f, -1.0f);
}

void MapDecorationRenderer::render_ornate_title_cartouche(
    UiDrawList& ui, float cx, float top_y, std::string_view title, std::string_view subtitle) {

    const float title_w = estimate_cartouche_width(title, 18.0f);
    const float subtitle_w = estimate_cartouche_width(subtitle, 12.0f);
    const float content_w = std::max(title_w, subtitle_w);
    const float banner_w = std::max(360.0f, content_w + 64.0f);
    constexpr float banner_h = 60.0f;
    const float bx = cx - banner_w * 0.5f;

    // 1. Ambient soft drop shadow
    ui.drop_shadow({bx, top_y, banner_w, banner_h}, 0x70000000u, 6.0f, 0.0f, 3.0f);

    // 2. Swallowtail Pennant Notched Ribbon Ends (Flanking the Cartouche)
    const float pennant_w = 28.0f;
    const float pennant_h = 36.0f;
    const float pennant_y = top_y + 12.0f;
    const float y_mid = pennant_y + pennant_h * 0.5f;

    // Left pennant (authentic notched swallowtail geometry with true transparency in notch)
    const float lx_att = bx + 6.0f;
    const float lx_tip = bx - pennant_w + 6.0f;
    const float lx_notch = bx - pennant_w + 16.0f;
    // Top wing
    ui.quad_points_colors(lx_tip, pennant_y, 0xffeed065u,
                          lx_att, pennant_y, 0xff8c6e28u,
                          lx_att, y_mid, 0xff8c6e28u,
                          lx_notch, y_mid, 0xffdfba52u);
    // Bottom wing
    ui.quad_points_colors(lx_notch, y_mid, 0xffdfba52u,
                          lx_att, y_mid, 0xff8c6e28u,
                          lx_att, pennant_y + pennant_h, 0xff8c6e28u,
                          lx_tip, pennant_y + pennant_h, 0xffeed065u);

    // Right pennant
    const float rx_att = bx + banner_w - 6.0f;
    const float rx_tip = bx + banner_w + pennant_w - 6.0f;
    const float rx_notch = bx + banner_w + pennant_w - 16.0f;
    // Top wing
    ui.quad_points_colors(rx_att, pennant_y, 0xff8c6e28u,
                          rx_tip, pennant_y, 0xffeed065u,
                          rx_notch, y_mid, 0xffdfba52u,
                          rx_att, y_mid, 0xff8c6e28u);
    // Bottom wing
    ui.quad_points_colors(rx_att, y_mid, 0xff8c6e28u,
                          rx_notch, y_mid, 0xffdfba52u,
                          rx_tip, pennant_y + pennant_h, 0xffeed065u,
                          rx_att, pennant_y + pennant_h, 0xff8c6e28u);

    // 3. Aged Parchment Scroll Center Plaque
    ui.quad_gradient({bx, top_y, banner_w, banner_h}, 0xfffcf8edu, 0xffe8dcbfu, true);

    // 4. Gilded Engraved Filigree Border
    ui.quad({bx, top_y, banner_w, 2.5f}, 0xffdfba52u);
    ui.quad({bx, top_y + banner_h - 2.5f, banner_w, 2.5f}, 0xff8c6e28u);
    ui.quad({bx, top_y, 2.5f, banner_h}, 0xffdfba52u);
    ui.quad({bx + banner_w - 2.5f, top_y, 2.5f, banner_h}, 0xff8c6e28u);

    // Inner hairline pinstripe
    ui.quad({bx + 4.0f, top_y + 4.0f, banner_w - 8.0f, 1.0f}, 0x408c6e28u);
    ui.quad({bx + 4.0f, top_y + banner_h - 5.0f, banner_w - 8.0f, 1.0f}, 0x408c6e28u);

    // Corner rosette bosses on cartouche
    ui.radial_disc(bx + 6.0f, top_y + 6.0f, 3.5f, 0xffeed065u, 0xff8c6e28u);
    ui.radial_disc(bx + banner_w - 6.0f, top_y + 6.0f, 3.5f, 0xffeed065u, 0xff8c6e28u);
    ui.radial_disc(bx + 6.0f, top_y + banner_h - 6.0f, 3.5f, 0xffeed065u, 0xff8c6e28u);
    ui.radial_disc(bx + banner_w - 6.0f, top_y + banner_h - 6.0f, 3.5f, 0xffeed065u, 0xff8c6e28u);

    // 5. Imperial Wax Seal Medallion in Cartouche Center Bottom
    ui.radial_disc(cx, top_y + banner_h, 11.0f, 0xffaa2626u, 0xff701414u);
    ui.radial_disc(cx, top_y + banner_h, 5.0f, 0xffdfba52u, 0xff8c6e28u);

    // Flanking laurel filigree flourishes beneath wax seal
    ui.quad({cx - 16.0f, top_y + banner_h - 2.0f, 6.0f, 2.0f}, 0xffdfba52u);
    ui.quad({cx + 10.0f, top_y + banner_h - 2.0f, 6.0f, 2.0f}, 0xffdfba52u);

    // 6. Victorian Engraved Typography with Exact UTF-8 Aware Centering & Intaglio Shadow
    const bool has_subtitle = !subtitle.empty();
    const float title_y = has_subtitle ? (top_y + 11.0f) : (top_y + (banner_h - 18.0f) * 0.5f);

    // Intaglio shadow pass
    ui.text(std::string(title), cx + 0.5f, title_y + 0.5f, 18.0f, 0x50ffffffu, {}, true);
    ui.text(std::string(title), cx, title_y, 18.0f, 0xff1e1208u, {}, true);

    if (has_subtitle) {
        ui.text(std::string(subtitle), cx + 0.5f, top_y + 35.5f, 12.0f, 0x40ffffffu, {}, true);
        ui.text(std::string(subtitle), cx, top_y + 35.0f, 12.0f, 0xff664a30u, {}, true);
    }
}

} // namespace thunder
