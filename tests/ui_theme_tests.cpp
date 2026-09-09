#include "thunder/presentation/ui/StrategyUi.hpp"
#include "thunder/presentation/ui/TooltipStack.hpp"
#include "thunder/presentation/ui/UiTheme.hpp"

#include <cassert>
#include <iostream>
#include <limits>

using namespace thunder;

namespace {

[[nodiscard]] bool contains_vertex_color(const UiDrawList& list, std::uint32_t rgba) {
    for (const auto& vertex : list.vertices()) {
        if (vertex.rgba == rgba) return true;
    }
    return false;
}

[[nodiscard]] bool contains_translucent_black(const UiDrawList& list) {
    for (const auto& vertex : list.vertices()) {
        const std::uint32_t alpha = vertex.rgba >> 24u;
        if ((vertex.rgba & 0x00ffffffu) == 0u && alpha > 0u && alpha < 0xf0u) return true;
    }
    return false;
}

[[nodiscard]] bool contains_text(const UiDrawList& list, const std::string& utf8) {
    for (const auto& run : list.text_runs()) {
        if (run.utf8 == utf8) return true;
    }
    return false;
}

[[nodiscard]] bool contains_text_color(const UiDrawList& list, std::uint32_t rgba) {
    for (const auto& run : list.text_runs()) {
        if (run.rgba == rgba) return true;
    }
    return false;
}

} // namespace

void test_default_theme_resolves() {
    UiDrawList list;
    assert(&list.theme() == &UiTheme::victorian());
    UiTheme custom;
    custom.colors.bg_panel = 0xff112233u;
    list.set_theme(&custom);
    assert(&list.theme() == &custom);
    list.set_theme(nullptr);
    assert(&list.theme() == &UiTheme::victorian());
    std::cout << "test_default_theme_resolves [PASS]\n";
}

void test_materials_follow_theme_tokens() {
    const auto& t = UiTheme::victorian();
    UiDrawList list;
    list.wood_panel({0, 0, 200, 200});
    assert(contains_vertex_color(list, t.materials.wood_bevel));
    assert(contains_vertex_color(list, t.materials.wood_filigree));
    list.clear();
    list.parchment_panel({0, 0, 200, 200});
    assert(contains_vertex_color(list, t.materials.parchment_margin));
    assert(contains_vertex_color(list, t.materials.parchment_sheet));
    list.clear();
    list.leather_panel({0, 0, 200, 200});
    assert(contains_vertex_color(list, t.materials.leather_base));
    list.clear();
    list.progress_bar({0, 0, 100, 10}, 0.5f);
    assert(contains_vertex_color(list, t.colors.gold));
    assert(contains_vertex_color(list, t.colors.bg_deep));
    std::cout << "test_materials_follow_theme_tokens [PASS]\n";
}

void test_custom_theme_overrides_materials() {
    UiTheme custom;
    custom.materials.parchment_margin = 0xff00ff00u;
    UiDrawList list;
    list.set_theme(&custom);
    list.parchment_panel({0, 0, 200, 200});
    assert(contains_vertex_color(list, 0xff00ff00u));
    std::cout << "test_custom_theme_overrides_materials [PASS]\n";
}

void test_gradient_and_ornaments() {
    UiDrawList list;
    list.v_gradient({0, 0, 40, 30}, 0xffffffffu, 0xff000000u, 6);
    // Smooth gradients are one interpolated quad, not stacked bands
    assert(list.batches().size() == 1);
    assert(list.vertices().size() == 4);
    list.clear();
    list.quad_gradient({0, 0, 40, 30}, 0xff112233u, 0xff445566u, true);
    assert(list.vertices()[0].rgba == 0xff112233u);
    assert(list.vertices()[2].rgba == 0xff445566u);
    list.clear();
    list.quad_gradient({0, 0, 40, 30}, 0xff112233u, 0xff445566u, false);
    assert(list.vertices()[1].rgba == 0xff112233u);
    assert(list.vertices()[2].rgba == 0xff445566u);
    list.clear();
    list.drop_shadow({0, 0, 40, 30}, 0x80000000u, 4.0f, 1.0f, 2.0f);
    assert(list.vertices().size() >= 12); // stacked penumbra layers
    list.clear();
    list.corner_ornaments({0, 0, 100, 100});
    assert(list.vertices().size() == 4 * 8);
    list.clear();
    list.divider_ornament({0, 0, 200, 8});
    assert(!list.vertices().empty());
    list.clear();
    list.separator({0, 0, 200, 2});
    assert(!list.vertices().empty());
    std::cout << "test_gradient_and_ornaments [PASS]\n";
}

void test_header_and_window_frame() {
    const auto& t = UiTheme::victorian();
    UiDrawList list;
    list.ornate_header({0, 0, 300, 30}, "Window Title");
    assert(contains_vertex_color(list, t.colors.border_gold));
    assert(contains_text(list, "Window Title"));
    assert(contains_text_color(list, t.colors.text_gold));
    list.clear();
    list.window_frame({0, 0, 400, 300}, "Frame");
    assert(contains_text(list, "Frame"));
    assert(contains_vertex_color(list, t.colors.bg_panel));
    assert(contains_translucent_black(list)); // soft penumbra layers
    std::cout << "test_header_and_window_frame [PASS]\n";
}

void test_tab_states_are_distinct() {
    const auto& t = UiTheme::victorian();
    UiDrawList active;
    active.tab({0, 0, 80, 26}, "Active", true, false);
    assert(contains_vertex_color(active, t.colors.border_gold));
    assert(contains_text_color(active, t.colors.text_gold));
    UiDrawList inactive;
    inactive.tab({0, 0, 80, 26}, "Idle", false, false);
    assert(contains_vertex_color(inactive, t.colors.bg_panel_recessed));
    assert(!contains_vertex_color(inactive, t.colors.border_gold));
    assert(contains_text_color(inactive, t.colors.text_secondary));
    std::cout << "test_tab_states_are_distinct [PASS]\n";
}

void test_form_controls() {
    const auto& t = UiTheme::victorian();
    UiDrawList list;
    list.dropdown_row({0, 0, 160, 24}, "Option", false, true, false);
    assert(contains_text_color(list, t.colors.text_gold));
    list.clear();
    list.checkbox({0, 0, 14, 14}, true, false, false);
    assert(contains_vertex_color(list, t.colors.gold));
    list.clear();
    list.radio({0, 0, 14, 14}, true, false, false);
    assert(contains_vertex_color(list, t.colors.gold));
    list.clear();
    list.slider({0, 0, 120, 16}, 0.4f, false);
    assert(contains_vertex_color(list, t.colors.gold));
    assert(contains_vertex_color(list, t.colors.bg_deep));
    list.clear();
    list.scrollbar({0, 0, 8, 200}, 0.25f, 0.3f, false);
    assert(contains_vertex_color(list, t.colors.border_normal));
    list.clear();
    list.input_box({0, 0, 120, 24}, "abc", true);
    assert(contains_vertex_color(list, t.colors.state_focus));
    assert(contains_text(list, "abc"));
    std::cout << "test_form_controls [PASS]\n";
}

void test_data_rows_and_stats() {
    const auto& t = UiTheme::victorian();
    UiDrawList list;
    list.list_row({0, 0, 300, 24}, "Primary", "Secondary", "1,250", false, true, false);
    assert(contains_vertex_color(list, t.colors.state_selected));
    assert(contains_text(list, "Primary"));
    assert(contains_text(list, "1,250"));
    list.clear();
    list.table_header_cell({0, 0, 100, 22}, "Column", true);
    assert(contains_vertex_color(list, t.colors.border_gold));
    assert(contains_text(list, "Column"));
    list.clear();
    list.stat_row({0, 0, 300, 24}, "Treasury", "12,500", "+250");
    assert(contains_text_color(list, t.colors.text_positive));
    list.clear();
    list.stat_row({0, 0, 300, 24}, "Debt", "9,000", "-100");
    assert(contains_text_color(list, t.colors.text_negative));
    std::cout << "test_data_rows_and_stats [PASS]\n";
}

void test_notification_severities() {
    const auto& t = UiTheme::victorian();
    UiDrawList critical;
    critical.notification_card({0, 0, 260, 60}, "War", "Body", 0);
    assert(contains_vertex_color(critical, t.colors.text_negative));
    UiDrawList info;
    info.notification_card({0, 0, 260, 60}, "Note", "Body", 4);
    assert(contains_vertex_color(info, t.colors.border_normal));
    assert(!contains_vertex_color(info, t.colors.text_negative));
    std::cout << "test_notification_severities [PASS]\n";
}

void test_modal_window() {
    const auto& t = UiTheme::victorian();
    UiDrawList list;
    list.modal_window({0, 0, 400, 300}, "Event Title", "Something happened.");
    assert(contains_text(list, "Event Title"));
    assert(contains_text(list, "Something happened."));
    assert(contains_vertex_color(list, t.materials.parchment_sheet));
    assert(contains_vertex_color(list, t.materials.wax_base));
    std::cout << "test_modal_window [PASS]\n";
}

void test_number_formatting() {
    assert(ui_format_number(0.0) == "0");
    assert(ui_format_number(1234.0) == "1,234");
    assert(ui_format_number(-1234567.0) == "-1,234,567");
    assert(ui_format_number(1234.56, 1) == "1,234.6");
    assert(ui_format_number(12500.0, 0, true) == "12.5K");
    assert(ui_format_number(2500000.0, 0, true) == "2.5M");
    assert(ui_format_number(1500000000.0, 0, true) == "1.5B");
    assert(ui_format_number(999.0, 0, true) == "999");
    assert(ui_format_delta(250.0) == "+250");
    assert(ui_format_delta(-32.0) == "-32");
    assert(ui_format_delta(0.0) == "0");
    std::cout << "test_number_formatting [PASS]\n";
}

void test_color_blend_and_delta_color() {
    const auto& t = UiTheme::victorian();
    assert(ui_blend(0xff000000u, 0xffffffffu, 0.0f) == 0xff000000u);
    assert(ui_blend(0xff000000u, 0xffffffffu, 1.0f) == 0xffffffffu);
    assert(ui_blend(0xff000000u, 0xffffffffu, 0.5f) == 0xff808080u);
    assert(ui_apply_overlay(0xff000000u, 0x00ffffffu) == 0xff000000u);
    assert(ui_delta_color(t, 5.0) == t.colors.text_positive);
    assert(ui_delta_color(t, -5.0) == t.colors.text_negative);
    assert(ui_delta_color(t, 0.0) == t.colors.text_secondary);
    std::cout << "test_color_blend_and_delta_color [PASS]\n";
}

void test_tooltip_renders_with_theme() {
    const auto& t = UiTheme::victorian();
    TooltipStack stack;
    stack.push_root("Title", "Body text", {0, 0, 10, 10}, {0, 0, 1920, 1080});
    UiDrawList list;
    stack.render(list, {0, 0, 1920, 1080});
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, t.materials.parchment_sheet));
    assert(contains_text_color(list, t.colors.text_gold));
    std::cout << "test_tooltip_renders_with_theme [PASS]\n";
}

void test_degenerate_inputs_emit_nothing() {
    UiDrawList list;
    list.v_gradient({0, 0, 0, 0}, 0xffffffffu, 0xff000000u, 6);
    list.corner_ornaments({0, 0, 4, 4});
    list.ornate_header({0, 0, 0, 0}, "x");
    list.window_frame({0, 0, -1, 10}, "x");
    list.tab({0, 0, 0, 0}, "x", true);
    assert(list.vertices().empty());
    // Non-finite fractions clamp instead of poisoning geometry
    list.slider({0, 0, 100, 10}, std::numeric_limits<float>::quiet_NaN());
    list.scrollbar({0, 0, 8, 100}, std::numeric_limits<float>::quiet_NaN(), 2.0f);
    assert(!list.vertices().empty());
    std::cout << "test_degenerate_inputs_emit_nothing [PASS]\n";
}

void test_financial_and_monetary_components() {
    const auto& t = UiTheme::victorian();

    // 1. Currency formatting
    assert(ui_format_currency(1250.0, "£", false) == "£1,250");
    assert(ui_format_currency(1234.56, "£", false, 2) == "£1,234.56");
    assert(ui_format_currency(-34000.0, "£", true) == "-£34.0K");
    assert(ui_format_currency(12500000.0, "$", true) == "$12.5M");
    assert(ui_format_currency(1500000000000.0, "£", true) == "£1.5T");
    assert(ui_format_currency(2250000000000000.0, "£", true) == "£2.2Q");
    assert(ui_format_currency(45000.0, "£", true, 1, true) == "+£45.0K");
    assert(ui_format_currency(-0.0001, "£", true, 0) == "£0");
    assert(ui_format_currency(std::numeric_limits<double>::quiet_NaN()) == "—");

    // 2. Financial KPI Card
    UiDrawList list;
    list.financial_kpi_card({0, 0, 200, 100}, "TREASURY", "£1.2M", 4500.0, 0.65f);
    assert(!list.vertices().empty());
    assert(contains_text(list, "TREASURY"));
    assert(contains_text(list, "£1.2M"));
    assert(contains_text_color(list, t.colors.text_gold));
    assert(contains_text_color(list, t.colors.text_positive));

    // 3. Gold Reserve Meter (Bullion mode, saturation, and Debt / Bankruptcy mode)
    list.clear();
    list.gold_reserve_meter({0, 0, 200, 20}, 500.0, 1000.0);
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, t.colors.gold_reserve));
    assert(contains_vertex_color(list, t.colors.gold_reserve_dark));

    // Reserve saturation overflow gleam
    list.clear();
    list.gold_reserve_meter({0, 0, 200, 20}, 1200.0, 1000.0);
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, 0xffffffffu));

    // Normal debt within limit
    list.clear();
    list.gold_reserve_meter({0, 0, 200, 20}, -400.0, 1000.0, 500.0);
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, t.colors.fiscal_deficit));
    assert(contains_vertex_color(list, t.colors.debt_ceiling_track));

    // Sovereign Default / Bankruptcy Hazard state (debt exceeds credit limit)
    list.clear();
    list.gold_reserve_meter({0, 0, 200, 20}, -600.0, 1000.0, 500.0);
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, 0xc0d92b2bu)); // Hazard hatching
    assert(contains_vertex_color(list, 0xffeed065u)); // Default alert marker

    // 4. Treasury Balance Ticker (Surplus & Deficit)
    list.clear();
    list.treasury_balance_ticker({0, 0, 160, 24}, 12000.0, 9500.0);
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, t.colors.fiscal_surplus));
    assert(contains_text(list, "+£2,500"));

    list.clear();
    list.treasury_balance_ticker({0, 0, 160, 24}, 50000.0, 25000.0);
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, t.colors.fiscal_surplus));
    assert(contains_text(list, "+£25.0K"));

    list.clear();
    list.treasury_balance_ticker({0, 0, 160, 24}, 5000.0, 8500.0);
    assert(!list.vertices().empty());
    assert(contains_vertex_color(list, t.colors.fiscal_deficit));
    assert(contains_text(list, "-£3,500"));

    std::cout << "test_financial_and_monetary_components [PASS]\n";
}

int main() {
    test_default_theme_resolves();
    test_materials_follow_theme_tokens();
    test_custom_theme_overrides_materials();
    test_gradient_and_ornaments();
    test_header_and_window_frame();
    test_tab_states_are_distinct();
    test_form_controls();
    test_data_rows_and_stats();
    test_notification_severities();
    test_modal_window();
    test_number_formatting();
    test_color_blend_and_delta_color();
    test_tooltip_renders_with_theme();
    test_degenerate_inputs_emit_nothing();
    test_financial_and_monetary_components();
    std::cout << "UI theme regression tests passed\n";
    return 0;
}
