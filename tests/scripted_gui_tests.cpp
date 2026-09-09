#include "thunder/presentation/ui/ScriptedGui.hpp"
#include "thunder/presentation/ui/ScriptedGuiPainter.hpp"
#include "thunder/presentation/ui/ScriptedGuiRuntime.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] bool has_diagnostic(const thunder::ScriptedGuiCompileResult& result,
                                  thunder::ScriptedGuiDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const auto& diagnostic) { return diagnostic.code == code; });
}

[[nodiscard]] const thunder::CompiledUiNode* find_node(const thunder::ScriptedGuiBlueprint& blueprint,
                                                     thunder::UiWidgetKind kind) {
    const auto found = std::find_if(blueprint.nodes().begin(), blueprint.nodes().end(),
                                    [kind](const auto& node) { return node.kind == kind; });
    return found == blueprint.nodes().end() ? nullptr : &*found;
}

} // namespace

int main() {
    thunder::SymbolTable symbols;
    thunder::ScriptedGuiSchema schema{symbols};
    const auto country = schema.register_context("country");
    const auto state = schema.register_context("state");
    (void)schema.register_property(country, "show_budget", thunder::UiValueType::Boolean);
    (void)schema.register_property(country, "can_spend", thunder::UiValueType::Boolean);
    (void)schema.register_property(country, "budget", thunder::UiValueType::Number);
    (void)schema.register_property(country, "display_name", thunder::UiValueType::Text);
    (void)schema.register_property(country, "flag", thunder::UiValueType::Asset);
    (void)schema.register_property(country, "states", thunder::UiValueType::Collection, state);
    (void)schema.register_property(country, "capital", thunder::UiValueType::Entity, state);
    (void)schema.register_property(country, "gdp_history", thunder::UiValueType::NumberSeries);
    (void)schema.register_property(country, "year_labels", thunder::UiValueType::TextSeries);
    (void)schema.register_property(state, "name", thunder::UiValueType::Text);
    (void)schema.register_command("open_budget");

    bool rejected_invalid_schema = false;
    try {
        (void)schema.register_property(country, "bad_scalar", thunder::UiValueType::Number, state);
    } catch (const std::invalid_argument&) {
        rejected_invalid_schema = true;
    }
    assert(rejected_invalid_schema);

    const std::string template_source = R"(
ui_template state_row {
    context = state
    root = {
        type = row
        id = row_root
        gap = 4
        children = {
            label = {
                id = state_name
                text = { bind = name }
            }
        }
    }
}
)";
    const std::string screen_source = R"(
scripted_gui country_overview {
    context = country
    root = {
        type = column
        id = overview_root
        gap = 8
        children = {
            label = {
                id = capital_name
                text = { bind = capital.name }
            }
            button = {
                id = budget_button
                text = budget_button_label
                icon = icons/budget
                visible = { bind = show_budget }
                enabled = { bind = can_spend }
                command = open_budget
                tooltip = budget_button_tooltip
            }
            list = {
                id = state_list
                items = { bind = states }
                item_template = state_row
                row_height = 28
                overscan = 3
                virtualized = yes
            }
            grid = {
                id = state_grid
                items = { bind = states }
                item_template = state_row
                columns = 4
                row_height = 36
                column_gap = 6
                row_gap = 5
                overscan = 2
            }
            chart = {
                id = growth_chart
                series = { bind = gdp_history }
                labels = { bind = year_labels }
                chart_kind = area
                max_points = 1024
                include_zero = yes
            }
            progress = {
                id = budget_progress
                value = { bind = budget }
            }
        }
    }
}
)";

    thunder::ScriptedGuiCompiler compiler{symbols, schema};
    const auto valid = compiler.compile(template_source + screen_source, "valid_gui.thunder");
    if (!valid.ok()) {
        for (const auto& diagnostic : valid.diagnostics)
            std::cerr << diagnostic.line << ':' << diagnostic.column << ' ' << diagnostic.message << '\n';
    }
    assert(valid.ok());
    assert(valid.blueprint.templates().size() == 1u);
    assert(valid.blueprint.screens().size() == 1u);
    assert(valid.blueprint.find_template(thunder::ui_stable_key("state_row")) != nullptr);
    assert(valid.blueprint.find_screen(thunder::ui_stable_key("country_overview")) != nullptr);
    assert(valid.blueprint.nodes().size() == 9u);
    assert(valid.blueprint.lists().size() == 1u);
    assert(valid.blueprint.grids().size() == 1u);
    assert(valid.blueprint.charts().size() == 1u);

    const auto* list_node = find_node(valid.blueprint, thunder::UiWidgetKind::List);
    const auto* grid_node = find_node(valid.blueprint, thunder::UiWidgetKind::Grid);
    const auto* chart_node = find_node(valid.blueprint, thunder::UiWidgetKind::Chart);
    const auto* button_node = find_node(valid.blueprint, thunder::UiWidgetKind::Button);
    assert(list_node != nullptr && grid_node != nullptr && chart_node != nullptr && button_node != nullptr);
    const auto& list = valid.blueprint.lists()[list_node->auxiliary_index];
    const auto& grid = valid.blueprint.grids()[grid_node->auxiliary_index];
    const auto& chart = valid.blueprint.charts()[chart_node->auxiliary_index];
    assert(list.items.valid() && list.item_template.valid() && list.row_height == 28.0f);
    assert(list.virtualized && list.overscan == 3u);
    assert(grid.items.valid() && grid.item_template == list.item_template);
    assert(grid.columns == 4u && grid.row_height == 36.0f && grid.overscan == 2u);
    assert(chart.series.valid() && chart.labels.valid() && chart.max_points == 1024u);
    assert(chart.kind == thunder::UiChartKind::Area && chart.include_zero);
    assert(button_node->command.valid());
    assert(button_node->command_key == thunder::ui_stable_key("open_budget"));
    assert(button_node->tooltip_key == thunder::ui_stable_key("budget_button_tooltip"));

    bool found_nested_path = false;
    for (const auto& binding : valid.blueprint.bindings()) {
        if (binding.target == thunder::UiBindingTarget::Text && binding.step_count == 2u) {
            const auto steps = valid.blueprint.binding_steps().subspan(binding.first_step,
                                                                        binding.step_count);
            assert(steps[0].context == country);
            assert(steps[1].context == state);
            found_nested_path = true;
        }
    }
    assert(found_nested_path);

    // The object-definition order does not affect compact template/screen IDs or
    // the deterministic blueprint checksum.
    const auto reordered = compiler.compile(screen_source + template_source, "reordered_gui.thunder");
    assert(reordered.ok());
    assert(reordered.blueprint.checksum() == valid.blueprint.checksum());

    thunder::SymbolTable fresh_symbols;
    thunder::ScriptedGuiSchema fresh_schema{fresh_symbols};
    const auto fresh_country = fresh_schema.register_context("country");
    const auto fresh_state = fresh_schema.register_context("state");
    (void)fresh_schema.register_property(fresh_country, "show_budget", thunder::UiValueType::Boolean);
    (void)fresh_schema.register_property(fresh_country, "can_spend", thunder::UiValueType::Boolean);
    (void)fresh_schema.register_property(fresh_country, "budget", thunder::UiValueType::Number);
    (void)fresh_schema.register_property(fresh_country, "display_name", thunder::UiValueType::Text);
    (void)fresh_schema.register_property(fresh_country, "flag", thunder::UiValueType::Asset);
    (void)fresh_schema.register_property(fresh_country, "states", thunder::UiValueType::Collection,
                                         fresh_state);
    (void)fresh_schema.register_property(fresh_country, "capital", thunder::UiValueType::Entity,
                                         fresh_state);
    (void)fresh_schema.register_property(fresh_country, "gdp_history",
                                         thunder::UiValueType::NumberSeries);
    (void)fresh_schema.register_property(fresh_country, "year_labels",
                                         thunder::UiValueType::TextSeries);
    (void)fresh_schema.register_property(fresh_state, "name", thunder::UiValueType::Text);
    (void)fresh_schema.register_command("open_budget");
    thunder::ScriptedGuiCompiler fresh_compiler{fresh_symbols, fresh_schema};
    const auto fresh_reordered = fresh_compiler.compile(screen_source + template_source);
    assert(fresh_reordered.ok());
    assert(fresh_reordered.blueprint.checksum() == valid.blueprint.checksum());
    assert(valid.blueprint.memory_bytes() >= valid.blueprint.nodes().size() *
                                               sizeof(thunder::CompiledUiNode));

    // Virtualization consumes only compiled numeric metadata; it never resolves
    // a content string per row.
    const auto window = thunder::virtualize_rows(1'000'000u, list.row_height,
                                               280'000.0f, 560.0f, list.overscan);
    assert(window.first > 0u && window.count < 32u);

    const auto unknown_binding = compiler.compile(R"(
scripted_gui bad_binding {
    context = country
    root = { type = label text = { bind = missing_property } }
}
)");
    assert(!unknown_binding.ok());
    assert(has_diagnostic(unknown_binding, thunder::ScriptedGuiDiagnosticCode::UnknownBinding));

    const auto type_mismatch = compiler.compile(R"(
scripted_gui bad_type {
    context = country
    root = { type = button visible = { bind = budget } }
}
)");
    assert(!type_mismatch.ok());
    assert(has_diagnostic(type_mismatch, thunder::ScriptedGuiDiagnosticCode::BindingTypeMismatch));

    const auto template_cycle = compiler.compile(R"(
ui_template cycle_a {
    context = country
    root = { template = cycle_b }
}
ui_template cycle_b {
    context = country
    root = { template = cycle_a }
}
)");
    assert(!template_cycle.ok());
    assert(has_diagnostic(template_cycle, thunder::ScriptedGuiDiagnosticCode::TemplateCycle));

    const auto unknown_template = compiler.compile(R"(
scripted_gui bad_template {
    context = country
    root = { template = does_not_exist }
}
)");
    assert(!unknown_template.ok());
    assert(has_diagnostic(unknown_template, thunder::ScriptedGuiDiagnosticCode::UnknownTemplate));

    const auto schema_error = compiler.compile(R"(
scripted_gui bad_schema {
    context = country
    root = { type = label columns = 2 }
}
)");
    assert(!schema_error.ok());
    assert(has_diagnostic(schema_error, thunder::ScriptedGuiDiagnosticCode::UnknownField));

    // ScriptedGuiRuntime integration tests
    class MockDataProvider : public thunder::ScriptedGuiDataProvider {
    public:
        bool show_budget = true;
        bool can_spend = true;
        double budget = 1234.5;
        std::string display_name = "Great Britain";
        thunder::UiStableKey flag_asset = thunder::ui_stable_key("flags/gbr");
        std::size_t state_count = 50;
        std::string capital_name = "London";
        std::vector<double> gdp = {100.0, 110.0, 125.0, 140.0, 160.0};
        std::vector<thunder::UiStableKey> year_labels = {
            thunder::ui_stable_key("1836"),
            thunder::ui_stable_key("1837"),
            thunder::ui_stable_key("1838"),
            thunder::ui_stable_key("1839"),
            thunder::ui_stable_key("1840")
        };
        bool fail_reads = false;

        bool read_property(thunder::UiDataEntityRef source,
                           std::uint16_t property_slot,
                           thunder::UiDataValue& out) const noexcept override {
            if (fail_reads) return false;
            if (source.context.value() == 0) { // country
                switch (property_slot) {
                case 0: out = thunder::UiDataValue::boolean_value(show_budget); return true;
                case 1: out = thunder::UiDataValue::boolean_value(can_spend); return true;
                case 2: out = thunder::UiDataValue::number_value(budget); return true;
                case 3: out = thunder::UiDataValue::text_value(display_name); return true;
                case 4: out = thunder::UiDataValue::key_value(thunder::UiValueType::Asset, flag_asset); return true;
                case 5: {
                    thunder::UiDataCollectionRef col{};
                    col.element_context = thunder::UiDataContextId{1};
                    col.stable_key = thunder::ui_stable_key("states");
                    col.size = state_count;
                    out = thunder::UiDataValue::collection_value(col);
                    return true;
                }
                case 6: {
                    thunder::UiDataEntityRef ent{};
                    ent.context = thunder::UiDataContextId{1};
                    ent.stable_key = thunder::ui_stable_key("capital");
                    out = thunder::UiDataValue::entity_value(ent);
                    return true;
                }
                case 7: {
                    thunder::UiDataSeriesRef s{};
                    s.stable_key = thunder::ui_stable_key("gdp_history");
                    s.size = gdp.size();
                    out = thunder::UiDataValue::series_value(thunder::UiValueType::NumberSeries, s);
                    return true;
                }
                case 8: {
                    thunder::UiDataSeriesRef s{};
                    s.stable_key = thunder::ui_stable_key("year_labels");
                    s.size = year_labels.size();
                    out = thunder::UiDataValue::series_value(thunder::UiValueType::TextSeries, s);
                    return true;
                }
                default: return false;
                }
            } else if (source.context.value() == 1) { // state
                if (property_slot == 0) {
                    out = thunder::UiDataValue::text_value(capital_name);
                    return true;
                }
            }
            return false;
        }

        bool collection_element(const thunder::UiDataCollectionRef&,
                                std::size_t index,
                                thunder::UiDataEntityRef& out) const noexcept override {
            if (index >= state_count) return false;
            out.context = thunder::UiDataContextId{1};
            out.stable_key = thunder::ui_stable_key("state_" + std::to_string(index));
            out.handle = index;
            return true;
        }

        bool number_series_value(const thunder::UiDataSeriesRef&,
                                 std::size_t index,
                                 double& out) const noexcept override {
            if (index >= gdp.size()) return false;
            out = gdp[index];
            return true;
        }

        bool text_series_value(const thunder::UiDataSeriesRef&,
                               std::size_t index,
                               thunder::UiStableKey& out) const noexcept override {
            if (index >= year_labels.size()) return false;
            out = year_labels[index];
            return true;
        }
    };

    MockDataProvider provider;
    thunder::ScriptedGuiRuntime runtime{valid.blueprint};
    assert(!runtime.mounted());

    thunder::UiDataEntityRef root_country{};
    root_country.context = country;
    root_country.stable_key = thunder::ui_stable_key("GBR");
    root_country.handle = 1001;

    const auto screen_key = thunder::ui_stable_key("country_overview");
    const bool mounted = runtime.instantiate_screen(screen_key, root_country);
    assert(mounted);
    assert(runtime.mounted());
    assert(runtime.generation() == 0);

    const auto initial_diff = runtime.refresh(provider);
    assert(initial_diff.generation == 1);
    assert(initial_diff.dirty_nodes > 0);
    assert(initial_diff.provider_errors == 0);
    assert(!initial_diff.truncated);

    thunder::UiDrawList scripted_draw_list;
    thunder::ScriptedGuiPainter painter{valid.blueprint};
    painter.paint(runtime, scripted_draw_list, {0.0f, 0.0f, 1280.0f, 720.0f},
                  [](thunder::UiStableKey) { return std::string{"localized"}; });
    assert(!scripted_draw_list.vertices().empty());
    assert(!scripted_draw_list.indices().empty());
    assert(!scripted_draw_list.hits().empty());

    const auto* capital_node = runtime.find_blueprint_node(thunder::ui_stable_node_key(screen_key, "capital_name"));
    assert(capital_node != nullptr);
    assert(capital_node->text == "London");

    const auto* button_rt = runtime.find_blueprint_node(thunder::ui_stable_node_key(screen_key, "budget_button"));
    assert(button_rt != nullptr);
    assert(button_rt->visible && button_rt->enabled);
    assert(button_rt->command_key == thunder::ui_stable_key("open_budget"));

    const auto* progress_rt = runtime.find_blueprint_node(thunder::ui_stable_node_key(screen_key, "budget_progress"));
    assert(progress_rt != nullptr);
    assert(progress_rt->value == 1234.5);

    const auto* chart_rt = runtime.find_blueprint_node(thunder::ui_stable_node_key(screen_key, "growth_chart"));
    assert(chart_rt != nullptr);
    assert(chart_rt->chart_values.size() == 5);
    assert(chart_rt->chart_labels.size() == 5);
    assert(chart_rt->chart_kind == thunder::UiChartKind::Area);

    const auto chart_view = runtime.chart_view(static_cast<std::uint32_t>(chart_rt - runtime.nodes().data()));
    assert(chart_view.values.size() == 5);
    assert(chart_view.labels.size() == 5);

    // Modify provider state and verify dirty diff
    provider.show_budget = false;
    provider.budget = 2000.0;
    const auto update_diff = runtime.refresh(provider);
    assert(update_diff.generation == 2);
    assert(update_diff.dirty_nodes >= 2);
    assert(!button_rt->visible);
    assert(progress_rt->value == 2000.0);

    // Viewport virtualization on List
    thunder::UiCollectionViewport list_vp{};
    list_vp.node_or_instance_key = thunder::ui_stable_node_key(screen_key, "state_list");
    list_vp.scroll_y = 100.0f;
    list_vp.viewport_height = 200.0f;
    const thunder::UiCollectionViewport vps[] = {list_vp};
    const auto vp_diff = runtime.refresh(provider, vps);
    assert(vp_diff.generation == 3);
    const auto* list_rt = runtime.find_blueprint_node(thunder::ui_stable_node_key(screen_key, "state_list"));
    assert(list_rt != nullptr);
    assert(list_rt->item_window.count > 0);

    // Provider error recovery test
    provider.fail_reads = true;
    const auto err_diff = runtime.refresh(provider);
    assert(err_diff.provider_errors > 0);

    provider.fail_reads = false;
    const auto recover_diff = runtime.refresh(provider);
    assert(recover_diff.provider_errors == 0);

    runtime.clear();
    assert(!runtime.mounted());
    assert(runtime.nodes().empty());
    assert(!runtime.removed_keys().empty());

    // --- Construction Queue & PM Upgrade UI Schema & Widget Verification ---
    const auto cq_ctx = schema.register_context("construction_queue");
    const auto cp_ctx = schema.register_context("construction_project");
    (void)schema.register_property(cq_ctx, "capacity", thunder::UiValueType::Number);
    (void)schema.register_property(cq_ctx, "projects", thunder::UiValueType::Collection, cp_ctx);
    (void)schema.register_property(cp_ctx, "name", thunder::UiValueType::Text);
    (void)schema.register_property(cp_ctx, "kind_label", thunder::UiValueType::Text);
    (void)schema.register_property(cp_ctx, "progress_ratio", thunder::UiValueType::Number);
    (void)schema.register_property(cp_ctx, "eta_text", thunder::UiValueType::Text);
    (void)schema.register_property(cp_ctx, "is_paused", thunder::UiValueType::Boolean);

    // Verify Construction Queue UI Row Widget Rendering
    thunder::UiDrawList draw_list;
    draw_list.clear();
    draw_list.construction_queue_row({10.0f, 10.0f, 600.0f, 40.0f},
                                    "Textile Mill #1 [Steam Looms]",
                                    "[PM Upgrade]",
                                    0.65f,
                                    "2 Weeks",
                                    false);
    assert(!draw_list.vertices().empty());
    assert(!draw_list.indices().empty());

    // Render Monument construction row
    draw_list.construction_queue_row({10.0f, 60.0f, 600.0f, 40.0f},
                                    "Statue of Liberty",
                                    "[Monument]",
                                    0.25f,
                                    "18 Weeks",
                                    false);
    assert(draw_list.vertices().size() > 10);

    // Verify Tariff Smooth Slider & Input Box Dual-Widget Rendering
    draw_list.tariff_slider_input_row({10.0f, 110.0f, 600.0f, 36.0f},
                                     "Grain (London Market)",
                                     0.15f,
                                     "15.0%",
                                     true);
    draw_list.tariff_slider_input_row({10.0f, 150.0f, 600.0f, 36.0f},
                                     "Bessemer Steel (Export)",
                                     0.05f,
                                     "5.0%",
                                     false);
    assert(draw_list.vertices().size() > 30);

    std::cout << "Scripted GUI blueprint, construction queue UI, and runtime tests passed\n";
}
