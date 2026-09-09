#include "thunder/runtime/engine/GameContentRuntime.hpp"

#include "thunder/runtime/engine/ThunderEngine.hpp"
#include "thunder/content/definition/WorldContentBinder.hpp"
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/world/WorldBootstrap.hpp"

#include <stdexcept>
#include <string>
#include <optional>

namespace thunder {

GameContentRuntime::GameContentRuntime(const ScriptRegistry& registry)
    : registry_(registry), definitions_(symbols_, registry_) {}

const ContentLoadResult& GameContentRuntime::load(const VirtualFileSystem& vfs) {
    if (loaded_) throw std::logic_error("GameContentRuntime is a one-shot content snapshot");
    result_ = ContentLoader{symbols_, registry_}.load(vfs, definitions_);
    loaded_ = true;
    return result_;
}

bool GameContentRuntime::install_new_game(
    ThunderEngine& engine,
    std::int32_t history_date,
    std::vector<ScriptCompileDiagnostic>& diagnostics) {
    return install_new_game(engine, history_date, diagnostics, nullptr);
}

bool GameContentRuntime::install_new_game(
    ThunderEngine& engine,
    std::int32_t history_date,
    std::vector<ScriptCompileDiagnostic>& diagnostics,
    const WorldPackReader* world_pack) {
    if (!loaded_ || installed_) {
        diagnostics.push_back({!loaded_ ? "content must be loaded before install" :
                                          "content snapshot is already installed", 0});
        return false;
    }
    if (!result_.ok()) {
        diagnostics.insert(diagnostics.end(), result_.diagnostics.begin(), result_.diagnostics.end());
        return false;
    }
    const GameDate start_date{
        history_date / 10'000,
        static_cast<unsigned>((history_date / 100) % 100),
        static_cast<unsigned>(history_date % 100),
        0u
    };
    if (!GameClock::validate_state(start_date, 0u, 0u)) {
        diagnostics.push_back({"new-game history date is invalid", 0});
        return false;
    }
    if (engine.clock().tick_index() != 0u || engine.world().countries.size() != 0u ||
        engine.world().markets.size() != 0u || engine.world().buildings.size() != 0u ||
        engine.world().pops.size() != 0u || engine.definitions().good_count() != 0u ||
        !engine.gameplay().definitions().empty() || !engine.ai().actions().empty() ||
        !engine.ai().plans().empty() || !engine.research().definitions().empty() ||
        !engine.notifications().definitions().empty() || !engine.on_actions().definitions().empty()) {
        diagnostics.push_back({"script content can only be installed into a pristine new-game engine", 0});
        return false;
    }

    // Validate every binder against staging runtimes before mutating the real
    // engine. This makes bad cross-domain references a content-load failure,
    // not a half-installed session.
    EconomyDefinitions staged_economy;
    World staged_world;
    ScriptedGameplayRuntime staged_gameplay{registry_, &definitions_.scripts()};
    UtilityAiEngine staged_ai{registry_, &definitions_.scripts()};
    ResearchSystem staged_research{registry_, &definitions_.scripts()};
    NotificationRuntime staged_notifications{registry_, &definitions_.scripts()};
    OnActionRuntime staged_on_actions{registry_, &definitions_.scripts()};
    WorldContentBinder world_content{definitions_};

    bool ok = definitions_.bind_economy(staged_economy, diagnostics);
    ok &= definitions_.bind_gameplay(staged_gameplay, staged_ai, diagnostics);
    ok &= definitions_.bind_research(staged_research, diagnostics);
    ok &= definitions_.bind_notifications(staged_notifications, diagnostics);
    ok &= definitions_.bind_on_actions(staged_gameplay, staged_on_actions, diagnostics);
    std::optional<WorldBootstrapResult> staged_bootstrap;
    if (world_pack != nullptr) {
        try {
            staged_bootstrap.emplace(WorldBootstrap::load(*world_pack, staged_economy));
            staged_world = std::move(staged_bootstrap->world);
            ok &= world_content.hydrate_countries(staged_world, diagnostics);
            ok &= world_content.instantiate_entities(staged_world, staged_economy, diagnostics);
        } catch (const std::exception& error) {
            diagnostics.push_back({std::string{"world pack bootstrap failed: "} + error.what(), 0});
            ok = false;
        }
    } else {
        world_content.instantiate_countries(staged_world);
    }
    world_content.apply_history(history_date, staged_world);
    if (!ok || !diagnostics.empty()) return false;

    engine.set_new_game_content_hash(result_.content_hash);
    if (world_pack != nullptr) engine.set_world_pack_hash(world_pack->stats().build_hash);
    engine.clock().restore_state(start_date, 0u, 0u);
    engine.definitions() = std::move(staged_economy);
    if (staged_bootstrap) {
        engine.set_world_topology(std::move(staged_bootstrap->adjacency),
                                  std::move(staged_bootstrap->spatial_placement),
                                  std::move(staged_bootstrap->state_regions));
        engine.set_world_static_layers(std::move(staged_bootstrap->static_layers));
    }
    engine.world() = std::move(staged_world);

    const auto before = diagnostics.size();
    ok = definitions_.bind_gameplay(engine.gameplay(), engine.ai(), diagnostics);
    ok &= definitions_.bind_research(engine.research(), diagnostics);
    ok &= definitions_.bind_notifications(engine.notifications(), diagnostics);
    ok &= definitions_.bind_on_actions(engine.gameplay(), engine.on_actions(), diagnostics);
    if (!ok || diagnostics.size() != before) {
        throw std::logic_error("validated content failed during engine commit");
    }
    engine.initialize_economy();
    installed_ = true;
    return true;
}

} // namespace thunder
