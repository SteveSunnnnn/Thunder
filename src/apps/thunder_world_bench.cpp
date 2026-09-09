// Native map acceptance host. Measures the actual Vulkan backend and saves
// screenshots outside the timed interval; never substitutes a browser mockup.
#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"
#include "thunder/simulation/world/WorldTopology.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/presentation/render/map/WorldMapPicker.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    SDL_Window* window = nullptr;
    try {
        std::filesystem::path world, output = "world_bench", shaders;
        int frames = 240, width = 2560, height = 1440;
        float altitude = 10000000.0f, u = 0.5f, v = 0.5f;
        std::uint32_t shading_debug = 0u;
        bool validation = false, exercise_politics = false, synthetic_ownership = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--validation") { validation = true; continue; }
            if (arg == "--exercise-politics") { exercise_politics = true; continue; }
            if (arg == "--synthetic-ownership") { synthetic_ownership = true; continue; }
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
            else if (arg == "--shading-debug") shading_debug = static_cast<std::uint32_t>(std::stoul(value));
            else throw std::runtime_error("unknown option: " + arg);
        }
        if (world.empty() || frames < 1 || width < 1 || height < 1 ||
            !std::isfinite(altitude) || altitude <= 0 || !std::isfinite(u) || !std::isfinite(v))
            throw std::runtime_error("usage: thunder_world_bench --world file --out directory [--validation]");
        std::filesystem::create_directories(output);
        thunder::WorldPackReader pack;
        pack.open(world);
        const auto topology = thunder::WorldTopology::load(pack);
        thunder::WorldMapPicker picker;
        std::string diagnostic;
        if (!picker.open(world, topology.map_hierarchy, diagnostic)) throw std::runtime_error(diagnostic);
        std::vector<thunder::ProvincePoliticalRecord> politics(topology.geography.province_count());
        // Stable reference colors are only visual diagnostics; ownership comes
        // from the compiled scenario. Distinct IDs intentionally share a color
        // for a subset to exercise the integer-boundary path.
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
            const auto state = topology.geography.province_state(id);
            const auto owner = topology.geography.province_owner(id);
            politics[i].owner_country = synthetic_ownership
                ? (state.valid() ? state.value() % static_cast<std::uint32_t>(colors.size()) : 0xffffffffu)
                : owner.value();
            const auto kind = topology.geography.province_kind(id);
            if (kind == thunder::ProvinceKind::Sea) politics[i].flags = thunder::PoliticalSea;
            else if (kind == thunder::ProvinceKind::Lake) politics[i].flags = thunder::PoliticalLake;
        }
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Thunder world map validation", width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error(SDL_GetError());
        {
            thunder::VulkanDesktopBackend renderer;
            renderer.set_world_pack(world);
            const auto font_dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/fonts";
            renderer.set_ui_font_atlas(font_dir / "map_sans_atlas.thunderimg", font_dir / "map_sans.thunderfont");
            if (!shaders.empty()) renderer.set_shader_dir(shaders);
            renderer.initialize(window, validation);
            renderer.set_shading_debug(shading_debug);
            renderer.set_world_political_state(politics, colors);
            const auto& metadata = picker.metadata();
            const double span_x = metadata.bounds_world_m[2] - metadata.bounds_world_m[0];
            const double span_y = metadata.bounds_world_m[3] - metadata.bounds_world_m[1];
            const float half_y = std::min(0.5f, static_cast<float>(altitude / span_y));
            const float half_x = std::min(0.5f, static_cast<float>(static_cast<double>(altitude) * width / height / span_x));
            renderer.set_map_view(u, v, half_x, half_y, altitude, 90.0f);
            thunder::UiDrawList map_labels;
            std::vector<const thunder::WorldMapLabelRecord*> label_candidates;
            for (const auto& label : topology.map_labels.records())
                if (label.kind == thunder::WorldMapLabelKind::Country) label_candidates.push_back(&label);
            std::sort(label_candidates.begin(), label_candidates.end(), [](auto a, auto b) {
                return a->geographic_area_km2 > b->geographic_area_km2;
            });
            struct LabelBox { float x0, y0, x1, y1; };
            std::vector<LabelBox> placed;
            for (const auto* label : label_candidates) {
                const auto& a = label->spine.front();
                const auto& b = label->spine.back();
                const float cx = (a.u + b.u) * 0.5f;
                const float cy = (a.v + b.v) * 0.5f;
                const float sx = ((cx - u) / (2.0f * half_x) + 0.5f) * static_cast<float>(width);
                const float sy = ((cy - v) / (2.0f * half_y) + 0.5f) * static_cast<float>(height);
                const float dx = (b.u - a.u) / (2.0f * half_x) * static_cast<float>(width);
                const float dy = (b.v - a.v) / (2.0f * half_y) * static_cast<float>(height);
                const float available = std::hypot(dx, dy);
                const float angle = std::atan2(dy, dx);
                const float size = std::min(42.0f, available / std::max(1.0f, static_cast<float>(label->text.size()) * 0.65f));
                if (size < 13.0f || altitude < 3000.0f) continue;
                const float text_width = size * static_cast<float>(label->text.size()) * 0.65f;
                const float half_w = std::abs(std::cos(angle)) * text_width * 0.5f + std::abs(std::sin(angle)) * size * 0.7f;
                const float half_h = std::abs(std::sin(angle)) * text_width * 0.5f + std::abs(std::cos(angle)) * size * 0.7f;
                const LabelBox box{sx - half_w, sy - half_h, sx + half_w, sy + half_h};
                if (box.x0 < 0 || box.y0 < 0 || box.x1 > static_cast<float>(width) || box.y1 > static_cast<float>(height)) continue;
                if (std::any_of(placed.begin(), placed.end(), [&](const auto& old) {
                    return box.x0 < old.x1 + 5 && box.x1 + 5 > old.x0 && box.y0 < old.y1 + 5 && box.y1 + 5 > old.y0;
                })) continue;
                map_labels.map_text(label->text, sx, sy, size, 0xff10151bu, angle, size * 0.035f);
                placed.push_back(box);
            }
            renderer.submit_ui(map_labels);
            SDL_Event event;
            // Warm all pages, shaders, present queues and timestamp query rings.
            const auto warm_start = std::chrono::steady_clock::now();
            for (int i = 0; i < 180; ++i) {
                while (SDL_PollEvent(&event)) {}
                renderer.draw_frame();
                if (std::chrono::steady_clock::now() - warm_start > std::chrono::seconds(60))
                    throw std::runtime_error("map warmup exceeded 60 seconds");
            }
            if (!renderer.committed_map_ready()) throw std::runtime_error("map did not become resident");
            renderer.wait_idle();
            renderer.reset_stats();
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < frames; ++i) {
                while (SDL_PollEvent(&event)) {}
                renderer.draw_frame();
            }
            renderer.wait_idle();
            const double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            const auto stats = renderer.stats();
            std::uint64_t checksum = 0;
            constexpr std::uint32_t pick_count = 1000000;
            const auto pick_start = std::chrono::steady_clock::now();
            for (std::uint32_t i = 0; i < pick_count; ++i) {
                const auto p = picker.pick_uv(static_cast<double>(i % 997u) / 997.0,
                                             static_cast<double>(i % 991u) / 991.0, 0, i);
                checksum += p.valid() ? p.location.value() + 1u : 0u;
            }
            const double pick_ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - pick_start).count() / pick_count;
            std::ofstream report(output / "timing.json");
            report << "{\n  \"width\": " << width << ", \"height\": " << height
                   << ", \"altitude_m\": " << altitude << ", \"frames\": " << frames
                   << ",\n  \"wall_fps\": " << frames * 1000.0 / elapsed_ms
                   << ", \"gpu_ms_mean\": " << stats.avg_gpu_ms
                   << ", \"cpu_ms_mean\": " << stats.avg_cpu_ms
                   << ", \"cpu_submission_ms_mean\": " << stats.avg_cpu_submission_ms
                   << ", \"frame_ms_p95\": " << stats.p95_frame_ms
                   << ",\n  \"pick_ns_mean\": " << pick_ns << ", \"pick_checksum\": " << checksum
                   << ", \"resident_picking_bytes\": " << picker.resident_bytes()
                   << ", \"validation_errors\": " << renderer.validation_errors()
                   << ", \"validation_enabled\": " << (validation ? "true" : "false")
                   << ",\n  \"ownership_mode\": \"" << (synthetic_ownership ? "synthetic_diagnostics" : "world_pack_history") << "\"\n}\n";
            renderer.request_screenshot(output / "map.bmp");
            renderer.draw_frame();
            renderer.wait_idle();
            if (exercise_politics) {
                const auto target = picker.pick_uv(u, v, 0, 0);
                if (!target.valid()) throw std::runtime_error("political test point has no Location");
                auto& record = politics[target.raster_location.value()];
                if ((record.flags & (thunder::PoliticalSea | thunder::PoliticalLake)) != 0u)
                    throw std::runtime_error("political test point must be on land");
                record.owner_country = (record.owner_country + 1u) % static_cast<std::uint32_t>(colors.size());
                record.flags |= thunder::PoliticalOccupied;
                renderer.set_world_political_state(politics, colors);
                renderer.request_screenshot(output / "political_next_frame.bmp");
                renderer.draw_frame();
                renderer.wait_idle();
            }
            renderer.write_report(output / "device.txt");
            std::cout << "MAP_BENCH gpu_ms=" << stats.avg_gpu_ms << " cpu_ms=" << stats.avg_cpu_ms
                      << " wall_fps=" << frames * 1000.0 / elapsed_ms << " pick_ns=" << pick_ns << '\n';
            if (renderer.validation_errors() != 0) throw std::runtime_error("Vulkan validation reported errors");
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        std::cerr << "MAP_BENCH FAIL: " << error.what() << '\n';
        return 1;
    }
}
