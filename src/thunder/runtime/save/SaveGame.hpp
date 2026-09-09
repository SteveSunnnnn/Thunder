#pragma once
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

class ScriptedGameplayRuntime;
class UtilityAiEngine;
class NotificationRuntime;
class OnActionRuntime;

struct SaveGameMetadata {
    std::uint32_t version = 4;
    std::uint64_t content_hash = 0;
    std::uint64_t world_pack_hash = 0;
    std::uint64_t world_checksum = 0;
    std::uint64_t runtime_checksum = 0;
};

struct SaveGameBlob {
    SaveGameMetadata metadata{};
    std::vector<std::byte> bytes;
};

class SaveGameCodec {
public:
    [[nodiscard]] static SaveGameBlob encode(const World& world, const GameClock& clock,
                                             const ScriptedGameplayRuntime& gameplay,
                                             const UtilityAiEngine& ai,
                                             const NotificationRuntime& notifications,
                                             const OnActionRuntime& on_actions,
                                             std::uint64_t content_hash = 0,
                                             std::uint64_t world_pack_hash = 0);
    [[nodiscard]] static SaveGameBlob encode_compressed(const World& world, const GameClock& clock,
                                                        const ScriptedGameplayRuntime& gameplay,
                                                        const UtilityAiEngine& ai,
                                                        const NotificationRuntime& notifications,
                                                        const OnActionRuntime& on_actions,
                                                        std::uint64_t content_hash = 0,
                                                        std::uint64_t world_pack_hash = 0,
                                                        int compression_level = 3);
    static SaveGameMetadata decode(std::span<const std::byte> bytes, World& world,
                                   GameClock& clock, ScriptedGameplayRuntime& gameplay,
                                   UtilityAiEngine& ai, NotificationRuntime& notifications,
                                   OnActionRuntime& on_actions,
                                   const EconomyDefinitions& definitions,
                                   std::uint64_t expected_content_hash = 0,
                                   std::uint64_t expected_world_pack_hash = 0);
};

} // namespace thunder
