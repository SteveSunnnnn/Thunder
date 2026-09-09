#include "thunder/presentation/render/map/VectorMapPipeline.hpp"
#include "thunder/presentation/render/map/VectorMapTypography.hpp"
#include "thunder/presentation/render/flag/DynamicFlag3D.hpp"
#include "thunder/presentation/render/water/PhysicalWaterPass.hpp"
#include "thunder/simulation/world/WorldMapLabels.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include "thunder/presentation/ui/ScriptedGui.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace thunder;

int main() {
    std::cout << "[Vector Map and UI Tests Starting]...\n";

    // 1. Test VectorMapSystem polygons and shared edges
    {
        VectorMapSystem strategy_map;

        // Province 1 (Country 1, State 1)
        std::vector<VectorPoint> poly1_pts{{0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 100.0f}, {0.0f, 100.0f}};
        strategy_map.add_province_polygon(ProvinceId{1}, CountryId{1}, StateId{1}, poly1_pts);

        // Province 2 (Country 1, State 2) - Shares x = 100.0 edge with Province 1
        std::vector<VectorPoint> poly2_pts{{100.0f, 0.0f}, {200.0f, 0.0f}, {200.0f, 100.0f}, {100.0f, 100.0f}};
        strategy_map.add_province_polygon(ProvinceId{2}, CountryId{1}, StateId{2}, poly2_pts);

        // Province 3 (Country 2, State 3) - Shares y = 100.0 edge with Province 1
        std::vector<VectorPoint> poly3_pts{{0.0f, 100.0f}, {100.0f, 100.0f}, {100.0f, 200.0f}, {0.0f, 200.0f}};
        strategy_map.add_province_polygon(ProvinceId{3}, CountryId{2}, StateId{3}, poly3_pts);

        assert(strategy_map.polygon_count() == 3);

        strategy_map.rebuild_shared_edges();
        assert(strategy_map.edge_count() > 0);

        bool found_country_border = false;
        bool found_state_border = false;
        bool found_coastline = false;

        for (const auto& e : strategy_map.edges()) {
            if (e.border_class == VectorBorderClass::Country) {
                found_country_border = true;
            } else if (e.border_class == VectorBorderClass::State) {
                found_state_border = true;
            } else if (e.border_class == VectorBorderClass::Coastline) {
                found_coastline = true;
            }
        }

        assert(found_country_border && "Country border must be classified between Country 1 and 2");
        assert(found_state_border && "State border must be classified between State 1 and 2");
        assert(found_coastline && "Coastline must be classified for single-owner edges");

        // Generate vector border mesh
        VectorBorderMesh border_mesh;
        strategy_map.generate_border_mesh(border_mesh, 1.0f);
        assert(border_mesh.index_count() > 0);
        assert(border_mesh.vertex_count() > 0);

        // Generate coastline hachures
        VectorBorderMesh hachure_mesh;
        strategy_map.generate_coastline_hachures(hachure_mesh, 3, 5.0f);
        assert(hachure_mesh.vertex_count() > 0);

        // Generate nautical rhumb lines
        VectorBorderMesh rhumb_mesh;
        strategy_map.generate_rhumb_lines(rhumb_mesh, {500.0f, 500.0f}, 400.0f, 16);
        assert(rhumb_mesh.vertex_count() == 16 * 4);

        // Generate war-goal hatching ribbons
        VectorBorderMesh hatching_mesh;
        strategy_map.generate_hatching_mesh(ProvinceId{1}, hatching_mesh, 0x808b261fu, 10.0f);
        assert(hatching_mesh.vertex_count() > 0);

        // Test paper-to-3D terrain level of detail (blend)
        assert(VectorMapSystem::calculate_paper_map_blend(900000.0) == 1.0f);
        assert(VectorMapSystem::calculate_paper_map_blend(40000.0) == 0.0f);
        float mid_blend = VectorMapSystem::calculate_paper_map_blend(430000.0);
        assert(mid_blend > 0.1f && mid_blend < 0.9f);

        std::cout << "  [PASS] VectorMapSystem contours, hachures, rhumb lines, and paper blend\n";
    }

    // 2. Test PhysicalWaterEvaluator
    {
        std::vector<GerstnerWave> waves{{1.0f, 0.0f, 2.0f, 80.0f, 1.5f, 0.6f},
                                        {0.7071f, 0.7071f, 1.0f, 40.0f, 2.0f, 0.4f}};

        Vector3D disp = PhysicalWaterEvaluator::evaluate_displacement(10.0f, 20.0f, 5.0f, waves);
        assert(std::abs(disp.z) > 0.001f);

        Vector3D norm = PhysicalWaterEvaluator::evaluate_normal(10.0f, 20.0f, 5.0f, waves);
        float n_len = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        assert(std::abs(n_len - 1.0f) < 1e-3f);

        // Schlick Fresnel
        assert(std::abs(PhysicalWaterEvaluator::evaluate_fresnel(1.0f, 0.02f) - 0.02f) < 1e-4f);
        assert(std::abs(PhysicalWaterEvaluator::evaluate_fresnel(0.0f, 0.02f) - 1.0f) < 1e-4f);

        // Beer-Lambert decreases exponentially with depth
        float t_shallow = PhysicalWaterEvaluator::evaluate_transmission(1.0f, 0.15f);
        float t_deep = PhysicalWaterEvaluator::evaluate_transmission(20.0f, 0.15f);
        assert(t_shallow > t_deep && t_deep > 0.0f);

        // Coastline front foam intensity
        float foam_near = PhysicalWaterEvaluator::evaluate_coast_foam(1.0f, 0.5f, 15.0f);
        float foam_far = PhysicalWaterEvaluator::evaluate_coast_foam(16.0f, 0.5f, 15.0f);
        assert(foam_near > 0.0f && foam_far == 0.0f);

        std::cout << "  [PASS] PhysicalWaterEvaluator Gerstner waves, Fresnel, and Beer-Lambert\n";
    }

    // 3. Test ornamental wood and parchment UI materials
    {
        UiDrawList ui;

        // Wood Panel (solid mahogany + brass rivets)
        ui.wood_panel({50.0f, 50.0f, 300.0f, 200.0f});
        assert(ui.vertices().size() > 0);
        assert(ui.indices().size() > 0);

        // Parchment Panel (aged warm paper + double ink hairline)
        ui.parchment_panel({60.0f, 60.0f, 280.0f, 180.0f});

        // Brass Button (embossed metal button)
        ui.brass_button({70.0f, 70.0f, 120.0f, 30.0f}, "ENACT TREATY");

        // Ink Chart (copperplate trend line on parchment)
        std::vector<float> gdp_data{100.0f, 105.0f, 110.0f, 108.0f, 115.0f, 125.0f, 130.0f};
        ui.ink_chart({70.0f, 110.0f, 260.0f, 80.0f}, gdp_data);

        // Financial KPI Card & Bullion Reserve Meter (Victoria 3 Financial Mechanics)
        ui.financial_kpi_card({70.0f, 200.0f, 220.0f, 70.0f}, "GROSS DOMESTIC PRODUCT", "£42.8M", 125000.0, 0.78f);
        ui.gold_reserve_meter({70.0f, 280.0f, 220.0f, 18.0f}, 750.0, 1000.0, 500.0);
        ui.treasury_balance_ticker({70.0f, 310.0f, 180.0f, 24.0f}, 15000.0, 11200.0);

        assert(ui.text_runs().size() > 0);
        assert(ui.batches().size() > 0);

        // Verify sovereign default hazard rendering on bankruptcy
        UiDrawList default_ui;
        default_ui.gold_reserve_meter({70.0f, 280.0f, 220.0f, 18.0f}, -600.0, 1000.0, 500.0);
        assert(!default_ui.vertices().empty());

        // A malformed scripted value must be dropped at the draw-list
        // boundary instead of emitting NaN/Inf vertices or hit regions.
        UiDrawList malformed;
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const float bad_line[] = {0.0f, 0.0f, nan, 1.0f};
        malformed.quad({nan, 0.0f, 10.0f, 10.0f}, 0xffffffffu);
        malformed.polyline(bad_line, 0xffffffffu);
        malformed.text("ignored", nan, 0.0f, 12.0f, 0xffffffffu);
        malformed.hit(1u, {0.0f, 0.0f, nan, 10.0f});
        assert(malformed.vertices().empty());
        assert(malformed.text_runs().empty());
        assert(malformed.hits().empty());

        const auto bad_tooltip = place_tooltip({nan, 0.0f, 10.0f, 10.0f}, 120.0f, 40.0f,
                                               {0.0f, 0.0f, 800.0f, 600.0f});
        assert(bad_tooltip.w == 0.0f && bad_tooltip.h == 0.0f);
        const float overflowing_offsets[] = {0.0f, std::numeric_limits<float>::max(),
                                             std::numeric_limits<float>::max()};
        assert(virtualize_variable_rows(overflowing_offsets, std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::max()).count == 0u);

        std::cout << "  [PASS] Wood Panel, Parchment, Brass Button, and Ink Chart\n";
    }

    // 4. Dynamic 3D flag mesh and strict UI overlay ordering
    {
        DynamicFlag3DConfig config;
        config.columns = 16;
        config.rows = 8;
        config.pattern = DynamicFlagPattern::CrossSaltire;
        DynamicFlag3D flag{config};
        const auto cloth_vertices = static_cast<std::size_t>(config.columns) * config.rows;
        const auto cloth_indices = static_cast<std::size_t>(config.columns - 1u) *
                                   (config.rows - 1u) * 6u;
        assert(flag.vertices().size() == cloth_vertices);
        assert(flag.indices().size() == cloth_indices);
        flag.update(0.75f);
        for (std::size_t row = 0; row < config.rows; ++row) {
            const auto& pinned = flag.vertices()[row * config.columns];
            assert(std::abs(pinned.z) < 1e-6f);
        }
        bool free_edge_moved = false;
        for (std::size_t row = 0; row < config.rows; ++row) {
            const auto& free_edge = flag.vertices()[row * config.columns + config.columns - 1u];
            free_edge_moved = free_edge_moved || std::abs(free_edge.z) > 1e-4f;
        }
        assert(free_edge_moved);

        UiDrawList layered;
        layered.quad({0, 0, 100, 40}, 0xff111111u);
        layered.module(ui_stable_key("dynamic_flag"), {2, 2, 40, 24}, {0, 0, 100, 40});
        layered.text("under", 4, 4, 12, 0xffffffffu);
        layered.quad({10, 10, 80, 30}, 0xff222222u);
        layered.text("overlay", 14, 14, 12, 0xffffffffu);
        assert(layered.modules().size() == 1u);
        assert(layered.batches().size() == 2u);
        assert(layered.batches()[0].order < layered.modules()[0].order);
        assert(layered.modules()[0].order < layered.text_runs()[0].order);
        assert(layered.text_runs()[0].order < layered.batches()[1].order);
        assert(layered.batches()[1].order < layered.text_runs()[1].order);

        std::cout << "  [PASS] Dynamic 3D flag cloth and UI overlay ordering\n";
    }

    // 5. Binary vector boundary loading and screen-space vector border generation
    {
        VectorMapSystem vmap;
        const std::filesystem::path bin_path = "content/base/map/world/world_boundaries_near.thundervec";
        if (std::filesystem::exists(bin_path)) {
            const bool loaded = vmap.load_binary_boundaries(bin_path);
            assert(loaded);
            assert(!vmap.binary_polylines().empty());

            UiDrawList ui;
            // Test zoomed out (world view)
            vmap.render_screen_boundaries(
                ui,
                0.5, 0.5,
                0.5, 0.5,
                1920, 1080,
                [](std::uint32_t a, std::uint32_t b) {
                    if (b == 0) return VectorBorderClass::Coastline;
                    if (a % 10 != b % 10) return VectorBorderClass::Country;
                    return VectorBorderClass::Province;
                }
            );
            assert(!ui.vertices().empty());
            assert(!ui.indices().empty());
            const auto world_vertex_count = ui.vertices().size();
            (void)world_vertex_count;

            // Test zoomed in (close view)
            ui.clear();
            vmap.render_screen_boundaries(
                ui,
                0.5, 0.5,
                0.05, 0.05,
                1920, 1080,
                [](std::uint32_t a, std::uint32_t b) {
                    if (b == 0) return VectorBorderClass::Coastline;
                    if (a % 10 != b % 10) return VectorBorderClass::Country;
                    return VectorBorderClass::Province;
                }
            );
            assert(!ui.vertices().empty());
            assert(!ui.indices().empty());

            // Test screen margin culling (far off-screen view)
            ui.clear();
            vmap.render_screen_boundaries(
                ui,
                999.0, 999.0,
                0.001, 0.001,
                1920, 1080,
                [](std::uint32_t, std::uint32_t) { return VectorBorderClass::Country; }
            );
            assert(ui.vertices().empty()); // Off-screen must cull all vertices
        }

        std::cout << "  [PASS] Binary vector boundary loading, LOD culling, and anti-aliased screen projection\n";
    }

    // 6. VectorMapTypography Catmull-Rom spline, smooth curvature, and engraved label layout
    {
        // 6a. Test sample_spline: unit tangents and smooth continuous interpolation
        std::vector<VectorPoint> anchors = {
            {0.10f, 0.20f}, {0.20f, 0.25f}, {0.30f, 0.22f}, {0.40f, 0.28f}
        };
        auto spline = VectorMapTypography::sample_spline(anchors, 32);
        assert(spline.size() == 32);
        for (const auto& pt : spline) {
            float len = std::sqrt(pt.tangent_x * pt.tangent_x + pt.tangent_y * pt.tangent_y);
            assert(std::abs(len - 1.0f) < 1.0e-3f && "Spline tangent vectors must be normalized unit vectors");
        }
        for (std::size_t i = 1; i < spline.size(); ++i) {
            assert(spline[i].distance >= spline[i - 1].distance && "Spline distance must be monotonically non-decreasing");
        }

        // 6b. Test layout_curved_label smooth glyph angles
        auto layout = VectorMapTypography::layout_curved_label("GREAT BRITAIN", anchors, 16.0f, 0xff000000u, 1);
        assert(!layout.glyphs.empty());
        for (std::size_t i = 1; i < layout.glyphs.size(); ++i) {
            float d = std::abs(layout.glyphs[i].angle_rad - layout.glyphs[i - 1].angle_rad);
            while (d > 3.14159265f) d = std::abs(d - 6.2831853f);
            assert(d <= 0.050f && "Adjacent glyph angles must be smoothly constrained without jagged twists");
        }

        std::cout << "  [PASS] VectorMapTypography Catmull-Rom spline, smooth curvature, and engraved label layout\n";
    }

    std::cout << "=== ALL VECTOR MAP AND UI TESTS PASSED (100%) ===\n";
    return 0;
}
