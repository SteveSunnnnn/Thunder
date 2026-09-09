#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/runtime/save/SaveGame.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace thunder {

class EconomyDefinitions;
class GameClock;
class JobSystem;
class ReplayJournal;
class ScriptRegistry;
class ScriptedGameplayRuntime;
class OnActionRuntime;
class UtilityAiEngine;
class ResearchSystem;
class NotificationRuntime;
class TickScheduler;
class DeterministicCommandStage;
class World;
class ProvinceAdjacencyGraph;
class SpatialPlacementDatabase;
class StateRegionIndex;
struct WorldStaticLayers;
struct EconomyTickProfile;
struct SaveGameBlob;
enum class CommandType : std::uint8_t;

// Zero selects JobSystem's recommended worker count. Keeping this config
// header small prevents every client that needs ThunderEngine from importing all
// simulation stores and render-adjacent headers.
struct ThunderEngineConfig {
    std::size_t background_threads = 0;
    std::uint64_t content_hash = 0;
    std::uint64_t world_pack_hash = 0;
};

// Public composition facade. Concrete subsystem storage lives in ThunderEngine's
// private implementation so engine clients depend on capability interfaces,
// not on the complete simulation include graph.
class ThunderEngine {
public:
    explicit ThunderEngine(ThunderEngineConfig config = {});
    ~ThunderEngine();
    ThunderEngine(const ThunderEngine&) = delete;
    ThunderEngine& operator=(const ThunderEngine&) = delete;
    ThunderEngine(ThunderEngine&&) noexcept;
    ThunderEngine& operator=(ThunderEngine&&) noexcept;

    [[nodiscard]] EconomyDefinitions& definitions() noexcept;
    [[nodiscard]] const EconomyDefinitions& definitions() const noexcept;
    [[nodiscard]] World& world() noexcept;
    [[nodiscard]] const World& world() const noexcept;
    [[nodiscard]] GameClock& clock() noexcept;
    [[nodiscard]] const GameClock& clock() const noexcept;
    [[nodiscard]] JobSystem& jobs() noexcept;
    [[nodiscard]] ReplayJournal& replay() noexcept;
    [[nodiscard]] ScriptRegistry& scripts() noexcept;
    [[nodiscard]] const ScriptRegistry& scripts() const noexcept;
    [[nodiscard]] ScriptedGameplayRuntime& gameplay() noexcept;
    [[nodiscard]] const ScriptedGameplayRuntime& gameplay() const noexcept;
    [[nodiscard]] OnActionRuntime& on_actions() noexcept;
    [[nodiscard]] const OnActionRuntime& on_actions() const noexcept;
    [[nodiscard]] UtilityAiEngine& ai() noexcept;
    [[nodiscard]] const UtilityAiEngine& ai() const noexcept;
    [[nodiscard]] ResearchSystem& research() noexcept;
    [[nodiscard]] const ResearchSystem& research() const noexcept;
    [[nodiscard]] NotificationRuntime& notifications() noexcept;
    [[nodiscard]] const NotificationRuntime& notifications() const noexcept;
    [[nodiscard]] TickScheduler& scheduler() noexcept;
    [[nodiscard]] const TickScheduler& scheduler() const noexcept;
    [[nodiscard]] DeterministicCommandStage& staged_commands() noexcept;
    [[nodiscard]] const DeterministicCommandStage& staged_commands() const noexcept;

    void set_new_game_content_hash(std::uint64_t content_hash);
    void set_world_pack_hash(std::uint64_t world_pack_hash);
    void set_world_topology(ProvinceAdjacencyGraph adjacency,
                            SpatialPlacementDatabase spatial_placement);
    void set_world_topology(ProvinceAdjacencyGraph adjacency,
                            SpatialPlacementDatabase spatial_placement,
                            StateRegionIndex state_regions);
    [[nodiscard]] const ProvinceAdjacencyGraph& adjacency() const noexcept;
    [[nodiscard]] const SpatialPlacementDatabase& spatial_placement() const noexcept;
    [[nodiscard]] const StateRegionIndex& state_regions() const noexcept;
    void set_world_static_layers(WorldStaticLayers layers);
    [[nodiscard]] const WorldStaticLayers& static_layers() const noexcept;

    void initialize_economy();
    std::uint64_t queue_command(CommandType type, CountryId country, double value);
    void advance_tick(EconomyTickProfile* economy_profile = nullptr);
    void advance_ticks(std::uint64_t count);
    [[nodiscard]] SaveGameBlob make_save() const;
    void restore(std::span<const std::byte> save);
    [[nodiscard]] bool validate_world() const noexcept;
    [[nodiscard]] std::uint64_t engine_checksum() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace thunder
