#pragma once

#include "thunder/presentation/render/map/VectorMapPipeline.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include "thunder/simulation/world/WorldMapLabels.hpp"
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thunder {

struct SplinePoint {
    float x = 0.0f;
    float y = 0.0f;
    float tangent_x = 1.0f;
    float tangent_y = 0.0f;
    float distance = 0.0f;
};

struct CurvedCharGlyph {
    std::string utf8;
    float x = 0.0f;
    float y = 0.0f;
    float angle_rad = 0.0f;
    float font_size = 14.0f;
    std::uint32_t rgba = 0xff3a2618u;
};

struct CurvedLabelLayout {
    std::string text;
    std::vector<CurvedCharGlyph> glyphs;
    UiRect aabb{};
    int priority = 0; // Higher = Sovereign Country, Medium = State, Lower = City
    bool is_visible = true;
};

struct EngravedMapGlyph {
    std::string utf8;
    float u = 0.0f;
    float v = 0.0f;
    float tangent_u = 1.0f;
    float tangent_v = 0.0f;
    float font_size_uv = 0.01f;
    std::uint32_t rgba = 0xf2221d19u;
};

struct EngravedMapLabel {
    std::string key;
    std::string text;
    float min_u = 0.0f;
    float max_u = 1.0f;
    float min_v = 0.0f;
    float max_v = 1.0f;
    float center_u = 0.5f;
    float center_v = 0.5f;
    std::vector<EngravedMapGlyph> glyphs;
};

class VectorMapTypography {
public:
    VectorMapTypography() = default;

    // Interpolates Catmull-Rom spline along medial axis anchor points
    [[nodiscard]] static std::vector<SplinePoint> sample_spline(std::span<const VectorPoint> anchors,
                                                                std::size_t sample_count = 32);

    // Formats and places characters along the spline path
    [[nodiscard]] static CurvedLabelLayout layout_curved_label(std::string text,
                                                              std::span<const VectorPoint> anchors,
                                                              float font_size = 16.0f,
                                                              std::uint32_t rgba = 0xff3a2618u,
                                                              int priority = 10,
                                                              float tracking_factor = 1.0f,
                                                              float fill_ratio = 0.8f);

    // Prunes overlapping labels based on priority
    static void prune_collisions(std::span<CurvedLabelLayout> labels);

    // Emits curved glyph text runs to UiDrawList
    static void render_labels(UiDrawList& ui, std::span<const CurvedLabelLayout> labels, UiRect scissor = {});

    // Precomputes an engraved country label permanently fixed in map UV space
    [[nodiscard]] static EngravedMapLabel layout_engraved_country(
        std::string text,
        std::span<const WorldMapLabelPoint> spine,
        double geographic_area_km2 = 0.0,
        std::uint32_t rgba = 0xf2221d19u);

    // Precomputes all country labels from WorldMapLabels once
    [[nodiscard]] static std::vector<EngravedMapLabel> prepare_engraved_countries(
        const WorldMapLabels& labels);

    // Renders engraved country labels fixed to the terrain geography
    static void render_engraved_labels(
        UiDrawList& ui,
        std::span<const EngravedMapLabel> engraved_labels,
        double center_u, double center_v,
        double half_u, double half_v,
        int screen_width, int screen_height,
        bool horizontal_wrap = false);
};

} // namespace thunder
