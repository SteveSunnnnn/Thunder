#pragma once

#include "thunder/presentation/ui/StrategyUi.hpp"
#include <cstdint>
#include <string_view>

namespace thunder {

class MapDecorationRenderer {
public:
    // Render solid mahogany desk border around map canvas with gilded brass inlay and graticule graduation ticks
    static void render_tabletop_wood_frame(UiDrawList& ui, UiRect canvas_rect, float frame_thickness = 28.0f);

    // Render authentic 16-point nautical brass compass rose with dual-tone shaded faceted star, fleur-de-lis and wind labels
    static void render_brass_compass_rose(UiDrawList& ui, float center_x, float center_y, float radius = 75.0f);

    // Render Victorian ornate baroque strapwork corner filigree cartouches with scrollwork and acanthus flourishes
    static void render_corner_vignettes(UiDrawList& ui, UiRect map_rect, float size = 64.0f);

    // Render decorative Victorian royal cartouche banner ribbon (for map title / era label)
    static void render_ornate_title_cartouche(UiDrawList& ui, float center_x, float top_y,
                                              std::string_view title, std::string_view subtitle);
};

} // namespace thunder
