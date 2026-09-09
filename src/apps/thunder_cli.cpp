#include "thunder/presentation/render/RenderSnapshotBuilder.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/foundation/geo/MercatorProjection.hpp"
#include "thunder/presentation/render/terrain/TerrainClipmap.hpp"
#include "thunder/presentation/render/terrain/TerrainHeightPage.hpp"
#include "thunder/presentation/render/terrain/TerrainPageCache.hpp"
#include "thunder/presentation/render/terrain/TerrainStreamingPlanner.hpp"
#include "thunder/presentation/render/map/PoliticalMapState.hpp"
#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"
#include "thunder/presentation/render/map/CoastDistancePage.hpp"
#include "thunder/presentation/render/vulkan/VulkanProbe.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/simulation/kernel/CommandQueue.hpp"
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/kernel/ModifierGraph.hpp"
#include "thunder/simulation/kernel/TickScheduler.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    using namespace thunder;

    if (argc > 1) {
        std::string_view arg{argv[1]};
        if (arg == "--script-docs") {
            std::cout << ScriptRegistry::make_builtin().export_markdown_docs();
            return 0;
        }
        if (arg == "--script-schema") {
            std::cout << ScriptRegistry::make_builtin().export_schema_json();
            return 0;
        }
    }

    World world;
    const auto gbr = world.countries.create({"GBR", 26'000'000.0, 100.0, 18.0, 0.20});
    const auto fra = world.countries.create({"FRA", 34'000'000.0, 92.0, 12.0, 0.22});

    CommandQueue commands;
    commands.enqueue(CommandType::SetTaxRate, gbr, 0.18);
    commands.apply_all(world);

    auto scripts = ScriptRegistry::make_builtin();
    if (scripts.evaluate_trigger("population_above", world, ScopeRef::country(gbr), 20'000'000.0)) {
        // NOTE: Deliberately not injecting phantom money. In a real game,
        // treasury changes must come from the closed-loop economy settlement.
    }

    ModifierGraph modifiers;
    const auto base_tax = modifiers.add_source("base_tax", 1.0);
    const auto admin_efficiency = modifiers.add_source("admin_efficiency", 0.85);
    const auto effective_tax = modifiers.add_derived("effective_tax", {base_tax, admin_efficiency},
        [base_tax, admin_efficiency](const ModifierGraph& g) {
            return g.value(base_tax) * g.value(admin_efficiency);
        });

    TickScheduler scheduler;
    scheduler.add({"population_growth", TickFrequency::Weekly, {}, [gbr, fra](TickContext& ctx) {
        for (const auto id : {gbr, fra}) {
            const auto p = ctx.world.countries.population(id);
            ctx.world.countries.set_population(id, p * 1.00012);
        }
    }});
    // REMOVED: Former "country_budget" scheduler that injected phantom money
    // from GDP * tax_rate directly into treasury. In the closed-loop economy,
    // all treasury income comes exclusively from tax deductions on POP cash
    // during the EconomySystem::settlement() phase.
    scheduler.compile();

    GameClock clock;
    JobSystem jobs;
    TickExecutionProfile tick_profile;
    for (int i = 0; i < 28; ++i) {
        clock.advance_tick();
        TickContext context{world, clock};
        scheduler.run_due_parallel(context, jobs, &tick_profile);
    }

    const auto snapshot = build_render_snapshot(world);

    StrategicCameraState camera;
    camera.center = MercatorProjection::project({-2.0, 54.0});
    TerrainClipmap clipmap;
    std::vector<TerrainPatchInstance> terrain_patches;
    terrain_patches.reserve(clipmap.max_patch_count());
    const auto terrain_stats = clipmap.build(camera, terrain_patches);
    TerrainPageCache height_cache{4096};
    TerrainStreamingPlanner planner;
    const auto initial_uploads = planner.plan(terrain_patches, height_cache, 1u, 16ull * 1024ull * 1024ull);

    PoliticalMapState political_map;
    political_map.resize(8'000u, 256u);
    political_map.clear_dirty();
    political_map.set_owner(ProvinceId{100u}, gbr);
    political_map.set_country_color(gbr, {170u, 42u, 52u, 255u});
    political_map.set_map_value(ProvinceId{100u}, 12.4f);
    const auto map_upload = political_map.normalize_dirty();
    const auto vulkan = probe_vulkan_loader();

    std::cout << "Thunder Engine 1.0 Development\n";
    std::cout << "Date: " << clock.to_string() << "\n";
    std::cout << "World checksum: 0x" << std::hex << snapshot.world_checksum << std::dec << "\n";
    std::cout << "Effective tax modifier: " << modifiers.value(effective_tax) << "\n";
    std::cout << "Simulation worker slots: " << jobs.parallelism()
              << " last_tick_waves=" << tick_profile.waves.size()
              << " tick_profile_us=" << std::chrono::duration<double, std::micro>(tick_profile.total).count() << "\n";
    std::cout << "Terrain patches: " << terrain_stats.patch_count
              << " payload=" << terrain_stats.bytes << " bytes"
              << " triangles=" << terrain_stats.estimated_triangles << "\n";
    std::cout << "Initial height upload requests: " << initial_uploads.size()
              << " page_bytes=" << TerrainHeightPage::sample_count * sizeof(std::uint16_t) << "\n";
    std::cout << "Province ID page: " << sizeof(ProvinceRasterPage::Storage)
              << " bytes, Coast SDF page: " << sizeof(CoastDistancePage::Storage) << " bytes\n";
    std::cout << "Sparse political update upload: " << map_upload.total_bytes() << " bytes\n";
    std::cout << "Vulkan loader: " << (vulkan.loader_found ? "yes" : "no")
              << " API " << vulkan.api_major() << '.' << vulkan.api_minor() << '.' << vulkan.api_patch()
              << " instance=" << (vulkan.instance_created ? "yes" : "no")
              << " devices=" << vulkan.physical_device_count << "\n";
    for (std::size_t i = 0; i < world.countries.size(); ++i) {
        CountryId id{static_cast<CountryId::rep_type>(i)};
        std::cout << world.countries.tag(id)
                  << " pop=" << std::fixed << std::setprecision(0) << world.countries.population(id)
                  << " treasury=" << std::setprecision(3) << world.countries.treasury(id)
                  << " tax=" << std::setprecision(2) << world.countries.tax_rate(id) << '\n';
    }
    return 0;
}
