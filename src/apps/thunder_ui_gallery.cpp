// UI component gallery host. Renders the engine's themed widget set over the
// live world map into a screenshot, so visual QA against the target
// grand-strategy look (see docs/UI_THEME.md) never substitutes a mockup for
// the real Vulkan path. Screenshot output: <out>/gallery.bmp.
#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include "thunder/presentation/ui/UiTheme.hpp"
#include "thunder/simulation/world/WorldTopology.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/presentation/render/map/WorldMapPicker.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace thunder;

namespace {

void render_top_bar(UiDrawList& ui, float width) {
    const auto& theme = ui.theme();
    const auto& colors = theme.colors;
    const auto& type = theme.type;
    ui.wood_panel({0.0f, 0.0f, width, 64.0f});
    ui.wax_seal(34.0f, 32.0f, 18.0f);
    ui.text("QING EMPIRE", 64.0f, 19.0f, type.display, colors.text_gold);

    struct Chip { const char* label; const char* value; double delta; };
    const Chip chips[]{
        {"TREASURY", "\xC2\xA3""14.2M", 38100.0},
        {"AUTHORITY", "712", 4.0},
        {"INFLUENCE", "891", -12.0},
        {"PRESTIGE", "305", 2.0},
    };
    float x = 330.0f;
    for (const auto& chip : chips) {
        ui.panel({x, 10.0f, 150.0f, 44.0f}, colors.bg_panel_recessed, colors.border_brass, 0u, 0.0f);
        ui.text(chip.label, x + 10.0f, 16.0f, type.caption, colors.text_muted);
        ui.text(chip.value, x + 10.0f, 29.0f, type.stat_value, colors.text_primary);
        ui.text(ui_format_delta(chip.delta), x + 140.0f, 31.0f, type.stat_delta,
                ui_delta_color(theme, chip.delta), {}, true);
        x += 162.0f;
    }

    ui.text("1 JANUARY 1836", width - 210.0f, 14.0f, type.secondary, colors.text_secondary, {}, true);
    for (int i = 0; i < 4; ++i) {
        const bool selected = (i == 1);
        ui.medallion_button({width - 196.0f + static_cast<float>(i) * 36.0f, 32.0f, 26.0f, 26.0f},
                            selected, false, true);
    }
}

void render_lens_rail(UiDrawList& ui, float height) {
    ui.wood_panel({0.0f, 64.0f, 56.0f, height - 64.0f});
    const bool selected[] = {true, false, false, false, false, false};
    const bool hovered[] = {false, true, false, false, false, false};
    const bool enabled[] = {true, true, true, true, false, true};
    for (std::size_t i = 0; i < 6u; ++i) {
        ui.medallion_button({8.0f, 86.0f + static_cast<float>(i) * 52.0f, 40.0f, 40.0f},
                            selected[i], hovered[i], enabled[i]);
    }
}

void render_market_panel(UiDrawList& ui) {
    const auto& theme = ui.theme();
    const auto& type = theme.type;
    const UiRect panel{88.0f, 96.0f, 600.0f, 590.0f};
    ui.parchment_panel(panel);
    ui.ornate_header({panel.x + 14.0f, panel.y + 12.0f, panel.w - 28.0f, 40.0f},
                     "MARKET \xC2\xB7 QING EMPIRE");

    const float tab_y = panel.y + 62.0f;
    ui.tab({panel.x + 16.0f, tab_y, 110.0f, 26.0f}, "Goods", true);
    ui.tab({panel.x + 130.0f, tab_y, 110.0f, 26.0f}, "Trade", false);
    ui.tab({panel.x + 244.0f, tab_y, 110.0f, 26.0f}, "Prices", false, true);

    const float tx = panel.x + 16.0f;
    float y = panel.y + 104.0f;
    ui.table_header_cell({tx, y, 220.0f, 22.0f}, "GOOD");
    ui.table_header_cell({tx + 220.0f, y, 100.0f, 22.0f}, "PRICE", true);
    ui.table_header_cell({tx + 320.0f, y, 120.0f, 22.0f}, "BALANCE", true);
    ui.table_header_cell({tx + 440.0f, y, 112.0f, 22.0f}, "TREND", true);
    y += 24.0f;
    struct Row { const char* name; const char* kind; const char* price; };
    const Row rows[]{
        {"Grain", "staple food", "\xC2\xA3""22.4"}, {"Fish", "food", "\xC2\xA3""18.1"},
        {"Clothes", "manufactured", "\xC2\xA3""41.0"}, {"Furniture", "manufactured", "\xC2\xA3""47.8"},
        {"Tea", "luxury", "\xC2\xA3""63.2"}, {"Opium", "luxury", "\xC2\xA3""88.9"},
        {"Iron", "industrial", "\xC2\xA3""35.5"},
    };
    for (std::size_t i = 0; i < 7u; ++i) {
        ui.list_row({tx, y, 552.0f, 24.0f}, rows[i].name, rows[i].kind, rows[i].price,
                    i == 2u, false, (i % 2u) == 1u);
        y += 24.0f;
    }
    ui.separator({tx, y + 6.0f, 552.0f, 8.0f});
    y += 18.0f;

    ui.text("BUY 320 \xC2\xB7 SELL 210", tx, y, type.caption, ui.theme().materials.parchment_text_muted);
    ui.gauge_balance({tx + 170.0f, y - 2.0f, 200.0f, 22.0f}, 320.0f, 210.0f);
    y += 34.0f;

    const float history[] = {22.1f, 22.0f, 22.4f, 22.9f, 22.6f, 23.1f, 22.8f, 22.4f,
                             22.7f, 23.4f, 23.0f, 23.6f, 24.1f, 23.8f, 24.4f, 24.0f};
    ui.text("GRAIN PRICE \xC2\xB7 16 WEEKS", tx, y, type.caption, ui.theme().materials.parchment_text_muted);
    ui.ink_chart({tx + 170.0f, y - 6.0f, 382.0f, 84.0f}, history);
    y += 100.0f;

    ui.text("MARKET ACCESS", tx, y, type.caption, ui.theme().materials.parchment_text_muted);
    ui.progress_bar({tx + 170.0f, y, 382.0f, 14.0f}, 0.78f);
    y += 30.0f;
    ui.stat_row({tx, y, 552.0f, 24.0f}, "Trade routes", "18 active", "+3");
}

void render_right_column(UiDrawList& ui, float width) {
    const auto& colors = ui.theme().colors;
    const float x = width - 388.0f;
    ui.financial_kpi_card({x, 96.0f, 372.0f, 92.0f}, "TREASURY", "\xC2\xA3""14.2M",
                          38100.0, 0.62f, colors.gold_reserve);
    ui.financial_kpi_card({x, 198.0f, 372.0f, 92.0f}, "GROSS DOMESTIC PRODUCT",
                          "\xC2\xA3""389M", 2.1, 0.45f, colors.investment_pool);
    ui.gold_reserve_meter({x, 300.0f, 372.0f, 84.0f}, 12.4e6, 20.0e6, 25.0e6);
    ui.treasury_balance_ticker({x, 394.0f, 372.0f, 84.0f}, 58200.0, 47100.0);
    ui.notification_card({x, 492.0f, 372.0f, 84.0f}, "Construction completed",
                         "Government Administration \xC2\xB7 Beijing", 2);
    ui.notification_card({x, 584.0f, 372.0f, 84.0f}, "Diplomatic play escalated",
                         "Great Britain demands war reparations", 4);
}

void render_construction_panel(UiDrawList& ui, float height) {
    const UiRect panel{664.0f, height - 268.0f, 640.0f, 232.0f};
    ui.parchment_panel(panel);
    ui.ornate_header({panel.x + 14.0f, panel.y + 10.0f, panel.w - 28.0f, 36.0f}, "CONSTRUCTION");
    float y = panel.y + 54.0f;
    ui.construction_queue_row({panel.x + 16.0f, y, panel.w - 32.0f, 40.0f},
                              "Government Administration", "Building \xC2\xB7 Level 4", 0.62f,
                              "24 weeks left");
    y += 46.0f;
    ui.construction_queue_row({panel.x + 16.0f, y, panel.w - 32.0f, 40.0f},
                              "Railway", "Building \xC2\xB7 Zhili", 0.18f, "96 weeks left");
    y += 46.0f;
    ui.construction_queue_row({panel.x + 16.0f, y, panel.w - 32.0f, 40.0f},
                              "Naval Base", "Building \xC2\xB7 Fujian", 0.44f, "paused", true);
    y += 50.0f;
    ui.tariff_slider_input_row({panel.x + 16.0f, y, panel.w - 32.0f, 34.0f},
                               "Grain tariff", 0.35f, "35%", false);
}

void render_controls_panel(UiDrawList& ui, float height) {
    const auto& theme = ui.theme();
    const auto& colors = theme.colors;
    const auto& type = theme.type;
    const UiRect panel{88.0f, height - 268.0f, 540.0f, 232.0f};
    ui.panel(panel, colors.bg_panel, colors.border_normal, colors.shadow_floating, 4.0f);
    ui.ornate_header({panel.x + 14.0f, panel.y + 10.0f, panel.w - 28.0f, 36.0f}, "CONTROLS");

    float y = panel.y + 54.0f;
    ui.brass_button({panel.x + 16.0f, y, 110.0f, 26.0f}, "Enact");
    ui.brass_button({panel.x + 134.0f, y, 110.0f, 26.0f}, "Pressed", true);
    UiControlVisualState disabled{}; disabled.enabled = false;
    ui.mechanical_button({panel.x + 252.0f, y, 110.0f, 26.0f}, disabled);
    UiControlVisualState hovered{}; hovered.hovered = true; hovered.hover_mix = 1.0f;
    ui.mechanical_button({panel.x + 370.0f, y, 110.0f, 26.0f}, hovered);
    y += 40.0f;

    ui.checkbox({panel.x + 16.0f, y, 180.0f, 16.0f}, true);
    ui.text("Autopause on events", panel.x + 42.0f, y - 2.0f, type.body, colors.text_primary);
    ui.checkbox({panel.x + 232.0f, y, 180.0f, 16.0f}, false, true);
    ui.text("Debug overlays", panel.x + 258.0f, y - 2.0f, type.body, colors.text_primary);
    y += 30.0f;

    ui.radio({panel.x + 16.0f, y, 180.0f, 16.0f}, true);
    ui.text("Mercator", panel.x + 42.0f, y - 2.0f, type.body, colors.text_primary);
    ui.radio({panel.x + 232.0f, y, 180.0f, 16.0f}, false);
    ui.text("Gall stereographic", panel.x + 258.0f, y - 2.0f, type.body, colors.text_primary);
    y += 32.0f;

    ui.text("Map label density", panel.x + 16.0f, y - 2.0f, type.body, colors.text_primary);
    ui.slider({panel.x + 200.0f, y + 2.0f, 220.0f, 14.0f}, 0.65f, true);
    y += 32.0f;

    ui.dropdown_row({panel.x + 16.0f, y, 200.0f, 24.0f}, "Weekly budget view", false, true);
    ui.input_box({panel.x + 232.0f, y, 180.0f, 24.0f}, "Beijing", true);
    ui.scrollbar({panel.x + panel.w - 20.0f, panel.y + 54.0f, 8.0f, 130.0f}, 0.15f, 0.4f);
}

void render_event_modal(UiDrawList& ui, float width) {
    ui.modal_window({width - 560.0f, 700.0f, 420.0f, 240.0f}, "THE OPIUM QUESTION",
                    "British merchants press for open trade.\n"
                    "The court is divided.");
}

} // namespace

int main(int argc, char** argv) {
    SDL_Window* window = nullptr;
    try {
        const auto repo = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        std::filesystem::path world = repo / "generated/world_map/world-1836-preview.thunderworld";
        std::filesystem::path output = "ui_gallery";
        std::filesystem::path shaders;
        int frames = 150, width = 1920, height = 1080;
        float altitude = 3000000.0f, u = 0.525f, v = 0.46f;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (i + 1 == argc) throw std::runtime_error("missing value for " + arg);
            const std::string value = argv[++i];
            if (arg == "--world") world = value;
            else if (arg == "--out") output = value;
            else if (arg == "--shaders") shaders = value;
            else if (arg == "--frames") frames = std::stoi(value);
            else if (arg == "--width") width = std::stoi(value);
            else if (arg == "--height") height = std::stoi(value);
            else if (arg == "--altitude") altitude = std::stof(value);
            else if (arg == "--u") u = std::stof(value);
            else if (arg == "--v") v = std::stof(value);
            else throw std::runtime_error("unknown option: " + arg);
        }
        if (frames < 1 || width < 640 || height < 480)
            throw std::runtime_error("usage: thunder_ui_gallery [--world pack] [--out dir] [--frames n]");
        std::filesystem::create_directories(output);

        thunder::WorldPackReader pack;
        pack.open(world);
        const auto topology = thunder::WorldTopology::load(pack);
        thunder::WorldMapPicker picker;
        std::string diagnostic;
        if (!picker.open(world, topology.map_hierarchy, diagnostic)) throw std::runtime_error(diagnostic);

        std::vector<thunder::ProvincePoliticalRecord> politics(topology.geography.province_count());
        std::vector<thunder::Rgba8> colors(4096);
        for (std::size_t i = 0; i < colors.size(); ++i)
            colors[i] = {static_cast<std::uint8_t>(110u + (i * 31u) % 100u),
                         static_cast<std::uint8_t>(110u + (i * 53u) % 100u),
                         static_cast<std::uint8_t>(95u + (i * 17u) % 100u), 255u};
        for (std::size_t i = 0; i < topology.countries.size() && i < colors.size(); ++i) {
            const auto& country = topology.countries[i];
            if (country.authored_map_color)
                colors[i] = {country.map_color[0], country.map_color[1], country.map_color[2], country.map_color[3]};
        }
        for (std::size_t i = 0; i < politics.size(); ++i) {
            const auto id = thunder::ProvinceId{static_cast<thunder::ProvinceId::rep_type>(i)};
            politics[i].owner_country = topology.geography.province_owner(id).value();
            const auto kind = topology.geography.province_kind(id);
            if (kind == thunder::ProvinceKind::Sea) politics[i].flags = thunder::PoliticalSea;
            else if (kind == thunder::ProvinceKind::Lake) politics[i].flags = thunder::PoliticalLake;
        }

        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Thunder UI gallery", width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error(SDL_GetError());
        {
            thunder::VulkanDesktopBackend renderer;
            renderer.set_world_pack(world);
            const auto font_dir = repo / "assets/fonts";
            renderer.set_ui_font_atlas(font_dir / "ui_body_atlas.thunderimg", font_dir / "ui_body.thunderfont");
            if (!shaders.empty()) renderer.set_shader_dir(shaders);
            renderer.initialize(window, false);
            renderer.set_world_political_state(politics, colors);
            const auto& metadata = picker.metadata();
            const double span_x = metadata.bounds_world_m[2] - metadata.bounds_world_m[0];
            const double span_y = metadata.bounds_world_m[3] - metadata.bounds_world_m[1];
            const float half_y = std::min(0.5f, static_cast<float>(static_cast<double>(altitude) / span_y));
            const float half_x = std::min(0.5f, static_cast<float>(
                static_cast<double>(altitude) * width / height / span_x));
            renderer.set_map_view(u, v, half_x, half_y, altitude, 90.0f);

            thunder::UiDrawList ui;
            render_top_bar(ui, static_cast<float>(width));
            render_lens_rail(ui, static_cast<float>(height));
            render_market_panel(ui);
            render_right_column(ui, static_cast<float>(width));
            render_construction_panel(ui, static_cast<float>(height));
            render_controls_panel(ui, static_cast<float>(height));
            render_event_modal(ui, static_cast<float>(width));
            constexpr std::uint64_t enact_hit_id = 42u;
            ui.hit(enact_hit_id, {104.0f, static_cast<float>(height) - 268.0f + 54.0f, 110.0f, 26.0f});
            renderer.submit_ui(ui);
            const auto hit = ui.hit_test(160.0f, static_cast<float>(height) - 268.0f + 66.0f);
            std::cout << "hit_test(enact) -> " << (hit && *hit == enact_hit_id ? "ok" : "MISS") << '\n';

            SDL_Event event;
            for (int i = 0; i < frames; ++i) {
                while (SDL_PollEvent(&event)) {}
                renderer.draw_frame();
            }
            renderer.wait_idle();
            renderer.request_screenshot(output / "gallery.bmp");
            renderer.draw_frame();
            renderer.wait_idle();
            if (renderer.validation_errors() != 0) throw std::runtime_error("Vulkan validation reported errors");
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "UI_GALLERY wrote " << (output / "gallery.bmp") << '\n';
        return 0;
    } catch (const std::exception& error) {
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        std::cerr << "UI_GALLERY FAIL: " << error.what() << '\n';
        return 1;
    }
}
