#include "thunder/foundation/memory/FrameArena.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/foundation/jobs/DeterministicReduction.hpp"
#include "thunder/foundation/jobs/StablePartition.hpp"
#include "thunder/foundation/base/DeterministicRng.hpp"
#include "thunder/foundation/base/FpEnvironment.hpp"
#include "thunder/presentation/render/FrameRing.hpp"
#include "thunder/presentation/render/GpuCapabilities.hpp"
#include "thunder/presentation/render/StrategicCamera.hpp"
#include "thunder/presentation/render/map/CoastDistancePage.hpp"
#include "thunder/presentation/render/map/DirtySpanSet.hpp"
#include "thunder/presentation/render/map/PoliticalMapRenderPlan.hpp"
#include "thunder/presentation/render/map/PoliticalMapState.hpp"
#include "thunder/presentation/render/map/MapModeStore.hpp"
#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"
#include "thunder/presentation/render/map/PoliticalMapStreamingPlanner.hpp"
#include "thunder/presentation/render/map/ProvincePickingCache.hpp"
#include "thunder/presentation/render/map/PoliticalMapPageBundle.hpp"
#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/content/worldpack/WorldPackMetadata.hpp"
#include "thunder/presentation/render/map/WorldMapPicker.hpp"
#include "TestTempPath.hpp"
#include "thunder/presentation/render/vulkan/VulkanProbe.hpp"
#include "thunder/presentation/render/terrain/StreamingBudget.hpp"
#include "thunder/presentation/render/terrain/TerrainClipmap.hpp"
#include "thunder/presentation/render/terrain/TerrainHeightPage.hpp"
#include "thunder/presentation/render/terrain/TerrainPageCache.hpp"
#include "thunder/presentation/render/terrain/TerrainStreamingPlanner.hpp"
#include "thunder/presentation/render/terrain/TerrainRenderPlan.hpp"
#include "thunder/foundation/geo/MercatorProjection.hpp"
#include "thunder/presentation/render/RenderGraph.hpp"
#include "thunder/presentation/render/RenderSnapshot.hpp"
#include "thunder/presentation/render/SnapshotExchange.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/content/definition/VirtualFileSystem.hpp"
#include "thunder/content/definition/DefinitionDatabase.hpp"
#include "thunder/content/definition/ContentLoader.hpp"
#include "thunder/runtime/engine/ThunderEngine.hpp"
#include "thunder/runtime/engine/GameContentRuntime.hpp"
#include "thunder/simulation/kernel/CommandQueue.hpp"
#include "thunder/simulation/kernel/DeterministicCommandStage.hpp"
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/kernel/ModifierGraph.hpp"
#include "thunder/simulation/kernel/TickScheduler.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include "TestTempPath.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <atomic>
#include <limits>
#include <vector>
#include <thread>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace thunder;

static void test_clock() {
    GameClock clock;
    for (int i = 0; i < 4; ++i) clock.advance_tick();
    assert(clock.date().day == 2);
    assert(clock.is_daily_boundary());
}

static void test_modifier_dirty_propagation() {
    ModifierGraph graph;
    const auto a = graph.add_source("a", 2.0);
    const auto b = graph.add_source("b", 3.0);
    const auto c = graph.add_derived("c", {a, b}, [a, b](const ModifierGraph& g) { return g.value(a) * g.value(b); });
    const auto d = graph.add_derived("d", {c}, [c](const ModifierGraph& g) { return g.value(c) + 1.0; });
    assert(std::abs(graph.value(d) - 7.0) < 1e-9);
    graph.set_source(a, 4.0);
    assert(graph.dirty(c));
    assert(graph.dirty(d));
    graph.recompute_all_dirty();
    assert(std::abs(graph.value(d) - 13.0) < 1e-9);
}

static void test_modifier_deep_chain_is_iterative() {
    ModifierGraph graph;
    const auto source = graph.add_source("source", 1.0);
    auto previous = source;
    constexpr std::size_t depth = 50'000u;
    for (std::size_t i = 1; i < depth; ++i) {
        const auto dependency = previous;
        previous = graph.add_derived("chain", {dependency},
            [dependency](const ModifierGraph& value_graph) {
                return value_graph.value(dependency) + 1.0;
            });
    }
    assert(graph.value(previous) == static_cast<double>(depth));
    graph.set_source(source, 2.0);
    assert(graph.value(previous) == static_cast<double>(depth + 1u));
}

static void test_tick_dag_and_batches() {
    World world;
    GameClock clock;
    TickScheduler scheduler;
    int value = 0;
    scheduler.add({"a", TickFrequency::EveryTick, {}, [&value](TickContext&) { value += 1; }});
    scheduler.add({"b", TickFrequency::EveryTick, {}, [&value](TickContext&) { value += 10; }});
    scheduler.add({"c", TickFrequency::EveryTick, {"a", "b"}, [&value](TickContext&) { value *= 2; }});
    scheduler.compile();
    assert(scheduler.execution_batches().size() == 2);
    assert(scheduler.execution_batches()[0].size() == 2);
    assert(scheduler.execution_batches()[1].size() == 1);
    clock.advance_tick();
    TickContext ctx{world, clock};
    scheduler.run_due(ctx);
    assert(value == 22);
}



static void test_stable_partition_and_keyed_rng_are_hardware_independent() {
    const StablePartition partition{10'003u, 777u};
    assert(partition.grain_size() == 777u);
    assert(partition.chunk_count() == 13u);
    assert(partition.chunk(0u).begin == 0u);
    assert(partition.chunk(12u).end == 10'003u);

    constexpr std::size_t count = 50'000u;
    constexpr std::size_t grain = 511u;
    std::vector<std::uint64_t> serial_values(count);
    std::vector<std::uint64_t> parallel_values(count);
    JobSystem serial{0u};
    JobSystem parallel{4u};
    auto fill = [](JobSystem& jobs, std::span<std::uint64_t> out) {
        (void)jobs.parallel_for(out.size(), grain,
            [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    out[i] = DeterministicRng::keyed_u64(0x1836C0FEu, static_cast<std::uint64_t>(i), 77u);
                }
            });
    };
    fill(serial, serial_values);
    fill(parallel, parallel_values);
    assert(serial_values == parallel_values);
}

static void test_rng_ranges_and_hash_canonicalize_extremes() {
    for (std::uint64_t counter = 0; counter < 256u; ++counter) {
        const auto value = DeterministicRng::keyed_range(
            0x12345678u, 0x9abcdef0u, 10u, 17u, counter);
        assert(value >= 10u && value <= 17u);
    }
    assert(DeterministicRng::keyed_range(1u, 2u, 42u, 42u) == 42u);
    // The full unsigned domain used to turn the range into zero and then
    // divide by zero in the fallback path.
    assert(DeterministicRng::keyed_range(3u, 4u, 0u,
                                         std::numeric_limits<std::uint64_t>::max(), 5u) ==
           DeterministicRng::keyed_u64(3u, 4u, 5u));

    DeterministicRng first{0xfeedbeefu};
    DeterministicRng second{0xfeedbeefu};
    for (int i = 0; i < 64; ++i) {
        const auto a = first.next_i64(std::numeric_limits<std::int64_t>::min(),
                                      std::numeric_limits<std::int64_t>::max());
        const auto b = second.next_i64(std::numeric_limits<std::int64_t>::min(),
                                       std::numeric_limits<std::int64_t>::max());
        assert(a == b);
    }

    Fnv1a64 canonical_a;
    Fnv1a64 canonical_b;
    const auto payload_nan = std::bit_cast<double>(0x7ff8000000000001ull);
    canonical_a.add(std::numeric_limits<double>::quiet_NaN());
    canonical_b.add(payload_nan);
    assert(canonical_a.value() == canonical_b.value());
    Fnv1a64 positive_zero;
    Fnv1a64 negative_zero;
    positive_zero.add(0.0);
    negative_zero.add(-0.0);
    assert(positive_zero.value() == negative_zero.value());
}

static void test_job_system_parallel_for_and_scratch() {
    JobSystem jobs{3u, 64u * 1024u};
    constexpr std::size_t count = 200'000u;
    constexpr std::size_t grain = 1024u;
    std::vector<std::uint64_t> values(count, 0u);
    std::atomic<std::size_t> scratch_touches{0u};
    const auto stats = jobs.parallel_for(count, grain,
        [&](JobContext& context, std::size_t, std::size_t begin, std::size_t end) {
            auto scratch = context.scratch.allocate<std::uint64_t>(1u);
            scratch[0] = static_cast<std::uint64_t>(context.worker_index);
            scratch_touches.fetch_add(1u, std::memory_order_relaxed);
            for (std::size_t i = begin; i < end; ++i) values[i] = static_cast<std::uint64_t>(i * 3u + 1u);
        });
    assert(stats.jobs == (count + grain - 1u) / grain);
    assert(stats.worker_slots == 4u);
    assert(stats.workers_used >= 1u);
    assert(scratch_touches.load(std::memory_order_relaxed) == stats.jobs);
    for (std::size_t i = 0; i < count; ++i) assert(values[i] == static_cast<std::uint64_t>(i * 3u + 1u));
}

static double deterministic_parallel_sum(JobSystem& jobs, std::span<const double> values, std::size_t grain) {
    const std::size_t chunks = (values.size() + grain - 1u) / grain;
    DeterministicReduction<double> reduction;
    reduction.resize(chunks, 0.0);
    (void)jobs.parallel_for(values.size(), grain,
        [&](JobContext&, std::size_t chunk, std::size_t begin, std::size_t end) {
            double local = 0.0;
            for (std::size_t i = begin; i < end; ++i) local += values[i];
            reduction.partial(chunk) = local;
        });
    return reduction.fold(0.0, [](double a, double b) { return a + b; });
}

static void test_deterministic_reduction_across_worker_counts() {
    std::vector<double> values(150'000u);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<double>((i * 17u) % 1009u) * 0.0001 + 0.25;
    }
    JobSystem serial{0u};
    JobSystem parallel{4u};
    const double a = deterministic_parallel_sum(serial, values, 777u);
    const double b = deterministic_parallel_sum(parallel, values, 777u);
    assert(std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b));
}


static void test_job_scratch_rewinds_per_chunk() {
    JobSystem jobs{2u, 1024u};
    (void)jobs.parallel_for(100u, 1u,
        [](JobContext& context, std::size_t, std::size_t, std::size_t) {
            auto bytes = context.scratch.allocate<std::byte>(900u);
            bytes[0] = std::byte{0x2a};
        });
}

static std::uint64_t run_staged_command_scenario(std::size_t background_threads) {
    World world;
    for (std::uint32_t i = 0; i < 8u; ++i) {
        world.countries.create({"TST", 100.0 + i, 10.0, 0.0, 0.1});
    }
    constexpr std::size_t items = 4096u;
    constexpr std::size_t grain = 64u;
    constexpr std::size_t chunks = (items + grain - 1u) / grain;
    DeterministicCommandStage stage;
    stage.resize(chunks, 4u);
    JobSystem jobs{background_threads};
    (void)jobs.parallel_for(items, grain,
        [&](JobContext&, std::size_t chunk, std::size_t begin, std::size_t end) {
            const auto country = CountryId{static_cast<CountryId::rep_type>(chunk % 8u)};
            stage.emit(chunk, CommandType::SetTaxRate, country, 0.10 + static_cast<double>(chunk) * 0.0001);
            stage.emit(chunk, CommandType::AddTreasury, country, static_cast<double>(end - begin));
        });
    CommandQueue queue{stage.pending()};
    assert(stage.flush(queue) == chunks * 2u);
    queue.apply_all(world);
    return world.checksum();
}

static void test_deterministic_command_stage() {
    const auto serial = run_staged_command_scenario(0u);
    const auto parallel = run_staged_command_scenario(4u);
    assert(serial == parallel);
}

static void test_job_system_exception_and_nested_dispatch() {
    JobSystem jobs{2u};
    std::atomic<std::size_t> nested_count{0u};
    (void)jobs.parallel_for(64u, 8u,
        [&](JobContext&, std::size_t, std::size_t, std::size_t) {
            (void)jobs.parallel_for(8u, 2u,
                [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
                    nested_count.fetch_add(end - begin, std::memory_order_relaxed);
                });
        });
    assert(nested_count.load(std::memory_order_relaxed) == 64u);

    bool threw = false;
    try {
        (void)jobs.parallel_for(128u, 16u,
            [](JobContext&, std::size_t chunk, std::size_t, std::size_t) {
                if (chunk == 3u) throw std::runtime_error("job failure");
            });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}


static void test_tick_graph_dot_export() {
    TickScheduler scheduler;
    scheduler.add({"employment", TickFrequency::Weekly, {}, [](TickContext&) {}, TickTaskMode::ParallelSafe});
    scheduler.add({"production", TickFrequency::Weekly, {"employment"}, [](TickContext&) {}, TickTaskMode::Serial});
    scheduler.compile();
    const auto dot = scheduler.to_dot();
    assert(dot.find("digraph ThunderTickGraph") != std::string::npos);
    assert(dot.find("\"employment\" -> \"production\"") != std::string::npos);
    assert(dot.find("parallel-safe") != std::string::npos);
}

static void test_tick_parallel_safe_wave() {
    World world;
    GameClock clock;
    TickScheduler scheduler;
    std::array<int, 9> values{};
    std::atomic<std::size_t> entered{0u};
    std::vector<std::string> dependencies;
    dependencies.reserve(8u);
    for (std::size_t i = 0; i < 8u; ++i) {
        const std::string name = "p" + std::to_string(i);
        dependencies.push_back(name);
        scheduler.add({name, TickFrequency::EveryTick, {}, [&, i](TickContext&) {
            entered.fetch_add(1u, std::memory_order_acq_rel);
            while (entered.load(std::memory_order_acquire) < 2u) std::this_thread::yield();
            values[i] = static_cast<int>(i + 1u);
        }, TickTaskMode::ParallelSafe});
    }
    scheduler.add({"sum", TickFrequency::EveryTick, dependencies, [&](TickContext&) {
        for (std::size_t i = 0; i < 8u; ++i) values[8] += values[i];
    }, TickTaskMode::Serial});
    scheduler.compile();
    clock.advance_tick();
    TickContext context{world, clock};
    JobSystem jobs{2u};
    TickExecutionProfile profile;
    scheduler.run_due_parallel(context, jobs, &profile);
    assert(values[8] == 36);
    assert(profile.waves.size() == 2u);
    assert(profile.waves[0].parallel);
    assert(profile.waves[0].workers_used > 1u);
    assert(profile.waves[0].due_tasks == 8u);
    assert(!profile.waves[1].parallel);
}

static void test_script_scope() {
    World world;
    const auto country = world.countries.create({"TST", 100.0, 10.0, 5.0, 0.2});
    auto scripts = ScriptRegistry::make_builtin();
    assert(scripts.evaluate_trigger("population_above", world, ScopeRef::country(country), 50.0));
    scripts.execute_effect("add_treasury", world, ScopeRef::country(country), 2.0);
    assert(std::abs(world.countries.treasury(country) - 7.0) < 1e-9);
}

static void test_scheduler_cycle_detection() {
    TickScheduler scheduler;
    scheduler.add({"a", TickFrequency::EveryTick, {"b"}, [](TickContext&) {}});
    scheduler.add({"b", TickFrequency::EveryTick, {"a"}, [](TickContext&) {}});
    bool threw = false;
    try { scheduler.compile(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

static void test_command_queue_reuse() {
    World world;
    const auto country = world.countries.create({"TST", 100.0, 10.0, 5.0, 0.2});
    CommandQueue q{4};
    q.enqueue(CommandType::AddTreasury, country, 3.0);
    q.enqueue(CommandType::SetTaxRate, country, 0.25);
    assert(q.pending() == 2);
    q.apply_all(world);
    assert(q.empty());
    assert(std::abs(world.countries.treasury(country) - 8.0) < 1e-9);
    assert(std::abs(world.countries.tax_rate(country) - 0.25) < 1e-9);
    q.enqueue(CommandType::AddTreasury, country, 1.0);
    q.apply_all(world);
    assert(std::abs(world.countries.treasury(country) - 9.0) < 1e-9);
}

static void test_render_snapshot_reuse() {
    World world;
    world.countries.reserve(8);
    const auto a = world.countries.create({"AAA", 100.0, 10.0, 5.0, 0.2});
    world.countries.create({"BBB", 200.0, 20.0, 8.0, 0.3});
    RenderSnapshot snapshot;
    snapshot.reserve(8);
    const auto cap = snapshot.countries.capacity();
    build_render_snapshot(world, snapshot, 7);
    assert(snapshot.generation == 7);
    assert(snapshot.countries.size() == 2);
    assert(snapshot.countries.capacity() == cap);
    assert(snapshot.countries[0].id == a);
    assert(std::abs(snapshot.countries[0].population - 100.0f) < 1e-6f);
}

static void test_frame_arena_and_ring() {
    FrameArena arena{1024};
    auto ints = arena.allocate<std::uint32_t>(16);
    assert(reinterpret_cast<std::uintptr_t>(ints.data()) % alignof(std::uint32_t) == 0);
    assert(arena.used() >= sizeof(std::uint32_t) * 16);
    const auto peak = arena.peak();
    arena.reset();
    assert(arena.used() == 0);
    assert(arena.peak() == peak);

    FrameRing<3> ring{1024};
    auto* f0 = ring.try_begin(0);
    assert(f0 != nullptr);
    ring.submit(*f0, 10);
    auto* f1 = ring.try_begin(0);
    assert(f1 != nullptr);
    ring.submit(*f1, 11);
    auto* f2 = ring.try_begin(0);
    assert(f2 != nullptr);
    ring.submit(*f2, 12);
    assert(ring.try_begin(9) == nullptr);
    assert(ring.try_begin(10) != nullptr);
}


static void test_snapshot_exchange_latest_wins() {
    SnapshotExchange exchange{4};

    auto w0 = exchange.try_begin_write();
    assert(w0);
    w0.snapshot->generation = 1;
    exchange.publish(w0);

    auto w1 = exchange.try_begin_write();
    assert(w1);
    w1.snapshot->generation = 2;
    exchange.publish(w1); // generation 1 is stale and may be reclaimed

    auto read = exchange.try_acquire_latest();
    assert(read);
    assert(read.snapshot->generation == 2);

    // Producer still has two non-reading slots available while renderer holds one.
    auto w2 = exchange.try_begin_write();
    assert(w2);
    w2.snapshot->generation = 3;
    exchange.publish(w2);
    exchange.release(read);

    auto newest = exchange.try_acquire_latest();
    assert(newest);
    assert(newest.snapshot->generation == 3);
    exchange.release(newest);
}

static void test_render_graph() {
    RenderGraph graph;
    const auto terrain = graph.add_resource("terrain", RenderResourceKind::Image);
    const auto depth = graph.add_resource("depth", RenderResourceKind::Image);
    const auto lighting = graph.add_resource("lighting", RenderResourceKind::Image);

    const auto upload = graph.add_pass({"upload", RenderQueue::Transfer, {{terrain, RenderUsage::TransferDst, true}}});
    const auto cull = graph.add_pass({"cull", RenderQueue::Compute, {{depth, RenderUsage::StorageWrite, true}}});
    const auto draw = graph.add_pass({"draw", RenderQueue::Graphics,
                                      {{terrain, RenderUsage::Sampled, false},
                                       {depth, RenderUsage::DepthAttachment, true},
                                       {lighting, RenderUsage::ColorAttachment, true}}});
    const auto present = graph.add_pass({"present", RenderQueue::Graphics, {{lighting, RenderUsage::Present, false}}});
    graph.compile();

    assert(graph.order().size() == 4);
    assert(graph.batches().size() >= 3);
    // Upload and compute cull touch different resources and can record together.
    assert(graph.batches()[0].size() == 2);
    assert(graph.pass_name(upload) == "upload");
    assert(graph.pass_name(cull) == "cull");
    assert(graph.pass_name(draw) == "draw");
    assert(graph.pass_name(present) == "present");
    assert(!graph.barriers().empty());
}



static void test_render_graph_preserves_all_reader_hazards() {
    RenderGraph graph;
    const auto buffer = graph.add_resource("shared", RenderResourceKind::Buffer);
    const auto r0 = graph.add_pass({"reader0", RenderQueue::Graphics, {{buffer, RenderUsage::StorageRead, false}}});
    const auto r1 = graph.add_pass({"reader1", RenderQueue::Graphics, {{buffer, RenderUsage::StorageRead, false}}});
    const auto writer = graph.add_pass({"writer", RenderQueue::Compute, {{buffer, RenderUsage::StorageWrite, true}}});
    graph.compile();
    assert(graph.batches().size() == 2u);
    assert(graph.batches()[0].size() == 2u);
    std::size_t writer_barriers = 0u;
    bool from_r0 = false;
    bool from_r1 = false;
    for (const auto& barrier : graph.barriers()) {
        if (barrier.after == writer) {
            ++writer_barriers;
            from_r0 = from_r0 || barrier.before == r0;
            from_r1 = from_r1 || barrier.before == r1;
        }
    }
    assert(writer_barriers == 2u);
    assert(from_r0 && from_r1);
}

static void test_render_graph_serializes_image_read_layout_changes() {
    RenderGraph graph;
    const auto image = graph.add_resource("image", RenderResourceKind::Image);
    const auto sampled = graph.add_pass({"sampled", RenderQueue::Graphics, {{image, RenderUsage::Sampled, false}}});
    const auto transfer = graph.add_pass({"transfer-read", RenderQueue::Graphics, {{image, RenderUsage::TransferSrc, false}}});
    graph.compile();
    assert(graph.batches().size() == 2u);
    bool found = false;
    for (const auto& barrier : graph.barriers())
        found = found || (barrier.before == sampled && barrier.after == transfer);
    assert(found);
}

static void test_mercator_projection_roundtrip() {
    const GeoCoordinate london{-0.1278, 51.5074};
    const auto world = MercatorProjection::project(london);
    const auto result = MercatorProjection::unproject(world);
    assert(std::abs(result.longitude_deg - london.longitude_deg) < 1e-9);
    assert(std::abs(result.latitude_deg - london.latitude_deg) < 1e-9);

    // Equator baseline
    const auto eq_world = MercatorProjection::project({0.0, 0.0});
    assert(std::abs(eq_world.x) < 1e-9 && std::abs(eq_world.y) < 1e-9);
    const auto eq_geo = MercatorProjection::unproject(eq_world);
    assert(std::abs(eq_geo.longitude_deg) < 1e-9 && std::abs(eq_geo.latitude_deg) < 1e-9);

    // EPSG:3857 standard limits (+/- 85.051129 deg)
    const auto north_world = MercatorProjection::project({0.0, MercatorProjection::max_latitude_deg});
    const auto north_geo = MercatorProjection::unproject(north_world);
    assert(std::abs(north_geo.latitude_deg - MercatorProjection::max_latitude_deg) < 1e-5);

    const auto south_world = MercatorProjection::project({0.0, -MercatorProjection::max_latitude_deg});
    const auto south_geo = MercatorProjection::unproject(south_world);
    assert(std::abs(south_geo.latitude_deg - (-MercatorProjection::max_latitude_deg)) < 1e-5);

    // Extreme polar clamping without singularity or NaN
    const auto pole_over = MercatorProjection::project({0.0, 90.0});
    assert(std::isfinite(pole_over.y) && std::abs(pole_over.y - north_world.y) < 1e-9);

    const auto pole_under = MercatorProjection::project({0.0, -90.0});
    assert(std::isfinite(pole_under.y) && std::abs(pole_under.y - south_world.y) < 1e-9);

    // WorldPackMetadata validation at EPSG:3857 boundaries
    WorldPackMetadata meta;
    meta.schema_version = 1u;
    meta.projection = "mercator";
    meta.page_size = 128u;
    meta.clip_levels = 4u;
    meta.base_page_count_x = 40u;
    meta.base_page_count_y = 28u;
    meta.base_page_world_size_m = 64000.0;
    meta.bounds_wgs84 = {-180.0, -60.0, 180.0, MercatorProjection::max_latitude_deg};
    meta.bounds_world_m = {-20037508.34, -8399737.9, 20037508.34, 19971868.9};
    meta.horizontal_wrap = true;
    assert(meta.valid());
}

static void test_gpu_capability_tiers() {
    GpuCapabilities old_gpu;
    old_gpu.name = "old";
    old_gpu.api_minor = 2;
    const auto rejected = evaluate_gpu(old_gpu);
    assert(!rejected.compatible);
    assert(!rejected.missing_required.empty());

    GpuCapabilities recommended;
    recommended.name = "recommended";
    recommended.type = GpuType::Discrete;
    recommended.api_minor = 4;
    recommended.device_local_bytes = 8ull * 1024ull * 1024ull * 1024ull;
    recommended.dynamic_rendering = true;
    recommended.synchronization2 = true;
    recommended.timeline_semaphore = true;
    recommended.buffer_device_address = true;
    recommended.descriptor_indexing = true;
    recommended.draw_indirect_count = true;
    recommended.shader_draw_parameters = true;
    recommended.sampler_anisotropy = true;
    recommended.storage_buffer_16bit = true;
    recommended.dedicated_compute_queue = true;
    recommended.dedicated_transfer_queue = true;
    recommended.max_sampled_images = 8192;
    recommended.max_image_dimension_2d = 16384;
    const auto accepted = evaluate_gpu(recommended);
    assert(accepted.compatible);
    assert(accepted.tier == GpuTier::HighEnd);
    assert(accepted.score > 1'000);
}

static void test_strategic_camera_precision_and_zoom() {
    StrategicCameraState state;
    state.center = MercatorProjection::project({-2.0, 54.0});
    state.altitude_m = 100'000.0;
    state.viewport_width = 1920;
    state.viewport_height = 1080;
    StrategicCamera camera{state};
    const auto before = camera.ground_meters_per_pixel();
    const auto old_center = camera.state().center;
    camera.zoom_steps(2.0, 0.5, -0.25);
    assert(camera.ground_meters_per_pixel() < before);
    assert(camera.state().center.x != old_center.x || camera.state().center.y != old_center.y);
    camera.pan_pixels(100.0, -40.0);
    assert(std::isfinite(camera.state().center.x));
    assert(std::isfinite(camera.state().center.y));

    // A game can impose a closer zoom limit than the camera's engine-wide
    // minimum. Repeated cursor-focused wheel input at that limit must not pan.
    constexpr double game_min_altitude = 2'800'000.0;
    camera.state().altitude_m = game_min_altitude;
    const auto center_at_close_limit = camera.state().center;
    for (int i = 0; i < 16; ++i) {
        camera.zoom_steps(1.0, 0.85, -0.70,
                          game_min_altitude, StrategicCamera::max_altitude_m);
    }
    assert(camera.state().altitude_m == game_min_altitude);
    assert(camera.state().center.x == center_at_close_limit.x);
    assert(camera.state().center.y == center_at_close_limit.y);
}

static void test_terrain_clipmap_compact_stable_build() {
    TerrainClipmap clipmap;
    StrategicCameraState state;
    state.center = MercatorProjection::project({-2.0, 54.0});

    std::vector<TerrainPatchInstance> patches;
    patches.reserve(clipmap.max_patch_count());
    const auto cap = patches.capacity();
    const auto stats0 = clipmap.build(state, patches);
    assert(stats0.patch_count == clipmap.max_patch_count());
    assert(patches.capacity() == cap);
    assert(stats0.bytes == patches.size() * sizeof(TerrainPatchInstance));
    assert(stats0.patch_count <= 512);
    assert(stats0.estimated_triangles < 1'000'000ull);

    const auto first_origin_x = stats0.snapped_origin_x;
    state.center.x += 32.0; // below the floating-origin snap threshold
    const auto stats1 = clipmap.build(state, patches);
    assert(stats1.snapped_origin_x == first_origin_x);
    assert(patches.capacity() == cap);

    state.center.x += 512.0;
    const auto stats2 = clipmap.build(state, patches);
    assert(stats2.snapped_origin_x != first_origin_x);
}

static void test_terrain_clipmap_configuration_validation() {
    TerrainClipmap valid_clipmap;
    assert(valid_clipmap.max_patch_count() > 0);

    bool threw_zero_levels = false;
    try {
        TerrainClipmap bad{{0u, 8u, 1024.0, 256.0}};
    } catch (const std::invalid_argument&) {
        threw_zero_levels = true;
    }
    assert(threw_zero_levels);

    bool threw_excessive_levels = false;
    try {
        TerrainClipmap bad{{33u, 8u, 1024.0, 256.0}};
    } catch (const std::invalid_argument&) {
        threw_excessive_levels = true;
    }
    assert(threw_excessive_levels);

    bool threw_odd_patches = false;
    try {
        TerrainClipmap bad{{4u, 7u, 1024.0, 256.0}};
    } catch (const std::invalid_argument&) {
        threw_odd_patches = true;
    }
    assert(threw_odd_patches);
}

static void test_streaming_budget_adapts_to_frame_pressure() {
    StreamingBudgetController budget;
    const auto initial = budget.bytes_per_frame();
    for (int i = 0; i < 30; ++i) budget.observe_frame(6.0, 8.0);
    assert(budget.bytes_per_frame() > initial);
    const auto raised = budget.bytes_per_frame();
    for (int i = 0; i < 60; ++i) budget.observe_frame(22.0, 27.0);
    assert(budget.bytes_per_frame() < raised);
}


static void test_height_page_quantization() {
    std::array<float, TerrainHeightPage::sample_count> heights{};
    for (std::size_t i = 0; i < heights.size(); ++i) {
        heights[i] = -200.0f + static_cast<float>(i % TerrainHeightPage::samples_per_side) * 3.17f;
    }
    TerrainHeightPage page;
    page.encode(heights);
    for (std::size_t i = 0; i < heights.size(); ++i) {
        assert(std::abs(page.decode(i) - heights[i]) <= TerrainHeightPage::height_step_m * 0.51f);
    }
}

static void test_terrain_page_cache_and_streaming_plan() {
    TerrainPageCache cache{4};
    const TerrainPatchKey a{1, 1, 0};
    const TerrainPatchKey b{2, 1, 0};
    const TerrainPatchKey c{3, 1, 0};
    const TerrainPatchKey d{4, 1, 0};
    const TerrainPatchKey e{5, 1, 0};
    const auto pa = cache.allocate(a, 1);
    (void)cache.allocate(b, 2);
    (void)cache.allocate(c, 3);
    (void)cache.allocate(d, 4);
    assert(cache.resident_count() == 4);
    assert(cache.find(a) == pa.page);
    cache.touch(a, 100);
    const auto pe = cache.allocate(e, 101);
    assert(pe.evicted.has_value());
    assert(*pe.evicted != a);
    assert(cache.resident(a));
    assert(cache.resident(e));

    TerrainClipmap clipmap{{2, 4, 1024.0, 256.0}};
    StrategicCameraState camera;
    std::vector<TerrainPatchInstance> visible;
    clipmap.build(camera, visible);
    TerrainPageCache empty_cache{128};
    TerrainStreamingPlanner planner;
    planner.reserve(visible.size());
    constexpr std::uint64_t page_bytes = TerrainHeightPage::sample_count * sizeof(std::uint16_t);
    const auto requests = planner.plan(visible, empty_cache, 1u, page_bytes * 3u);
    assert(requests.size() == 3);
    assert(requests[0].key.level <= requests.back().key.level);

    // A resident visible page must be touched by the planner so bounded
    // approximate-LRU eviction does not discard something currently on screen.
    TerrainPageCache recency_cache{2u};
    const TerrainPatchKey hot{10, 10, 0};
    const TerrainPatchKey cold{11, 10, 0};
    const TerrainPatchKey incoming{12, 10, 0};
    (void)recency_cache.allocate(hot, 1u);
    (void)recency_cache.allocate(cold, 2u);
    std::array<TerrainPatchInstance,1> hot_visible{};
    hot_visible[0].key = hot;
    const auto none = planner.plan(hot_visible, recency_cache, 100u, page_bytes);
    assert(none.empty());
    const auto allocation = recency_cache.allocate(incoming, 101u);
    assert(allocation.evicted.has_value());
    assert(*allocation.evicted == cold);
    assert(recency_cache.resident(hot));
}

static void test_terrain_render_plan_dependencies() {
    RenderGraph graph;
    const auto plan = add_terrain_passes(graph);
    (void)plan;
    graph.compile();
    assert(graph.order().size() == 3);
    assert(graph.batches().size() == 3);
    assert(!graph.barriers().empty());
}


static void test_dirty_span_set_sparse_coalescing() {
    DirtySpanSet dirty;
    dirty.mark(2u);
    dirty.mark(3u);
    dirty.mark(100u, 2u);
    dirty.mark(105u);
    dirty.normalize(3u);
    const auto spans = dirty.spans();
    assert(spans.size() == 2u);
    assert((spans[0] == DirtySpan{2u, 2u}));
    assert((spans[1] == DirtySpan{100u, 6u}));
    assert(dirty.element_count() == 8u);
}

static void test_province_raster_page_compact_ids() {
    ProvinceRasterPage page;
    page.set(7u, 9u, ProvinceId{41u});
    assert(page.encoded(7u, 9u) == 42u);
    assert(page.sample(7u, 9u) == ProvinceId{41u});
    page.set_water(7u, 9u);
    assert(!page.sample(7u, 9u).valid());
    assert(sizeof(ProvinceRasterPage::Storage) == 32u * 1024u);
}

static void test_coast_distance_page_quantization() {
    std::array<float, CoastDistancePage::sample_count> source{};
    source[0] = -3200.25f;
    source[1] = 1234.75f;
    source[2] = CoastDistancePage::max_distance_m * 2.0f;
    CoastDistancePage page;
    page.encode(source);
    assert(std::abs(page.decode(0) - source[0]) <= CoastDistancePage::distance_step_m * 0.51f);
    assert(std::abs(page.decode(1) - source[1]) <= CoastDistancePage::distance_step_m * 0.51f);
    assert(page.decode(2) <= CoastDistancePage::max_distance_m);
}

static void test_political_map_updates_are_indirect_and_sparse() {
    PoliticalMapState map;
    map.resize(100'000u, 256u);
    map.clear_dirty();
    const CountryId gbr{7u};
    const CountryId fra{8u};
    map.set_country_color(gbr, {180u, 45u, 52u, 255u});
    map.set_owner(ProvinceId{100u}, gbr);
    map.set_owner(ProvinceId{101u}, gbr);
    map.set_owner(ProvinceId{50'000u}, fra);
    map.set_map_value(ProvinceId{100u}, 42.0f);
    map.set_map_value(ProvinceId{101u}, 43.0f);
    const auto stats = map.normalize_dirty(2u);
    assert(map.owner(ProvinceId{100u}) == gbr);
    assert(map.owner_dirty().size() == 2u); // two local edits + one distant edit
    assert(stats.owner_bytes < 128u);
    assert(stats.country_color_bytes == sizeof(Rgba8));
    assert(stats.map_value_bytes <= 4u * sizeof(float));
}

static void test_political_map_render_plan() {
    RenderGraph graph;
    const auto terrain = add_terrain_passes(graph);
    const auto map = add_political_map_passes(graph, terrain.depth, terrain.hdr_color);
    (void)map;
    graph.compile();
    assert(graph.order().size() == 5u);
    assert(!graph.barriers().empty());
}

static void test_vulkan_probe_is_safe_without_sdk() {
    const auto result = probe_vulkan_loader();
    if (result.loader_found) {
        assert(result.loader_api_version != 0u);
        if (result.instance_created) assert(result.create_instance_result == 0);
    }
}


static void test_political_map_streaming_is_budgeted_in_atomic_bundles() {
    TerrainClipmap clipmap{{2u, 4u, 1024.0, 256.0}};
    StrategicCameraState camera;
    std::vector<TerrainPatchInstance> visible;
    clipmap.build(camera, visible);
    TerrainPageCache cache{128u};
    PoliticalMapStreamingPlanner planner;
    planner.reserve(visible.size());
    const auto requests = planner.plan(visible, cache, 1u, PoliticalMapStreamingPlanner::bundle_bytes * 2ull);
    assert(requests.size() == 2u);
    assert(requests[0].estimated_bytes == PoliticalMapStreamingPlanner::bundle_bytes);
}

static void test_province_adjacency_csr() {
    const std::array<ProvinceAdjacencyInput, 5> edges{{
        {ProvinceId{0u}, ProvinceId{1u}, AdjacencyLand, 256u},
        {ProvinceId{1u}, ProvinceId{2u}, AdjacencyRiver, 300u},
        {ProvinceId{2u}, ProvinceId{3u}, AdjacencyStrait, 512u},
        {ProvinceId{1u}, ProvinceId{2u}, AdjacencyLand, 280u}, // duplicate GIS edge
        {ProvinceId{3u}, ProvinceId{4u}, AdjacencyLand, 256u}
    }};
    ProvinceAdjacencyGraph graph;
    graph.build(5u, edges);
    assert(graph.directed_edge_count() == 8u);
    assert(graph.neighbors(ProvinceId{1u}).size() == 2u);
    assert(graph.adjacent(ProvinceId{1u}, ProvinceId{2u}));
    assert(!graph.adjacent(ProvinceId{0u}, ProvinceId{4u}));
    const auto n = graph.neighbors(ProvinceId{1u});
    const auto it = std::find_if(n.begin(), n.end(), [](const ProvinceNeighbor& x) { return x.province == 2u; });
    assert(it != n.end());
    assert((it->flags & AdjacencyRiver) != 0u);
    assert((it->flags & AdjacencyLand) != 0u);
    assert(it->base_cost_q8 == 280u);
    assert(graph.memory_bytes() < 128u);
}


static void test_cpu_province_picking_avoids_gpu_readback() {
    ProvincePickingCache cache{4u, {1024.0, 3u}};
    const WorldMeters point{-100.0, 100.0};
    const auto key = cache.key_for_world(point, 0u);
    assert(key.x == -1);
    assert(key.y == 0);

    ProvinceRasterPage page;
    const auto tx = static_cast<std::uint32_t>((924.0 / 1024.0) * ProvinceRasterPage::samples_per_side);
    const auto ty = static_cast<std::uint32_t>(((1024.0 - 100.0) / 1024.0) * ProvinceRasterPage::samples_per_side);
    page.set(tx, ty, ProvinceId{321u});
    cache.insert(key, page, 1u);
    assert(cache.pick(point, 0u, 2u) == ProvinceId{321u});
    assert(cache.resident_count() == 1u);
    assert(cache.memory_bytes() < 200u * 1024u);

    ProvinceRasterPage coarse;
    const WorldMeters coarse_point{2500.0, 2500.0};
    const auto coarse_key = cache.key_for_world(coarse_point, 2u);
    const auto coarse_tx = static_cast<std::uint32_t>((2500.0 / 4096.0) * ProvinceRasterPage::samples_per_side);
    const auto coarse_ty = static_cast<std::uint32_t>(((4096.0 - 2500.0) / 4096.0) * ProvinceRasterPage::samples_per_side);
    coarse.set(coarse_tx, coarse_ty, ProvinceId{99u});
    cache.insert(coarse_key, coarse, 3u);
    assert(cache.pick_with_fallback(coarse_point, 0u, 4u) == ProvinceId{99u});
}


static void test_world_pack_random_access_and_reused_decode_scratch() {
    const auto path = thunder_test::unique_temp_path("thunder_worldpack_test.thunderworld");
    std::array<std::byte, 64u * 1024u> compressible{};
    for (std::size_t i = 0; i < compressible.size(); ++i) {
        compressible[i] = static_cast<std::byte>((i / 4096u) & 0x0fu);
    }
    std::array<std::byte, 8450u> height{};
    for (std::size_t i = 0; i < height.size(); ++i) height[i] = static_cast<std::byte>((i * 73u + 19u) & 0xffu);

    WorldPackWriter writer;
    writer.open(path);
    writer.append({WorldChunkType::Metadata, 0u, 0, 0, 0u}, std::as_bytes(std::span{"test", std::size_t{4}}));
    writer.append({WorldChunkType::ProvinceCoastBundle, 0u, 1, 2, 0u}, compressible);
    writer.append({WorldChunkType::TerrainHeightPage, 0u, 1, 2, 0u}, height);
    const auto write_stats = writer.finalize();
    assert(write_stats.chunk_count == 3u);
    assert(write_stats.stored_bytes <= write_stats.raw_bytes);

    WorldPackReader reader;
    reader.open(path);
    assert(reader.index().size() == 3u);
    const WorldChunkKey bundle_key{WorldChunkType::ProvinceCoastBundle, 0u, 1, 2, 0u};
    assert(reader.contains(bundle_key));
    const auto bundle_entry = reader.find(bundle_key);
    assert(bundle_entry.has_value());
    if (bundle_entry->codec == WorldChunkCodec::Zstd)
        assert(bundle_entry->stored_bytes < bundle_entry->raw_bytes);
    else
        assert(bundle_entry->stored_bytes == bundle_entry->raw_bytes);
    WorldPackDecodeScratch scratch{reader};
    const auto a = scratch.read(bundle_key);
    assert(a.size() == compressible.size());
    assert(std::equal(a.begin(), a.end(), compressible.begin(), compressible.end()));
    const auto first_stored_capacity = scratch.stored_capacity();
    const auto b = scratch.read(bundle_key);
    assert(b.size() == compressible.size());
    assert(scratch.stored_capacity() == first_stored_capacity);

    const auto raw = reader.read({WorldChunkType::TerrainHeightPage, 0u, 1, 2, 0u});
    assert(raw.size() == height.size());
    assert(std::equal(raw.begin(), raw.end(), height.begin(), height.end()));
    reader.close();
    std::filesystem::remove(path);
}



static void write_bytes_at(const std::filesystem::path& path, std::uint64_t offset,
                           std::span<const std::byte> bytes) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    assert(file);
    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(file);
}

static void test_world_pack_rejects_corrupt_metadata() {
    const auto base = thunder_test::unique_temp_path("thunder_worldpack_corrupt_base.thunderworld");
    const auto bad_hash = thunder_test::unique_temp_path("thunder_worldpack_corrupt_hash.thunderworld");
    const auto bad_codec = thunder_test::unique_temp_path("thunder_worldpack_corrupt_codec.thunderworld");
    const auto bad_offset = thunder_test::unique_temp_path("thunder_worldpack_corrupt_offset.thunderworld");
    std::array<std::byte, 64> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::byte>(i);

    WorldPackWriter writer;
    writer.open(base);
    writer.append({WorldChunkType::Metadata, 0u, 0, 0, 0u}, payload);
    writer.finalize();

    std::filesystem::copy_file(base, bad_hash, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(base, bad_codec, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(base, bad_offset, std::filesystem::copy_options::overwrite_existing);

    // Header build hash begins at byte 56.
    {
        std::fstream f(bad_hash, std::ios::binary | std::ios::in | std::ios::out);
        f.seekg(56, std::ios::beg);
        char c = 0;
        f.read(&c, 1);
        c = static_cast<char>(static_cast<unsigned char>(c) ^ 0xA5u);
        f.seekp(56, std::ios::beg);
        f.write(&c, 1);
    }

    constexpr std::uint64_t index_entry_bytes_for_test = 48u;
    const auto index_offset = std::filesystem::file_size(base) - index_entry_bytes_for_test;
    const std::array<std::byte,1> invalid_codec{{std::byte{0x7Fu}}};
    write_bytes_at(bad_codec, index_offset + 16u, invalid_codec);

    const std::array<std::byte,8> zero_offset{};
    write_bytes_at(bad_offset, index_offset + 20u, zero_offset);

    for (const auto& path : {bad_hash, bad_codec, bad_offset}) {
        bool rejected = false;
        try {
            WorldPackReader reader;
            reader.open(path);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        assert(rejected);
    }

    std::filesystem::remove(base);
    std::filesystem::remove(bad_hash);
    std::filesystem::remove(bad_codec);
    std::filesystem::remove(bad_offset);
}

static void test_map_mode_store_keeps_all_modes_gpu_resident() {
    MapModeStore modes;
    modes.resize(8'000u);
    assert(modes.memory_bytes() == 8'000u * 7u * sizeof(std::uint16_t));

    std::vector<float> population(8'000u);
    for (std::uint32_t i = 0; i < population.size(); ++i) population[i] = 1000.0f + static_cast<float>(i) * 250.0f;
    modes.set_scalar(MapMode::Population, population);
    const auto pop_view = modes.gpu_view(MapMode::Population);
    assert(pop_view.kind == MapModeValueKind::ScalarUnorm16);
    assert(pop_view.element_count == 8'000u);
    assert(pop_view.generation == 1u);
    assert(std::abs(modes.decode_scalar(MapMode::Population, ProvinceId{4'000u}) - population[4'000u]) / population[4'000u] < 0.002f);

    std::vector<std::uint16_t> markets(8'000u);
    for (std::uint32_t i = 0; i < markets.size(); ++i) markets[i] = static_cast<std::uint16_t>(i % 73u);
    modes.set_categories(MapMode::Market, markets);
    assert(modes.category(MapMode::Market, ProvinceId{1234u}) == markets[1234u]);
    const auto political = modes.gpu_view(MapMode::Political);
    assert(political.kind == MapModeValueKind::Owner);
    assert(political.element_count == 0u); // owner indirection lives in PoliticalMapState
}


static void test_political_page_bundle_decodes_without_format_translation() {
    std::array<std::byte, PoliticalMapPageBundleView::raw_bytes> bytes{};
    // Province encoded ID 43 => runtime ProvinceId 42.
    bytes[0] = std::byte{43};
    bytes[1] = std::byte{0};
    const std::size_t coast = PoliticalMapPageBundleView::province_bytes;
    const std::int16_t q = -2468;
    const auto uq = static_cast<std::uint16_t>(q);
    bytes[coast] = static_cast<std::byte>(uq & 0xffu);
    bytes[coast + 1u] = static_cast<std::byte>((uq >> 8u) & 0xffu);

    PoliticalMapPageBundleView view{bytes};
    ProvinceRasterPage province;
    CoastDistancePage coast_page;
    view.decode_province(province);
    view.decode_coast(coast_page);
    assert(province.sample(0u, 0u) == ProvinceId{42u});
    assert(std::abs(coast_page.decode(0u) - static_cast<float>(q) * CoastDistancePage::distance_step_m) < 0.001f);
}


static void test_world_pack_build_hash_is_order_independent() {
    const auto a_path = thunder_test::unique_temp_path("thunder_worldpack_hash_a.thunderworld");
    const auto b_path = thunder_test::unique_temp_path("thunder_worldpack_hash_b.thunderworld");
    std::array<std::byte, 512> a{};
    std::array<std::byte, 512> b{};
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = static_cast<std::byte>(i & 0xffu);
        b[i] = static_cast<std::byte>((i * 31u) & 0xffu);
    }
    const WorldChunkKey ka{WorldChunkType::ProvinceCoastBundle, 0u, 2, 1, 0u};
    const WorldChunkKey kb{WorldChunkType::ProvinceCoastBundle, 0u, 1, 2, 0u};
    WorldPackWriter wa;
    wa.open(a_path);
    wa.append(ka, a);
    wa.append(kb, b);
    const auto sa = wa.finalize();
    WorldPackWriter wb;
    wb.open(b_path);
    wb.append(kb, b);
    wb.append(ka, a);
    const auto sb = wb.finalize();
    assert(sa.build_hash != 0u);
    assert(sa.build_hash == sb.build_hash);
    WorldPackReader ra; ra.open(a_path);
    WorldPackReader rb; rb.open(b_path);
    assert(ra.stats().build_hash == rb.stats().build_hash);
    ra.close();
    rb.close();
    std::filesystem::remove(a_path);
    std::filesystem::remove(b_path);
}


static void test_thunderscript_parser_compiler_vm_no_runtime_string_lookup() {
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        script fiscal_relief {
            scope = country
            trigger = {
                population_above = 1000000
                treasury_above = 10
            }
            effect = {
                add_treasury = 5
                set_tax_rate = 0.15
            }
        }
        scripted_value fiscal_score {
            scope = country
            source = treasury
            multiply = 2
            add = 1
        }
    )THUNDER", "unit.thunder");
    assert(parsed.ok());

    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(compiler.compile(parsed, programs, diagnostics));
    assert(diagnostics.empty());
    assert(programs.script_count() == 1u);
    assert(programs.value_count() == 1u);

    World world;
    const auto country = world.countries.create({"TST", 2'000'000.0, 100.0, 20.0, 0.25});
    ScriptVm vm{registry};
    const auto* script = programs.find_script(symbols.find("fiscal_relief"));
    assert(script != nullptr);
    assert(vm.execute_if(*script, world, ScopeRef::country(country)));
    assert(std::abs(world.countries.treasury(country) - 25.0) < 1e-9);
    assert(std::abs(world.countries.tax_rate(country) - 0.15) < 1e-9);

    const auto* value = programs.find_value(symbols.find("fiscal_score"));
    assert(value != nullptr);
    assert(std::abs(vm.evaluate(*value, world, ScopeRef::country(country)) - 51.0) < 1e-9);
    assert(programs.instruction_bytes() < 1024u);
}

static void test_thunderscript_scope_errors_are_compile_time() {
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        script invalid_scope {
            scope = province
            trigger = { treasury_above = 1 }
        }
    )THUNDER");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(!compiler.compile(parsed, programs, diagnostics));
    assert(!diagnostics.empty());
    assert(programs.script_count() == 0u);
}

static void test_definition_database_history_and_immutable_runtime_split() {
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        country GBR {
            population = 26000000
            gdp = 510
            treasury = 19
            tax_rate = 0.20
        }
        history GBR {
            date = 1836.01.01
            effect = {
                set_tax_rate = 0.18
                add_treasury = 2
            }
        }
    )THUNDER");
    assert(parsed.ok());
    DefinitionDatabase definitions{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(definitions.ingest(parsed, diagnostics));
    assert(definitions.compile_scripts(parsed, diagnostics));
    assert(diagnostics.empty());
    assert(definitions.countries().size() == 1u);

    World world;
    definitions.instantiate_world(world);
    const auto gbr = definitions.runtime_country(symbols.find("GBR"));
    assert(gbr.valid());
    assert(std::abs(world.countries.tax_rate(gbr) - 0.20) < 1e-9);
    definitions.apply_history(18360101, world);
    assert(std::abs(world.countries.tax_rate(gbr) - 0.18) < 1e-9);
    assert(std::abs(world.countries.treasury(gbr) - 21.0) < 1e-9);
    assert(definitions.immutable_bytes() > sizeof(CountryDefinition));
}

static void test_mod_vfs_highest_priority_wins_deterministically() {
    const auto root = thunder_test::unique_temp_path("thunder_vfs_test");
    const auto base = root / "base";
    const auto mod = root / "mod";
    std::filesystem::create_directories(base / "common/countries");
    std::filesystem::create_directories(mod / "common/countries");
    {
        std::ofstream out(base / "common/countries/GBR.thunder");
        out << "country GBR { treasury = 10 }\n";
    }
    {
        std::ofstream out(mod / "common/countries/GBR.thunder");
        out << "country GBR { treasury = 99 }\n";
    }
    {
        std::ofstream out(base / "common/countries/FRA.thunder");
        out << "country FRA { treasury = 11 }\n";
    }

    VirtualFileSystem vfs;
    vfs.mount({"base", base, 0});
    vfs.mount({"mod", mod, 100});
    const auto files = vfs.enumerate();
    assert(files.size() == 2u);
    assert(files[0].logical_path == "common/countries/FRA.thunder");
    assert(files[1].logical_path == "common/countries/GBR.thunder");
    assert(files[1].mount_name == "mod");
    assert(VirtualFileSystem::read_text(files[1]).find("99") != std::string::npos);
    std::filesystem::remove_all(root);
}


static void test_content_loader_compiles_effective_mod_set_and_hashes_it() {
    const auto root = thunder_test::unique_temp_path("thunder_content_loader_test");
    const auto base = root / "base";
    const auto mod = root / "mod";
    std::filesystem::create_directories(base / "common");
    std::filesystem::create_directories(mod / "common");
    {
        std::ofstream out(base / "common/base_game.thunder");
        out << "country GBR { population = 26000000 treasury = 10 }\n"
               "script relief { scope = country trigger = { treasury_above = 5 } effect = { add_treasury = 1 } }\n";
    }
    {
        std::ofstream out(mod / "common/override.thunder");
        out << "country GBR { population = 26000000 treasury = 20 }\n"
               "script relief { scope = country trigger = { treasury_above = 5 } effect = { add_treasury = 2 } }\n";
    }
    VirtualFileSystem vfs;
    vfs.mount({"base", base, 0});
    vfs.mount({"mod", mod, 10});
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    DefinitionDatabase definitions{symbols, registry};
    ContentLoader loader{symbols, registry};
    const auto result = loader.load(vfs, definitions);
    assert(result.ok());
    assert(result.file_count == 2u);
    assert(result.object_count == 4u);
    assert(result.content_hash != 0u);
    World world;
    definitions.instantiate_world(world);
    const auto gbr = definitions.runtime_country(symbols.find("GBR"));
    assert(std::abs(world.countries.treasury(gbr) - 20.0) < 1e-9);
    ScriptVm vm{registry};
    const auto* relief = definitions.scripts().find_script(symbols.find("relief"));
    assert(relief != nullptr);
    assert(vm.execute_if(*relief, world, ScopeRef::country(gbr)));
    assert(std::abs(world.countries.treasury(gbr) - 22.0) < 1e-9);
    std::filesystem::remove_all(root);
}

static void test_economy_definitions_are_script_driven_and_bind_atomically() {
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        good grain { base_price_milli = 1000 category = staple }
        good tools { base_price_milli = 2500 category = industrial }
        good luxury_furniture { base_price_milli = 9000 category = luxury }
        good gold_bullion { base_price_milli = 32000 category = currency monetary_metal = gold }
        construction_goods base {
            input = { good = tools quantity_milli = 40 }
            input = { good = grain quantity_milli = 25 }
        }
        building_type farm {
            workers_per_level = 5000
            input = { good = tools quantity_milli = 50 }
            output = { good = grain quantity_milli = 1200 }
        }
        production_method intensive_farming {
            building_type = farm
            throughput_ppm = 1250000
            input = { good = tools quantity_milli = 90 }
            output = { good = grain quantity_milli = 1700 }
        }
        need_profile workers {
            need = { good = grain quantity_milli = 100 }
        }
    )THUNDER");
    assert(parsed.ok());
    DefinitionDatabase definitions{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(definitions.ingest(parsed, diagnostics));
    EconomyDefinitions economy;
    assert(definitions.bind_economy(economy, diagnostics));
    assert(diagnostics.empty());
    assert(economy.good_count() == 4u);
    assert(economy.building_type_count() == 1u);
    assert(economy.production_method_count() == 1u);
    assert(economy.need_profile_count() == 1u);
    assert(economy.good(GoodId{0}).key == "grain");
    assert(economy.good(GoodId{0}).category == GoodCategory::Staple);
    assert(economy.good(GoodId{1}).key == "tools");
    assert(economy.good(GoodId{1}).category == GoodCategory::Industrial);
    assert(economy.good(GoodId{2}).key == "luxury_furniture");
    assert(economy.good(GoodId{2}).category == GoodCategory::Luxury);
    assert(economy.good(GoodId{3}).key == "gold_bullion");
    assert(economy.good(GoodId{3}).category == GoodCategory::Currency);
    assert(economy.good(GoodId{3}).monetary_metal_key == metal_gold_key);
    assert(economy.find_good_by_monetary_metal(metal_gold_key) == GoodId{3});
    assert(!economy.find_good_by_monetary_metal(metal_silver_key).valid());
    assert(economy.construction_inputs().size() == 2u);
    assert(economy.construction_inputs()[0].good == GoodId{1});
    assert(economy.construction_inputs()[0].quantity_milli_per_1000_workers == 40);
    assert(economy.construction_inputs()[1].good == GoodId{0});
    assert(economy.outputs(BuildingTypeId{0})[0].quantity_milli_per_1000_workers == 1200);
    assert(economy.production_method(ProductionMethodId{0}).throughput_ppm == 1'250'000);

    const auto invalid = parser.parse(R"THUNDER(
        building_type broken {
            output = { good = missing quantity_milli = 1 }
        }
    )THUNDER");
    DefinitionDatabase invalid_definitions{symbols, registry};
    diagnostics.clear();
    assert(invalid_definitions.ingest(invalid, diagnostics));
    EconomyDefinitions unchanged;
    (void)unchanged.add_good({"sentinel", 1});
    assert(!invalid_definitions.bind_economy(unchanged, diagnostics));
    assert(unchanged.good_count() == 1u);
    assert(unchanged.good(GoodId{0}).key == "sentinel");
}

static void test_game_content_runtime_installs_one_script_snapshot() {
    const auto root = thunder_test::unique_temp_path("thunder_script_game_bootstrap");
    std::filesystem::create_directories(root / "common");
    {
        std::ofstream output(root / "common/game.thunder");
        output << R"THUNDER(
            good grain { base_price_milli = 1000 }
            building_type farm {
                output = { good = grain quantity_milli = 1000 }
            }
            need_profile workers {
                need = { good = grain quantity_milli = 100 }
            }
            country TST { population = 1000 treasury = 25 }
            script grant { scope = country effect = { add_treasury = 5 } }
            decision scripted_grant { scope = country effect = grant }
        )THUNDER";
    }

    ThunderEngine engine{{0u, 0u, 0u}};
    VirtualFileSystem vfs;
    vfs.mount({"base", root, 0});
    GameContentRuntime content{engine.scripts()};
    const auto& result = content.load(vfs);
    assert(result.ok());
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(content.install_new_game(engine, 18360101, diagnostics));
    assert(engine.clock().date().year == 1836);
    assert(engine.clock().date().month == 1u);
    assert(engine.clock().date().day == 1u);
    assert(diagnostics.empty());
    assert(engine.definitions().good_count() == 1u);
    assert(engine.definitions().building_type_count() == 1u);
    assert(engine.definitions().need_profile_count() == 1u);
    assert(engine.world().countries.size() == 1u);
    assert(engine.gameplay().definitions().size() == 1u);
    assert(content.installed());
    assert(!content.install_new_game(engine, 18360101, diagnostics));
    std::filesystem::remove_all(root);
}


static void test_thunderscript_boolean_groups_compile_to_rpn() {
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        script grouped {
            scope = country
            trigger = {
                all = {
                    population_above = 100
                    any = {
                        treasury_above = 1000
                        gdp_above = 50
                    }
                    not = {
                        tax_rate_above = 0.50
                    }
                }
            }
        }
    )THUNDER");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(compiler.compile(parsed, programs, diagnostics));
    const auto* grouped = programs.find_script(symbols.find("grouped"));
    assert(grouped != nullptr);
    assert(grouped->condition.size() >= 7u);

    World world;
    const auto c = world.countries.create({"TST", 1000.0, 80.0, 20.0, 0.20});
    ScriptVm vm{registry};
    assert(vm.evaluate(*grouped, world, ScopeRef::country(c)));
    world.countries.set_tax_rate(c, 0.60);
    assert(!vm.evaluate(*grouped, world, ScopeRef::country(c)));
}


static void test_localization_is_symbol_keyed_and_supports_fallback() {
    SymbolTable symbols;
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        localization en {
            GBR = "United Kingdom"
            only_en = "English fallback"
        }
        localization zh_CN {
            GBR = "英国"
        }
    )THUNDER");
    assert(parsed.ok());
    LocalizationDatabase localization{symbols};
    localization.ingest(parsed);
    const auto en = symbols.find("en");
    const auto zh = symbols.find("zh_CN");
    assert(localization.lookup(zh, symbols.find("GBR"), en) == "英国");
    assert(localization.lookup(zh, symbols.find("only_en"), en) == "English fallback");
    assert(localization.entry_count() == 3u);
    assert(localization.memory_bytes() > 0u);
}

static void test_thunderscript_arithmetic_folding() {
    // Value expressions fold at parse time into plain numbers (deterministic):
    // precedence, parentheses, unary minus and division-by-zero diagnostics.
    SymbolTable symbols;
    ThunderScriptParser parser{symbols};
    const auto parsed = parser.parse(R"THUNDER(
        good a { base_price_milli = 1000 * 2 + 500 }
        good b { base_price_milli = (900 - 100) / 4 }
        good c { base_price_milli = -3 + 12 * 3 }
        good d { base_price_milli = 2 * (3 + 4) - 1 }
    )THUNDER");
    assert(parsed.ok());
    assert(parsed.objects.size() == 4u);
    assert(parsed.objects[0].fields[0].kind == ScriptValueKind::Number);
    assert(parsed.objects[0].fields[0].number == 2500.0);
    assert(parsed.objects[1].fields[0].number == 200.0);
    assert(parsed.objects[2].fields[0].number == 33.0);
    assert(parsed.objects[3].fields[0].number == 13.0);

    // Legacy forms keep their meaning: attached signs stay literals, dotted
    // identifiers stay symbols, and division by zero is a parse diagnostic.
    const auto legacy = parser.parse(R"THUNDER(
        good e { base_price_milli = -50 }
        building_type depot-1 { workers_per_level = 100 }
        good f { base_price_milli = 10 / 0 }
    )THUNDER");
    assert(!legacy.ok());
    assert(legacy.objects.size() == 2u);
    assert(legacy.objects[0].fields[0].number == -50.0);
}

static void test_world_picker_is_finest_resident_and_bounded() {
    const auto path = thunder_test::unique_temp_path("thunder_resident_picker_test.thunderworld");
    WorldPackMetadata metadata;
    metadata.bounds_wgs84 = {0.0, 0.0, 0.02, 0.01};
    metadata.bounds_world_m = {0.0, 0.0, 2000.0, 1000.0};
    metadata.base_page_world_size_m = 1000.0;
    metadata.base_page_count_x = 2;
    metadata.base_page_count_y = 1;
    metadata.province_count = 2;
    const auto json = world_pack_metadata_json(metadata);
    {
        WorldPackWriter writer;
        writer.open(path);
        writer.append({WorldChunkType::Metadata, 0, 0, 0, 0}, std::as_bytes(std::span{json}));
        const std::vector<std::byte> mask(128u * 128u, std::byte{0});
        const std::vector<std::byte> height(65u * 65u * 2u, std::byte{0});
        for (std::int32_t x = 0; x < 2; ++x) {
            std::array<std::uint16_t, 128u * 128u * 2u> bundle{};
            for (std::size_t i = 0; i < 128u * 128u; ++i)
                bundle[i] = static_cast<std::uint16_t>(x == 1 || i >= 128u * 64u ? 2u : 1u);
            writer.append({WorldChunkType::ProvinceCoastBundle, 0, x, 0, 0}, std::as_bytes(std::span{bundle}));
            writer.append({WorldChunkType::TerrainHeightPage, 0, x, 0, 0}, height);
            writer.append({WorldChunkType::LakeMask, 0, x, 0, 0}, mask);
            writer.append({WorldChunkType::SpatialMask, 0, x, 0, 0}, mask);
        }
        (void)writer.finalize();
    }
    WorldMapHierarchy hierarchy;
    std::vector<MapLocationRecord> locations(2);
    for (std::size_t i = 0; i < locations.size(); ++i) {
        locations[i].key = "loc_test_" + std::to_string(i);
        locations[i].province = TradeProvinceId{0};
        locations[i].area = AreaId{0};
        locations[i].raster_province = ProvinceId{static_cast<ProvinceId::rep_type>(i)};
    }
    hierarchy.set({{"area"}}, {{"state", AreaId{0}}}, std::move(locations));
    std::string diagnostic;
    WorldMapPicker too_small{0};
    assert(!too_small.open(path, hierarchy, diagnostic));
    assert(!too_small.ready());
    WorldMapPicker picker{2};
    assert(picker.open(path, hierarchy, diagnostic));
    assert(picker.resident_bytes() == 128u * 128u * 2u + 2u * sizeof(std::uint32_t));
    assert(picker.pick_uv(0.25, 0.25, 0, 0).location == LocationId{0});
    assert(picker.pick_uv(0.25, 0.75, 0, 0).location == LocationId{1});
    assert(picker.pick_uv(0.75, 0.25, 15, 0).location == LocationId{1});
    assert(picker.pick_uv(1.0, 0.0, 0, 0).location == LocationId{1});
    assert(!picker.pick_world({-1.0, 500.0}, 0, 0).valid());
    assert(!picker.pick_world({std::numeric_limits<double>::quiet_NaN(), 500.0}, 0, 0).valid());
    assert(!picker.pick_uv(0.5, 1.01, 0, 0).valid());
    picker.close();
    assert(picker.resident_bytes() == 0);
    std::filesystem::remove(path);
}

static void test_fp_environment_probe() {
    const FpEnvironmentReport report = probe_fp_environment();
    // Platform sanity: the probe constants must separate strict IEEE binary64
    // (two roundings -> 0) from fused evaluation (one rounding -> 2^-104) on
    // any conforming platform, regardless of this build's FP flags.
    assert(report.probe_discriminates);
    // Default toolchains run round-to-nearest-even without FTZ/DAZ; a build
    // that flips either silently changes hashed doubles (see DETERMINISM.md).
    assert(report.round_to_nearest);
    assert(!report.denormals_flushed);
    // contraction_active is informational: it depends on the build's FP
    // flags, and flipping it changes the fingerprint below.

    // The fingerprint is stable across repeated calls...
    assert(probe_fp_environment().fingerprint == report.fingerprint);
    assert(fp_environment_fingerprint() == report.fingerprint);
    // ...and identical no matter which worker evaluates it.
    JobSystem serial{0u};
    JobSystem parallel{4u};
    constexpr std::size_t count = 10'000u;
    std::vector<std::uint64_t> serial_values(count, 0u);
    std::vector<std::uint64_t> parallel_values(count, 0u);
    auto fill = [&](JobSystem& jobs, std::span<std::uint64_t> out) {
        (void)jobs.parallel_for(out.size(), 257u,
            [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) out[i] = fp_environment_fingerprint();
            });
    };
    fill(serial, serial_values);
    fill(parallel, parallel_values);
    for (const auto value : serial_values) assert(value == report.fingerprint);
    for (const auto value : parallel_values) assert(value == report.fingerprint);
}

static void test_country_treasury_milli_is_authoritative() {
    CountryStore countries;
    const auto id = countries.create({"TST", 1'000.0, 500.0, 100.0, 0.2});
    const auto derived = [&]() {
        return static_cast<double>(countries.treasury_milli(id)) /
               static_cast<double>(economy_scale);
    };
    // The legacy double column must always be exactly milli/scale: repeated
    // scripted double adds (0.1 + 0.2 == 0.30000000000000004 in binary64)
    // previously left treasury_ drifting from the milli column.
    countries.add_treasury(id, 0.1);
    countries.add_treasury(id, 0.2);
    assert(countries.treasury_milli(id) == 100'300);
    assert(countries.treasury(id) == derived());
    countries.set_treasury(id, 42.0);
    assert(countries.treasury_milli(id) == 42'000);
    assert(countries.treasury(id) == 42.0);
    countries.add_treasury(id, -0.5);
    assert(countries.treasury_milli(id) == 41'500);
    assert(countries.treasury(id) == derived());
    countries.add_treasury_milli(id, -41'500);
    assert(countries.treasury_milli(id) == 0);
    assert(countries.treasury(id) == 0.0);
    // The boundary clamp keeps double->milli conversion clear of int64
    // overflow (the previous llround of an unclamped product was UB).
    countries.set_treasury(id, 1e300);
    assert(countries.treasury_milli(id) == std::numeric_limits<EconomyAmount>::max());
    assert(countries.treasury(id) == derived());
    bool rejected = false;
    try {
        countries.set_treasury(id, std::numeric_limits<double>::quiet_NaN());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    assert(countries.treasury_milli(id) == std::numeric_limits<EconomyAmount>::max());
}

int main() {
    test_world_picker_is_finest_resident_and_bounded();
    test_stable_partition_and_keyed_rng_are_hardware_independent();
    test_rng_ranges_and_hash_canonicalize_extremes();
    test_job_system_parallel_for_and_scratch();
    test_deterministic_reduction_across_worker_counts();
    test_job_system_exception_and_nested_dispatch();
    test_job_scratch_rewinds_per_chunk();
    test_deterministic_command_stage();
    test_tick_parallel_safe_wave();
    test_tick_graph_dot_export();
    test_clock();
    test_modifier_dirty_propagation();
    test_modifier_deep_chain_is_iterative();
    test_tick_dag_and_batches();
    test_script_scope();
    test_scheduler_cycle_detection();
    test_command_queue_reuse();
    test_render_snapshot_reuse();
    test_frame_arena_and_ring();
    test_snapshot_exchange_latest_wins();
    test_render_graph();
    test_render_graph_preserves_all_reader_hazards();
    test_render_graph_serializes_image_read_layout_changes();
    test_mercator_projection_roundtrip();
    test_gpu_capability_tiers();
    test_strategic_camera_precision_and_zoom();
    test_terrain_clipmap_compact_stable_build();
    test_terrain_clipmap_configuration_validation();
    test_height_page_quantization();
    test_terrain_page_cache_and_streaming_plan();
    test_streaming_budget_adapts_to_frame_pressure();
    test_terrain_render_plan_dependencies();
    test_dirty_span_set_sparse_coalescing();
    test_province_raster_page_compact_ids();
    test_coast_distance_page_quantization();
    test_political_map_updates_are_indirect_and_sparse();
    test_political_map_render_plan();
    test_vulkan_probe_is_safe_without_sdk();
    test_political_map_streaming_is_budgeted_in_atomic_bundles();
    test_province_adjacency_csr();
    test_cpu_province_picking_avoids_gpu_readback();
    test_world_pack_random_access_and_reused_decode_scratch();
    test_world_pack_rejects_corrupt_metadata();
    test_map_mode_store_keeps_all_modes_gpu_resident();
    test_political_page_bundle_decodes_without_format_translation();
    test_world_pack_build_hash_is_order_independent();
    test_thunderscript_parser_compiler_vm_no_runtime_string_lookup();
    test_thunderscript_scope_errors_are_compile_time();
    test_thunderscript_boolean_groups_compile_to_rpn();
    test_definition_database_history_and_immutable_runtime_split();
    test_mod_vfs_highest_priority_wins_deterministically();
    test_content_loader_compiles_effective_mod_set_and_hashes_it();
    test_economy_definitions_are_script_driven_and_bind_atomically();
    test_thunderscript_arithmetic_folding();
    test_game_content_runtime_installs_one_script_snapshot();
    test_localization_is_symbol_keyed_and_supports_fallback();
    test_fp_environment_probe();
    test_country_treasury_milli_is_authoritative();
    std::cout << "All Thunder 1.0 Development thunder tests passed.\n";
    return 0;
}
