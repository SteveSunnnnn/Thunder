#include "thunder/presentation/render/RenderSnapshot.hpp"
#include "thunder/presentation/render/RenderSnapshotBuilder.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/foundation/jobs/DeterministicReduction.hpp"
#include "thunder/foundation/geo/MercatorProjection.hpp"
#include "thunder/presentation/render/StrategicCamera.hpp"
#include "thunder/presentation/render/map/PoliticalMapState.hpp"
#include "thunder/presentation/render/map/MapModeStore.hpp"
#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"
#include "thunder/presentation/render/map/CoastDistancePage.hpp"
#include "thunder/presentation/render/map/PoliticalMapStreamingPlanner.hpp"
#include "thunder/presentation/render/map/ProvincePickingCache.hpp"
#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/presentation/render/terrain/TerrainClipmap.hpp"
#include "thunder/presentation/render/terrain/TerrainPageCache.hpp"
#include "thunder/presentation/render/terrain/TerrainStreamingPlanner.hpp"
#include "thunder/presentation/render/terrain/TerrainHeightPage.hpp"
#include "thunder/simulation/kernel/CommandQueue.hpp"
#include "thunder/simulation/kernel/ModifierGraph.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <array>
#include <vector>

using namespace thunder;
using Clock = std::chrono::steady_clock;

template <typename Fn>
double bench_ms(Fn&& fn) {
    const auto begin = Clock::now();
    fn();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

int main() {
    constexpr std::size_t countries = 100'000;
    World world;
    world.countries.reserve(countries);
    for (std::size_t i = 0; i < countries; ++i) {
        world.countries.create({"TST", 1'000'000.0 + static_cast<double>(i), 100.0, 20.0, 0.2});
    }

    RenderSnapshot snapshot;
    snapshot.reserve(countries);
    const double snapshot_ms = bench_ms([&] {
        for (std::uint64_t i = 0; i < 100; ++i) build_render_snapshot(world, snapshot, i);
    });

    CommandQueue commands{1'000'000};
    const CountryId first{0};
    const double command_enqueue_ms = bench_ms([&] {
        for (std::size_t i = 0; i < 1'000'000; ++i) commands.enqueue(CommandType::AddTreasury, first, 0.01);
    });
    const double command_apply_ms = bench_ms([&] { commands.apply_all(world); });

    ModifierGraph graph;
    const auto source = graph.add_source("source", 1.0);
    auto previous = source;
    constexpr std::size_t modifiers = 50'000;
    for (std::size_t i = 1; i < modifiers; ++i) {
        const auto dep = previous;
        previous = graph.add_derived("m", {dep}, [dep](const ModifierGraph& g) { return g.value(dep) + 1.0; });
    }
    (void)graph.value(previous);
    const double modifier_ms = bench_ms([&] {
        graph.set_source(source, 2.0);
        graph.recompute_all_dirty();
    });

    SymbolTable script_symbols;
    const auto script_registry = ScriptRegistry::make_builtin();
    ThunderScriptParser script_parser{script_symbols};
    std::string script_source;
    script_source.reserve(1'600'000u);
    for (std::uint32_t i = 0; i < 10'000u; ++i) {
        script_source += "script bench_" + std::to_string(i) +
            " { scope = country trigger = { population_above = 1 treasury_above = 1 } effect = { add_treasury = 0.01 } }\n";
    }
    ScriptProgramDatabase compiled_scripts;
    std::vector<ScriptCompileDiagnostic> script_diagnostics;
    ScriptParseResult parsed_scripts;
    const double script_compile_ms = bench_ms([&] {
        parsed_scripts = script_parser.parse(script_source, "bench.thunder");
        ScriptCompiler compiler{script_symbols, script_registry};
        (void)compiler.compile(parsed_scripts, compiled_scripts, script_diagnostics);
    });
    const auto* hot_script = compiled_scripts.find_script(script_symbols.find("bench_0"));
    ScriptVm script_vm{script_registry};
    std::uint64_t script_true_count = 0;
    const double script_vm_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 5'000'000u; ++i) {
            if (script_vm.evaluate(*hot_script, world, ScopeRef::country(first))) ++script_true_count;
        }
    });

    TerrainClipmap clipmap;
    StrategicCameraState camera;
    camera.center = MercatorProjection::project({-2.0, 54.0});
    std::vector<TerrainPatchInstance> terrain_patches;
    terrain_patches.reserve(clipmap.max_patch_count());
    const double terrain_clipmap_ms = bench_ms([&] {
        for (std::uint64_t i = 0; i < 10'000; ++i) {
            camera.center.x += 17.0;
            camera.center.y += 9.0;
            clipmap.build(camera, terrain_patches);
        }
    });

    TerrainPageCache empty_cache{4096};
    TerrainStreamingPlanner streaming_planner;
    streaming_planner.reserve(terrain_patches.size());
    const double streaming_plan_ms = bench_ms([&] {
        constexpr std::uint64_t budget = 16ull * 1024ull * 1024ull;
        for (std::uint64_t i = 0; i < 10'000; ++i) {
            (void)streaming_planner.plan(terrain_patches, empty_cache, 1u, budget);
        }
    });

    PoliticalMapState political_map;
    political_map.resize(100'000u, 512u);
    political_map.clear_dirty();
    const double political_updates_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 10'000u; ++i) {
            const ProvinceId province{static_cast<ProvinceId::rep_type>((i * 7919u) % 100'000u)};
            political_map.set_owner(province, CountryId{static_cast<CountryId::rep_type>(i % 512u)});
            political_map.set_map_value(province, static_cast<float>(i) * 0.01f);
        }
    });
    const double political_normalize_ms = bench_ms([&] {
        (void)political_map.normalize_dirty(3u);
    });
    const auto political_upload = political_map.normalize_dirty(3u);

    // Raster pages are intentionally large. Keeping several page-sized
    // objects in main's stack frame exceeded the default Windows stack before
    // the benchmark could report anything.
    std::vector<float> coast_source(CoastDistancePage::sample_count);
    for (std::size_t i = 0; i < coast_source.size(); ++i) {
        coast_source[i] = static_cast<float>(static_cast<std::int32_t>(i % 4096u) - 2048) * 1.25f;
    }
    auto coast_page = std::make_unique<CoastDistancePage>();
    const double coast_encode_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 1'000u; ++i) coast_page->encode(coast_source);
    });



    MapModeStore map_modes;
    map_modes.resize(8'000u);
    std::vector<float> mode_values(8'000u);
    for (std::uint32_t i = 0; i < mode_values.size(); ++i) mode_values[i] = 10'000.0f + static_cast<float>(i) * 1234.5f;
    const double map_mode_encode_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 1'000u; ++i) map_modes.set_scalar(MapMode::Gdp, mode_values);
    });
    std::uint64_t map_mode_switch_checksum = 0;
    const double map_mode_switch_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 1'000'000u; ++i) {
            const auto mode = static_cast<MapMode>(2u + (i % 7u));
            const auto view = map_modes.gpu_view(mode);
            map_mode_switch_checksum += view.element_offset + view.element_count;
        }
    });

    PoliticalMapState realistic_map;
    realistic_map.resize(8'000u, 256u);
    realistic_map.clear_dirty();
    const double realistic_conquest_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 100u; ++i) {
            realistic_map.set_owner(ProvinceId{1000u + i}, CountryId{7u});
        }
        (void)realistic_map.normalize_dirty(3u);
    });
    const auto realistic_upload = realistic_map.normalize_dirty(3u);

    std::vector<ProvinceAdjacencyInput> adjacency_edges;
    constexpr std::uint32_t adjacency_provinces = 20'000u;
    adjacency_edges.reserve(adjacency_provinces * 3u);
    for (std::uint32_t i = 0; i < adjacency_provinces; ++i) {
        adjacency_edges.push_back({ProvinceId{i}, ProvinceId{(i + 1u) % adjacency_provinces}, AdjacencyLand, 256u});
        adjacency_edges.push_back({ProvinceId{i}, ProvinceId{(i + 97u) % adjacency_provinces}, AdjacencyLand, 300u});
        adjacency_edges.push_back({ProvinceId{i}, ProvinceId{(i + 401u) % adjacency_provinces}, AdjacencyRiver, 350u});
    }
    ProvinceAdjacencyGraph adjacency;
    const double adjacency_build_ms = bench_ms([&] { adjacency.build(adjacency_provinces, adjacency_edges); });
    std::uint64_t adjacency_checksum = 0;
    const double adjacency_scan_ms = bench_ms([&] {
        for (std::uint32_t repeat = 0; repeat < 100u; ++repeat) {
            for (std::uint32_t i = 0; i < adjacency_provinces; ++i) {
                for (const auto& n : adjacency.neighbors(ProvinceId{i})) adjacency_checksum += n.province;
            }
        }
    });

    TerrainPageCache political_bundle_cache{1024u};
    PoliticalMapStreamingPlanner political_streaming;
    political_streaming.reserve(terrain_patches.size());
    const double political_streaming_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 10'000u; ++i) {
            (void)political_streaming.plan(terrain_patches, political_bundle_cache, 1u, 16ull * 1024ull * 1024ull);
        }
    });


    ProvincePickingCache picking_cache{64u, {1024.0, 7u}};
    auto pick_page = std::make_unique<ProvinceRasterPage>();
    for (std::uint32_t y = 0; y < ProvinceRasterPage::samples_per_side; ++y) {
        for (std::uint32_t x = 0; x < ProvinceRasterPage::samples_per_side; ++x) pick_page->set(x, y, ProvinceId{42u});
    }
    picking_cache.insert({0, 0, 0}, *pick_page, 1u);
    std::uint64_t pick_checksum = 0;
    const double picking_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 1'000'000u; ++i) {
            const double v = static_cast<double>(i % 1024u) + 0.25;
            const auto id = picking_cache.pick({v, v}, 0u, i + 2u);
            if (id.valid()) pick_checksum += id.value();
        }
    });


    const auto worldpack_path = std::filesystem::temp_directory_path() / "thunder_bench_worldpack.thunderworld";
    std::array<std::byte, 64u * 1024u> bundle{};
    WorldPackStats worldpack_write_stats{};
    const double worldpack_write_ms = bench_ms([&] {
        WorldPackWriter writer;
        writer.open(worldpack_path);
        for (std::uint32_t page = 0; page < 256u; ++page) {
            for (std::size_t i = 0; i < bundle.size(); ++i) {
                bundle[i] = static_cast<std::byte>(((i / 128u) + page) & 0xffu);
            }
            writer.append({WorldChunkType::ProvinceCoastBundle, 0u,
                           static_cast<std::int32_t>(page & 15u), static_cast<std::int32_t>(page >> 4u), 0u}, bundle);
        }
        worldpack_write_stats = writer.finalize();
    });
    WorldPackReader worldpack;
    worldpack.open(worldpack_path);
    std::uint64_t find_checksum = 0;
    const double worldpack_find_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 1'000'000u; ++i) {
            const std::uint32_t page = (i * 2654435761u) & 255u;
            const auto e = worldpack.find({WorldChunkType::ProvinceCoastBundle, 0u,
                static_cast<std::int32_t>(page & 15u), static_cast<std::int32_t>(page >> 4u), 0u});
            if (e) find_checksum += e->raw_bytes;
        }
    });
    WorldPackDecodeScratch worldpack_scratch{worldpack};
    std::uint64_t decode_checksum = 0;
    const double worldpack_decode_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 10'000u; ++i) {
            const std::uint32_t page = (i * 97u) & 255u;
            const auto data = worldpack_scratch.read({WorldChunkType::ProvinceCoastBundle, 0u,
                static_cast<std::int32_t>(page & 15u), static_cast<std::int32_t>(page >> 4u), 0u});
            decode_checksum += static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[(i * 13u) & (data.size() - 1u)]));
        }
    });
    worldpack.close();
    std::filesystem::remove(worldpack_path);


    JobSystem job_system;
    constexpr std::size_t dense_rows = 5'000'000u;
    constexpr std::size_t dense_grain = 8192u;
    std::vector<float> dense_income(dense_rows);
    std::vector<float> dense_needs(dense_rows);
    std::vector<float> dense_result(dense_rows);
    for (std::size_t i = 0; i < dense_rows; ++i) {
        dense_income[i] = 0.5f + static_cast<float>(i % 4096u) * 0.001f;
        dense_needs[i] = 0.25f + static_cast<float>((i * 17u) % 2048u) * 0.0005f;
    }
    const std::size_t dense_chunks = (dense_rows + dense_grain - 1u) / dense_grain;
    DeterministicReduction<double> dense_serial_reduction;
    dense_serial_reduction.resize(dense_chunks, 0.0);
    const double dense_serial_ms = bench_ms([&] {
        for (std::size_t chunk = 0; chunk < dense_chunks; ++chunk) {
            const std::size_t begin = chunk * dense_grain;
            const std::size_t end = (begin + dense_grain < dense_rows) ? begin + dense_grain : dense_rows;
            double local = 0.0;
            for (std::size_t i = begin; i < end; ++i) {
                const float value = dense_income[i] * 1.013f - dense_needs[i] * 0.997f;
                dense_result[i] = value;
                local += static_cast<double>(value);
            }
            dense_serial_reduction.partial(chunk) = local;
        }
    });
    const double dense_serial_checksum = dense_serial_reduction.fold(0.0, [](double a, double b) { return a + b; });

    DeterministicReduction<double> dense_reduction;
    dense_reduction.resize(dense_chunks, 0.0);
    JobDispatchStats dense_dispatch{};
    const double dense_parallel_ms = bench_ms([&] {
        dense_dispatch = job_system.parallel_for(dense_rows, dense_grain,
            [&](JobContext&, std::size_t chunk, std::size_t begin, std::size_t end) {
                double local = 0.0;
                for (std::size_t i = begin; i < end; ++i) {
                    const float value = dense_income[i] * 1.013f - dense_needs[i] * 0.997f;
                    dense_result[i] = value;
                    local += static_cast<double>(value);
                }
                dense_reduction.partial(chunk) = local;
            });
    });
    const double dense_parallel_checksum = dense_reduction.fold(0.0, [](double a, double b) { return a + b; });

    std::size_t dispatch_touch = 0u;
    const double dispatch_overhead_ms = bench_ms([&] {
        for (std::size_t repeat = 0; repeat < 1'000u; ++repeat) {
            (void)job_system.parallel_for(4096u, 1024u,
                [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
                    dispatch_touch += (end - begin) == 0u ? 1u : 0u;
                });
        }
    });

    TerrainPageCache page_cache{4096};
    for (std::uint32_t i = 0; i < page_cache.capacity(); ++i) {
        (void)page_cache.allocate({static_cast<std::int32_t>(i), 0, 0}, i);
    }
    const double page_eviction_ms = bench_ms([&] {
        for (std::uint32_t i = 0; i < 10'000; ++i) {
            (void)page_cache.allocate({static_cast<std::int32_t>(100'000u + i), 1, 0}, 10'000u + i);
        }
    });

    std::cout << std::fixed << std::setprecision(3)
              << "Thunder microbench (machine-specific, use for regressions only)\n"
              << "100 x 100k-country compact snapshots: " << snapshot_ms << " ms total\n"
              << "snapshot average: " << snapshot_ms / 100.0 << " ms\n"
              << "enqueue 1m commands: " << command_enqueue_ms << " ms\n"
              << "apply 1m commands: " << command_apply_ms << " ms\n"
              << "dirty+recompute 50k modifier chain: " << modifier_ms << " ms\n"
              << "job system parallelism: " << job_system.parallelism() << " slots\n"
              << "5m-row dense kernel serial: " << dense_serial_ms << " ms, checksum " << dense_serial_checksum << "\n"
              << "5m-row dense kernel parallel: " << dense_parallel_ms << " ms, checksum " << dense_parallel_checksum
              << ", workers " << dense_dispatch.workers_used << "\n"
              << "1000 small 4-chunk dispatches: " << dispatch_overhead_ms << " ms, guard " << dispatch_touch << "\n"
              << "parse+compile 10k ThunderScripts: " << script_compile_ms << " ms, programs " << compiled_scripts.script_count() << "\n"
              << "5m compiled 2-trigger ThunderScript evaluations: " << script_vm_ms << " ms, true " << script_true_count << "\n"
              << "ThunderScript compiled instruction bytes: " << compiled_scripts.instruction_bytes() << "\n"
              << "10k terrain clipmap rebuilds: " << terrain_clipmap_ms << " ms total\n"
              << "terrain clipmap average: " << terrain_clipmap_ms / 10'000.0 << " ms\n"
              << "terrain patch count: " << terrain_patches.size() << "\n"
              << "terrain patch bytes: " << terrain_patches.size() * sizeof(TerrainPatchInstance) << "\n"
              << "terrain estimated triangles: " << clipmap.build(camera, terrain_patches).estimated_triangles << "\n"
              << "10k terrain streaming plans: " << streaming_plan_ms << " ms total\n"
              << "streaming plan average: " << streaming_plan_ms / 10'000.0 << " ms\n"
              << "10k evictions from 4096-page terrain cache: " << page_eviction_ms << " ms total\n"
              << "eviction average: " << page_eviction_ms / 10'000.0 << " ms\n"
              << "height page bytes: " << TerrainHeightPage::sample_count * sizeof(std::uint16_t) << "\n"
              << "10k political owner+map-value edits: " << political_updates_ms << " ms\n"
              << "political dirty normalization: " << political_normalize_ms << " ms\n"
              << "political upload bytes after 10k random edits: " << political_upload.total_bytes() << "\n"
              << "1000 coast-SDF page encodes: " << coast_encode_ms << " ms\n"
              << "province raster page bytes: " << sizeof(ProvinceRasterPage::Storage) << "\n"
              << "coast SDF page bytes: " << sizeof(CoastDistancePage::Storage) << "\n"
              << "100-province conquest on 8k map: " << realistic_conquest_ms << " ms, upload " << realistic_upload.total_bytes() << " bytes\n"
              << "1000 x 8k GDP map-mode encodes: " << map_mode_encode_ms << " ms total\n"
              << "1m resident map-mode switches: " << map_mode_switch_ms << " ms, checksum " << map_mode_switch_checksum << "\n"
              << "8k-province all-mode GPU data bytes: " << map_modes.memory_bytes() << "\n"
              << "20k-province CSR adjacency build: " << adjacency_build_ms << " ms\n"
              << "100 full adjacency scans: " << adjacency_scan_ms << " ms, checksum " << adjacency_checksum << "\n"
              << "adjacency CSR bytes: " << adjacency.memory_bytes() << "\n"
              << "10k political page streaming plans: " << political_streaming_ms << " ms total\n"
              << "1m CPU province picks: " << picking_ms << " ms, checksum " << pick_checksum << "\n"
              << "province picking cache bytes (64 pages): " << picking_cache.memory_bytes() << "\n"
              << "256 x 64KiB world-pack compile: " << worldpack_write_ms << " ms, payload ratio " << worldpack_write_stats.compression_ratio() << "\n"
              << "1m world-pack binary index lookups: " << worldpack_find_ms << " ms, checksum " << find_checksum << "\n"
              << "10k cached pread+Zstd page decodes: " << worldpack_decode_ms << " ms, checksum " << decode_checksum << "\n"
              << "world-pack scratch capacities: stored=" << worldpack_scratch.stored_capacity() << " decoded=" << worldpack_scratch.decoded_capacity() << "\n"
              << "snapshot record bytes: " << sizeof(CountryRenderRecord) << "\n";
}
