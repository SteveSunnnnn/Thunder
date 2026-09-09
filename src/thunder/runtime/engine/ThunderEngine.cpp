#include "thunder/runtime/engine/ThunderEngine.hpp"

#include "thunder/simulation/ai/UtilityAi.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/economy/EconomySystem.hpp"
#include "thunder/simulation/gameplay/NotificationRuntime.hpp"
#include "thunder/simulation/gameplay/OnActionRuntime.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/simulation/research/ResearchSystem.hpp"
#include "thunder/runtime/save/ReplayJournal.hpp"
#include "thunder/runtime/save/SaveGame.hpp"
#include "thunder/simulation/kernel/CommandQueue.hpp"
#include "thunder/simulation/kernel/DeterministicCommandStage.hpp"
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/kernel/TickScheduler.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"
#include "thunder/simulation/world/SpatialPlacement.hpp"
#include "thunder/simulation/world/StateRegionIndex.hpp"
#include "thunder/simulation/world/WorldStaticLayers.hpp"

#include <stdexcept>
#include <utility>

namespace thunder {
namespace {

std::size_t resolved_worker_count(std::size_t requested) noexcept {
    return requested == 0u ? JobSystem::recommended_background_threads() : requested;
}

} // namespace

struct ThunderEngine::Impl {
    ThunderEngineConfig config{};
    EconomyDefinitions definitions;
    World world;
    GameClock clock;
    JobSystem jobs;
    EconomySystem economy;
    CommandQueue commands;
    ReplayJournal replay;
    ScriptRegistry scripts;
    ScriptedGameplayRuntime gameplay;
    OnActionRuntime on_actions;
    UtilityAiEngine ai;
    ResearchSystem research;
    NotificationRuntime notifications;
    ProvinceAdjacencyGraph adjacency;
    SpatialPlacementDatabase spatial_placement;
    StateRegionIndex state_regions;
    WorldStaticLayers static_layers;
    TickScheduler scheduler;
    DeterministicCommandStage staged_commands;
    EconomyTickProfile* active_economy_profile = nullptr;

    explicit Impl(ThunderEngineConfig value)
        : config(value),
          jobs(resolved_worker_count(value.background_threads)),
          economy(definitions),
          scripts(ScriptRegistry::make_builtin()),
          gameplay(scripts),
          on_actions(scripts),
          ai(scripts),
          research(scripts),
          notifications(scripts) {
        staged_commands.resize(jobs.parallelism());
        init_scheduler();
    }

    void init_scheduler() {
        scheduler.add({"on_actions", TickFrequency::EveryTick, {}, [this](TickContext& ctx) {
            (void)on_actions.dispatch_due(ctx.world, gameplay, ctx.clock.tick_index());
        }, TickTaskMode::Serial});

        scheduler.add({"gameplay_daily", TickFrequency::Daily, {"on_actions"}, [this](TickContext& ctx) {
            gameplay.update(ctx.world, ctx.clock.tick_index());
        }, TickTaskMode::Serial});

        scheduler.add({"notifications_daily", TickFrequency::Daily, {"on_actions"}, [this](TickContext& ctx) {
            (void)notifications.update(ctx.clock.tick_index());
        }, TickTaskMode::ParallelSafe});

        scheduler.add({"economy_weekly", TickFrequency::Weekly, {"on_actions"}, [this](TickContext& ctx) {
            economy.run_weekly(ctx.world, jobs, active_economy_profile);
        }, TickTaskMode::Serial});

        scheduler.add({"research_weekly", TickFrequency::Weekly, {"on_actions"}, [this](TickContext& ctx) {
            const auto weekly_tick = ctx.clock.day_index() / 7u;
            (void)research.run_weekly(ctx.world, weekly_tick);
            research.run_tech_spread_weekly(ctx.world, weekly_tick);
        }, TickTaskMode::ParallelSafe});

        scheduler.add({"grand_strategy_weekly", TickFrequency::Weekly, {"research_weekly"}, [this](TickContext& ctx) {
            const auto weekly_tick = ctx.clock.day_index() / 7u;
            world.grand_strategy.run_weekly_reference_tick(
                ctx.world, !research.has_finalized_content(), weekly_tick);
            // R7: autonomous domestic migration after flows resolve, so new
            // flows start aging next week.
            world.grand_strategy.generate_autonomous_migration_flows(ctx.world, economy.definitions());
        }, TickTaskMode::Serial});

        scheduler.add({"ai_weekly", TickFrequency::Weekly, {"grand_strategy_weekly"}, [this](TickContext& ctx) {
            (void)ai.run_plans(ctx.world, ScopeType::Country, 4096u, ctx.clock.tick_index());
        }, TickTaskMode::Serial});

        scheduler.compile();
    }
};

ThunderEngine::ThunderEngine(ThunderEngineConfig config)
    : impl_(std::make_unique<Impl>(config)) {}

ThunderEngine::~ThunderEngine() = default;
ThunderEngine::ThunderEngine(ThunderEngine&&) noexcept = default;
ThunderEngine& ThunderEngine::operator=(ThunderEngine&&) noexcept = default;

EconomyDefinitions& ThunderEngine::definitions() noexcept { return impl_->definitions; }
const EconomyDefinitions& ThunderEngine::definitions() const noexcept { return impl_->definitions; }
World& ThunderEngine::world() noexcept { return impl_->world; }
const World& ThunderEngine::world() const noexcept { return impl_->world; }
GameClock& ThunderEngine::clock() noexcept { return impl_->clock; }
const GameClock& ThunderEngine::clock() const noexcept { return impl_->clock; }
JobSystem& ThunderEngine::jobs() noexcept { return impl_->jobs; }
ReplayJournal& ThunderEngine::replay() noexcept { return impl_->replay; }
ScriptRegistry& ThunderEngine::scripts() noexcept { return impl_->scripts; }
const ScriptRegistry& ThunderEngine::scripts() const noexcept { return impl_->scripts; }
ScriptedGameplayRuntime& ThunderEngine::gameplay() noexcept { return impl_->gameplay; }
const ScriptedGameplayRuntime& ThunderEngine::gameplay() const noexcept { return impl_->gameplay; }
OnActionRuntime& ThunderEngine::on_actions() noexcept { return impl_->on_actions; }
const OnActionRuntime& ThunderEngine::on_actions() const noexcept { return impl_->on_actions; }
UtilityAiEngine& ThunderEngine::ai() noexcept { return impl_->ai; }
const UtilityAiEngine& ThunderEngine::ai() const noexcept { return impl_->ai; }
ResearchSystem& ThunderEngine::research() noexcept { return impl_->research; }
const ResearchSystem& ThunderEngine::research() const noexcept { return impl_->research; }
NotificationRuntime& ThunderEngine::notifications() noexcept { return impl_->notifications; }
const NotificationRuntime& ThunderEngine::notifications() const noexcept { return impl_->notifications; }
TickScheduler& ThunderEngine::scheduler() noexcept { return impl_->scheduler; }
const TickScheduler& ThunderEngine::scheduler() const noexcept { return impl_->scheduler; }
DeterministicCommandStage& ThunderEngine::staged_commands() noexcept { return impl_->staged_commands; }
const DeterministicCommandStage& ThunderEngine::staged_commands() const noexcept { return impl_->staged_commands; }

void ThunderEngine::set_new_game_content_hash(std::uint64_t content_hash) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u ||
        state.world.markets.size() != 0u || state.world.buildings.size() != 0u ||
        state.world.pops.size() != 0u) {
        throw std::logic_error("content hash must be installed before authoritative world state");
    }
    state.config.content_hash = content_hash;
}

void ThunderEngine::set_world_pack_hash(std::uint64_t world_pack_hash) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u ||
        state.world.markets.size() != 0u || state.world.buildings.size() != 0u ||
        state.world.pops.size() != 0u) {
        throw std::logic_error("world pack hash must be installed before authoritative world state");
    }
    state.config.world_pack_hash = world_pack_hash;
}

void ThunderEngine::set_world_topology(ProvinceAdjacencyGraph adjacency,
                                    SpatialPlacementDatabase spatial_placement) {
    set_world_topology(std::move(adjacency), std::move(spatial_placement), StateRegionIndex{});
}

void ThunderEngine::set_world_topology(ProvinceAdjacencyGraph adjacency,
                                    SpatialPlacementDatabase spatial_placement,
                                    StateRegionIndex state_regions) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u)
        throw std::logic_error("world topology must be installed before authoritative world state");
    state.adjacency = std::move(adjacency);
    state.spatial_placement = std::move(spatial_placement);
    state.state_regions = std::move(state_regions);
}

const ProvinceAdjacencyGraph& ThunderEngine::adjacency() const noexcept { return impl_->adjacency; }
const SpatialPlacementDatabase& ThunderEngine::spatial_placement() const noexcept {
    return impl_->spatial_placement;
}
const StateRegionIndex& ThunderEngine::state_regions() const noexcept { return impl_->state_regions; }

void ThunderEngine::set_world_static_layers(WorldStaticLayers layers) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u)
        throw std::logic_error("world static layers must be installed before authoritative world state");
    state.static_layers = std::move(layers);
}

const WorldStaticLayers& ThunderEngine::static_layers() const noexcept {
    return impl_->static_layers;
}

void ThunderEngine::initialize_economy() {
    impl_->economy.rebuild_indices(impl_->world);
}

std::uint64_t ThunderEngine::queue_command(CommandType type, CountryId country, double value) {
    auto& state = *impl_;
    const auto sequence = state.commands.enqueue(type, country, value);
    state.replay.record(state.clock.tick_index() + 1u, type, country, value);
    return sequence;
}

void ThunderEngine::advance_tick(EconomyTickProfile* economy_profile) {
    auto& state = *impl_;
    state.staged_commands.flush(state.commands);
    state.commands.apply_all(state.world);
    state.clock.advance_tick();
    state.active_economy_profile = economy_profile;

    TickContext context{state.world, state.clock};
    state.scheduler.run_due(context);

    if (state.clock.is_yearly_boundary())
        state.replay.checkpoint(state.clock.tick_index(), engine_checksum());
}

void ThunderEngine::advance_ticks(std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) advance_tick();
}

SaveGameBlob ThunderEngine::make_save() const {
    const auto& state = *impl_;
    return SaveGameCodec::encode(state.world, state.clock, state.gameplay, state.ai,
                                 state.notifications, state.on_actions,
                                 state.config.content_hash, state.config.world_pack_hash);
}

void ThunderEngine::restore(std::span<const std::byte> save) {
    auto& state = *impl_;
    SaveGameCodec::decode(save, state.world, state.clock, state.gameplay, state.ai,
                          state.notifications, state.on_actions, state.definitions,
                          state.config.content_hash, state.config.world_pack_hash);
    state.economy.rebuild_indices(state.world);
    state.commands.clear();
    state.replay.clear();
    if (state.state_regions.state_count() == 0u && state.world.geography.state_count() != 0u)
        state.state_regions.rebuild(state.world.geography);
}

bool ThunderEngine::validate_world() const noexcept {
    const auto& state = *impl_;
    const auto& world = state.world;
    if (!world.geography.validate(world.countries.size(), world.markets.size())) return false;
    if (state.adjacency.province_count() != 0u &&
        (state.adjacency.province_count() != world.geography.province_count() ||
         !state.adjacency.is_symmetric())) return false;
    if (!state.spatial_placement.empty() &&
        state.spatial_placement.province_count() != world.geography.province_count()) return false;
    if (state.state_regions.state_count() != 0u &&
        state.state_regions.state_count() != world.geography.state_count()) return false;
    if (!state.static_layers.validate(world.geography.province_count(),
                                     world.geography.state_count())) return false;
    if (!world.grand_strategy.validate(
            world.countries.size(), world.markets.size(),
            world.geography.province_count(), world.geography.state_count(),
            world.buildings.size(), state.definitions.good_count())) return false;
    if (!world.banks.validate(world.countries.size(), world.buildings.size())) return false;
    if (!world.trade_policies.validate(world.countries.size())) return false;
    if (!world.currencies.validate(world.countries.size())) return false;
    if (!world.global_scripts.validate(world)) return false;
    try {
        if (!world.construction.validate(world)) return false;
    } catch (...) {
        return false;
    }
    if (!state.research.validate_state(world)) return false;

    for (std::size_t i = 0; i < world.countries.size(); ++i) {
        const auto currency = world.countries.primary_currency(
            CountryId{static_cast<CountryId::rep_type>(i)});
        if (currency == 0u) return false;
        if (world.currencies.size() > 0u && !world.currencies.contains(currency)) return false;
    }
    for (std::size_t i = 0; i < world.banks.size(); ++i) {
        const auto bank = world.banks.bank(BankId{static_cast<BankId::rep_type>(i)});
        if (world.currencies.size() > 0u && !world.currencies.contains(bank.currency)) return false;
    }
    for (std::size_t i = 0; i < world.buildings.size(); ++i) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const BuildingId id{static_cast<BuildingId::rep_type>(i)};
        const auto market = world.buildings.market(id);
        const auto type = world.buildings.type(id);
        if (market.valid() && market.value() >= world.markets.size()) return false;
        if (!type.valid() || type.value() >= state.definitions.building_type_count()) return false;
        const auto method = world.buildings.production_method(id);
        if (method.valid() &&
            (method.value() >= state.definitions.production_method_count() ||
             state.definitions.production_method(method).building_type != type)) return false;
        const auto province = world.buildings.province(id);
        if (province.valid() && province.value() >= world.geography.province_count()) return false;
    }
    for (std::size_t i = 0; i < world.pops.size(); ++i) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const PopId id{static_cast<PopId::rep_type>(i)};
        if (world.pops.market(id).valid() && world.pops.market(id).value() >= world.markets.size()) return false;
        if (world.pops.need_profile(id).value() >= state.definitions.need_profile_count()) return false;
        const auto employer = world.pops.employer(id);
        if (employer.valid() && (employer.value() >= world.buildings.size() ||
                                  !world.buildings.slot_pool().is_index_alive(employer.value()))) return false;
        if (world.pops.province(id).valid() &&
            world.pops.province(id).value() >= world.geography.province_count()) return false;
    }

    try {
        state.gameplay.validate_state(state.gameplay.instances(), state.gameplay.log(), world,
                                      state.clock.tick_index(), state.gameplay.next_instance_id());
        state.notifications.validate_state(state.notifications.instances(),
                                           state.notifications.next_instance_id(), world,
                                           state.clock.tick_index());
        state.on_actions.validate_state(state.on_actions.queue(),
                                        state.on_actions.next_invocation_id(), world);
    } catch (...) {
        return false;
    }
    return true;
}

std::uint64_t ThunderEngine::engine_checksum() const noexcept {
    const auto& state = *impl_;
    Fnv1a64 hash;
    hash.add(state.world.checksum());
    hash.add(state.gameplay.checksum());
    hash.add(state.ai.checksum());
    hash.add(state.clock.checksum());
    hash.add(state.notifications.checksum());
    hash.add(state.on_actions.checksum());
    return hash.value();
}

} // namespace thunder
