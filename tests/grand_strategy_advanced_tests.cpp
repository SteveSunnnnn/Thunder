#include "thunder/simulation/ai/StrategicAiPlanner.hpp"
#include "thunder/content/localization/LocalizationStore.hpp"
#include "thunder/presentation/render/map/VectorMapTypography.hpp"
#include "thunder/presentation/render/map/MapDecorationRenderer.hpp"
#include "thunder/presentation/render/vfx/LivingMapVfxSystem.hpp"
#include "thunder/simulation/warfare/BattlePhaseSystem.hpp"
#include "thunder/simulation/warfare/LogisticsNetwork.hpp"
#include <cassert>
#include <iostream>

using namespace thunder;

int main() {
    std::cout << "[Advanced Grand Strategy Tests Starting]...\n";

    // 1. Test LocalizationStore (Script dictionary, scope formatting, and rich text parser)
    {
        LocalizationStore loc;
        loc.set_language("en");
        loc.add_entry("en", "TREATY_SIGNED", "The [Root.GetName] has ratified treaty with [Target.GetName]!");
        loc.add_entry("zh", "TREATY_SIGNED", "[Root.GetName] 与 [Target.GetName] 签署了互不侵犯条约！");

        std::map<std::string, std::string> scopes{
            {"Root.GetName", "Kingdom of Prussia"},
            {"Target.GetName", "Austrian Empire"}
        };

        std::string res_en = loc.format("TREATY_SIGNED", scopes);
        assert(res_en == "The Kingdom of Prussia has ratified treaty with Austrian Empire!");

        loc.set_language("zh");
        std::string res_zh = loc.format("TREATY_SIGNED", scopes);
        assert(res_zh == "Kingdom of Prussia 与 Austrian Empire 签署了互不侵犯条约！");

        // Test rich text lexer with colors, bold, and icons
        std::string_view rich_sample = "Producing [color:gold]500[/color] [icon:grain] and [b]Iron[/b]";
        auto tokens = LocalizationStore::parse_rich_text(rich_sample);
        assert(tokens.size() >= 4);

        bool found_gold = false;
        bool found_icon = false;
        bool found_bold = false;

        for (const auto& t : tokens) {
            if (t.rgba == 0xffd4af37u && t.text == "500") found_gold = true;
            if (t.is_icon && t.icon_id == "grain") found_icon = true;
            if (t.is_bold && t.text == "Iron") found_bold = true;
        }
        assert(found_gold && found_icon && found_bold);

        std::cout << "  [PASS] LocalizationStore dictionaries, scopes, and rich text parsing\n";
    }

    // 2. Test StrategicAiPlanner (Script-driven parameters, construction queue, and balance-of-power coalitions)
    {
        StrategicAiPlanner ai;
        CountryId c1{1};
        AiStrategyParameters params;
        params.industrial_focus_ppm = 800'000;
        params.balance_of_power_sensitivity_ppm = 900'000;
        ai.set_country_strategy(c1, params);

        std::vector<ProvinceId> provinces{ProvinceId{10}, ProvinceId{11}};
        std::vector<std::pair<std::uint32_t, std::int32_t>> shortages{
            {0x1234u, 20}, // 20 units grain deficit
            {0x5678u, 50}  // 50 units steel deficit
        };

        auto proposals = ai.evaluate_construction_queue(c1, provinces, shortages);
        assert(proposals.size() == 2);
        // Steel (50 deficit) should be prioritized first
        assert(proposals[0].estimated_roi_ppm > proposals[1].estimated_roi_ppm);

        // Anti-hegemonic balance-of-power coalition test
        CountryId hegemon{99};
        std::vector<std::pair<CountryId, std::int32_t>> neighbors{
            {CountryId{2}, 50000},
            {CountryId{3}, 40000}
        };

        // Aggressor with 60 infamy and 80k military power
        auto coalition_opt = ai.evaluate_balance_of_power_coalition(c1, hegemon, 60, 80000, neighbors);
        assert(coalition_opt.has_value());
        assert(coalition_opt->proposed_members.size() == 3);
        assert(coalition_opt->is_activated == true);

        std::cout << "  [PASS] StrategicAiPlanner script parameters, construction, and coalitions\n";
    }

    // 3. Test VectorMapTypography & MapDecorationRenderer
    {
        std::vector<VectorPoint> spline_pts{{100.0f, 100.0f}, {200.0f, 150.0f}, {300.0f, 120.0f}, {400.0f, 180.0f}};
        auto layout = VectorMapTypography::layout_curved_label("BRITISH EMPIRE", spline_pts, 18.0f, 0xffd4af37u, 10);
        assert(layout.glyphs.size() == 14);
        assert(layout.aabb.w > 0.0f && layout.aabb.h > 0.0f);

        // Test label collision pruning
        std::vector<CurvedLabelLayout> labels{layout};
        CurvedLabelLayout low_prio = layout;
        low_prio.priority = 1;
        labels.push_back(low_prio);

        VectorMapTypography::prune_collisions(labels);
        assert(labels[0].is_visible == true);
        assert(labels[1].is_visible == false); // lower priority pruned

        // Test tabletop frame and compass rose
        UiDrawList ui;
        MapDecorationRenderer::render_tabletop_wood_frame(ui, {0.0f, 0.0f, 1920.0f, 1080.0f});
        MapDecorationRenderer::render_brass_compass_rose(ui, 500.0f, 500.0f, 50.0f);
        MapDecorationRenderer::render_corner_vignettes(ui, {0.0f, 0.0f, 1920.0f, 1080.0f});
        MapDecorationRenderer::render_ornate_title_cartouche(ui, 960.0f, 20.0f, "VICTORIA", "ANNO DOMINI 1836");

        assert(ui.vertices().size() > 0);
        assert(ui.batches().size() > 0);
        assert(ui.text_runs().size() >= 10); // Compass rose (N, E, S, W, NE, SE, SW, NW) + Cartouche title/subtitle

        // Check compass wind labels are emitted
        bool found_ne = false, found_sw = false;
        for (const auto& run : ui.text_runs()) {
            if (run.utf8 == "NE") found_ne = true;
            if (run.utf8 == "SW") found_sw = true;
        }
        assert(found_ne && found_sw);

        // Test all 4 corners of render_corner_vignettes (previously 3 corners were silently dropped)
        UiDrawList corners_ui;
        MapDecorationRenderer::render_corner_vignettes(corners_ui, {0.0f, 0.0f, 1920.0f, 1080.0f}, 40.0f);
        int tl_count = 0, tr_count = 0, bl_count = 0, br_count = 0;
        for (const auto& v : corners_ui.vertices()) {
            if (v.x < 100.0f && v.y < 100.0f) ++tl_count;
            else if (v.x > 1820.0f && v.y < 100.0f) ++tr_count;
            else if (v.x < 100.0f && v.y > 980.0f) ++bl_count;
            else if (v.x > 1820.0f && v.y > 980.0f) ++br_count;
        }
        assert(tl_count >= 30 && "Top-left corner must emit bracket/acanthus geometry");
        assert(tr_count >= 30 && "Top-right corner must emit bracket/acanthus geometry");
        assert(bl_count >= 30 && "Bottom-left corner must emit bracket/acanthus geometry");
        assert(br_count >= 30 && "Bottom-right corner must emit bracket/acanthus geometry");

        // Test ui_estimate_text_width: ASCII, CJK, and multi-line max line width
        assert(ui_estimate_text_width("", 16.0f) == 0.0f);
        float w_ascii = ui_estimate_text_width("EMPIRE", 16.0f);
        assert(w_ascii > 0.0f);
        float w_cjk = ui_estimate_text_width("大英帝国", 16.0f);
        assert(w_cjk > w_ascii * 0.8f);
        float w_multiline = ui_estimate_text_width("Short\nMuch longer second line", 16.0f);
        float w_line2 = ui_estimate_text_width("Much longer second line", 16.0f);
        assert(std::abs(w_multiline - w_line2) < 1e-3f && "Multi-line text width must equal max line width");

        // Test CJK UTF-8 cartouche centering and rendering
        UiDrawList cjk_ui;
        MapDecorationRenderer::render_ornate_title_cartouche(cjk_ui, 960.0f, 20.0f, "大英帝国", "维多利亚时代");
        assert(cjk_ui.text_runs().size() >= 4);
        assert(cjk_ui.text_runs()[1].utf8 == "大英帝国");
        assert(cjk_ui.text_runs()[3].utf8 == "维多利亚时代");
        assert(cjk_ui.text_runs()[1].centered == true && "Cartouche title must use runtime MSDF centered layout");

        // Test cartouche without subtitle (centered vertically)
        UiDrawList no_sub_ui;
        MapDecorationRenderer::render_ornate_title_cartouche(no_sub_ui, 960.0f, 20.0f, "VICTORIA", "");
        assert(no_sub_ui.text_runs().size() == 2 && "Cartouche without subtitle must emit only title runs");
        assert(no_sub_ui.text_runs()[0].centered == true);

        // Test dynamic cartouche width auto-expansion for long empire title
        UiDrawList long_ui;
        MapDecorationRenderer::render_ornate_title_cartouche(long_ui, 960.0f, 20.0f,
            "THE UNITED KINGDOM OF GREAT BRITAIN AND IRELAND", "EMPIRE UPON WHICH THE SUN NEVER SETS");
        assert(long_ui.text_runs().size() >= 4);
        assert(long_ui.text_runs()[1].utf8 == "THE UNITED KINGDOM OF GREAT BRITAIN AND IRELAND");
        assert(long_ui.vertices().size() > 0);

        // Test quad_points_colors primitive
        UiDrawList poly_ui;
        poly_ui.quad_points_colors(0.0f, 0.0f, 0xff000000u, 10.0f, 0.0f, 0x80000000u,
                                   10.0f, 10.0f, 0x00000000u, 0.0f, 10.0f, 0x40000000u);
        assert(poly_ui.vertices().size() == 4);
        assert(poly_ui.indices().size() == 6);
        assert(poly_ui.vertices()[0].rgba == 0xff000000u);
        assert(poly_ui.vertices()[2].rgba == 0x00000000u);

        // Test financial UI: gold_reserve_meter and treasury_balance_ticker with custom currency
        UiDrawList fin_ui;
        fin_ui.gold_reserve_meter({10.0f, 10.0f, 200.0f, 20.0f}, 75000.0, 100000.0, 50000.0);
        fin_ui.gold_reserve_meter({10.0f, 40.0f, 200.0f, 20.0f}, -25000.0, 100000.0, 50000.0);
        fin_ui.treasury_balance_ticker({10.0f, 70.0f, 150.0f, 24.0f}, 12500.0, 8300.0, {}, "$");
        fin_ui.gauge_balance({10.0f, 100.0f, 200.0f, 30.0f}, 150.0f, 100.0f);
        fin_ui.brass_button({10.0f, 140.0f, 120.0f, 30.0f}, "CONFIRM ORDER", false);
        fin_ui.brass_button({10.0f, 180.0f, 120.0f, 30.0f}, "CONFIRM ORDER", true);

        // Test brass button with CJK label centering
        UiDrawList cjk_btn_ui;
        cjk_btn_ui.brass_button({10.0f, 10.0f, 120.0f, 30.0f}, "确认", false);
        assert(cjk_btn_ui.text_runs().size() == 2);
        assert(cjk_btn_ui.text_runs()[0].centered == true && "Wide brass button must center CJK label");
        assert(cjk_btn_ui.text_runs()[1].centered == true);

        assert(fin_ui.vertices().size() > 0);
        assert(!fin_ui.text_runs().empty());
        bool found_dollar = false;
        for (const auto& run : fin_ui.text_runs()) {
            if (run.utf8.find('$') != std::string::npos) found_dollar = true;
        }
        assert(found_dollar);

        // Test engraved map labels (Victoria 3 permanent geographic map etching)
        std::vector<WorldMapLabelPoint> spine_uv{
            {0.40f, 0.50f}, {0.45f, 0.51f}, {0.50f, 0.52f}, {0.55f, 0.51f}, {0.60f, 0.50f}};
        auto engraved = VectorMapTypography::layout_engraved_country(
            "BRITISH EMPIRE", spine_uv, 21'494'337.0, 0xf2221d19u);
        assert(engraved.glyphs.size() == 14);
        assert(engraved.glyphs[0].font_size_uv > 0.0f);
        for (const auto& g : engraved.glyphs) {
            assert(g.u >= 0.35f && g.u <= 0.65f);
            assert(g.v >= 0.45f && g.v <= 0.55f);
        }

        UiDrawList engraved_ui;
        std::vector<EngravedMapLabel> engraved_list{engraved};
        VectorMapTypography::render_engraved_labels(
            engraved_ui, engraved_list, 0.5, 0.5, 0.5, 0.5, 1920, 1080, false);
        assert(engraved_ui.text_runs().size() == 14);
        const float standard_font_size = engraved_ui.text_runs()[0].size;
        assert(standard_font_size > 5.0f);

        // Test exact geographic zoom scaling: zooming in 2x (half_v halved) doubles the screen font size
        UiDrawList zoomed_ui;
        VectorMapTypography::render_engraved_labels(
            zoomed_ui, engraved_list, 0.5, 0.5, 0.25, 0.25, 1920, 1080, false);
        assert(zoomed_ui.text_runs().size() == 14);
        const float zoomed_font_size = zoomed_ui.text_runs()[0].size;
        assert(std::abs(zoomed_font_size - standard_font_size * 2.0f) < 0.1f);

        // Test frustum culling when camera is completely away from label
        UiDrawList culled_ui;
        VectorMapTypography::render_engraved_labels(
            culled_ui, engraved_list, 0.05, 0.05, 0.01, 0.01, 1920, 1080, false);
        assert(culled_ui.text_runs().empty());

        // Test horizontal wrap handling
        UiDrawList wrapped_ui;
        VectorMapTypography::render_engraved_labels(
            wrapped_ui, engraved_list, 1.5, 0.5, 0.5, 0.5, 1920, 1080, true);
        assert(wrapped_ui.text_runs().size() == 14);

        std::cout << "  [PASS] VectorMapTypography curved text, collision pruning, and engraved map labels\n";
    }

    // 4. Test LogisticsNetwork & BattlePhaseSystem
    {
        LogisticsNetwork log;
        log.add_supply_hub({ProvinceId{1}, CountryId{1}, 1000, 1000, 3});
        log.add_connection({ProvinceId{1}, ProvinceId{2}, 500, true, 1.0f}); // maritime connection to province 2

        float sup_init = log.calculate_frontline_supply_factor(ProvinceId{2});
        assert(sup_init > 0.8f);

        // Apply convoy raiding on sea node
        log.apply_convoy_raiding(ProvinceId{1}, 0.5f);
        float sup_raided = log.calculate_frontline_supply_factor(ProvinceId{2});
        assert(sup_raided < sup_init);

        // Test multi-phase battle progression
        BattlePhaseSystem battle_sys;
        BattleState battle;
        battle.battle_id = 1;
        battle.location = ProvinceId{2};
        battle.attacker = CountryId{1};
        battle.defender = CountryId{2};

        assert(battle.phase == BattlePhase::Reconnaissance);
        for (int day = 0; day < 12 && !battle.is_concluded; ++day) {
            battle_sys.advance_battle_day(battle, sup_raided, 1.0f);
        }
        assert(battle.phase_days_elapsed >= 0);
        assert(battle.attacker_manpower < 10000 || battle.defender_manpower < 10000);

        std::cout << "  [PASS] LogisticsNetwork convoy raiding and multi-phase battle tactics\n";
    }

    // 5. Test LivingMapVfxSystem
    {
        LivingMapVfxSystem vfx;
        vfx.spawn_train(100.0f, 100.0f, 300.0f, 300.0f);
        vfx.spawn_ship(500.0f, 500.0f, 700.0f, 500.0f);

        vfx.update(0.5f);
        assert(vfx.particle_count() > 0);

        UiDrawList ui;
        vfx.render(ui);
        assert(ui.vertices().size() > 0);

        std::cout << "  [PASS] LivingMapVfxSystem trains, ships, and trailing particle wakes\n";
    }

    std::cout << "=== ALL ADVANCED GRAND STRATEGY ENGINE TESTS PASSED (100%) ===\n";
    return 0;
}
