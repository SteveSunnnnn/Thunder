#pragma once

#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/gameplay/NotificationRuntime.hpp"
#include "thunder/simulation/gameplay/OnActionRuntime.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/simulation/ai/UtilityAi.hpp"
#include "thunder/runtime/save/SaveGame.hpp"
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace thunder {

enum class AsyncSaveState : std::uint8_t {
    Idle = 0,
    InProgress = 1,
    Completed = 2,
    Failed = 3
};

struct AsyncSaveStatus {
    AsyncSaveState state = AsyncSaveState::Idle;
    std::uint64_t bytes_written = 0;
    std::uint64_t world_checksum = 0;
    std::uint64_t runtime_checksum = 0;
    std::string target_path;
    std::string error_message;
};

// AsyncSavePipeline
// Provides zero-stutter background save persistence. Main thread captures
// a lightweight state snapshot and queues a save job. Dedicated background
// IO worker serializes, compresses, writes to a temporary file and atomically
// renames to target file path.
class AsyncSavePipeline {
public:
    AsyncSavePipeline();
    ~AsyncSavePipeline();

    AsyncSavePipeline(const AsyncSavePipeline&) = delete;
    AsyncSavePipeline& operator=(const AsyncSavePipeline&) = delete;
    AsyncSavePipeline(AsyncSavePipeline&&) = delete;
    AsyncSavePipeline& operator=(AsyncSavePipeline&&) = delete;

    // Initiates a background save from authoritative state snapshots
    bool trigger_save(const World& world,
                      const GameClock& clock,
                      const ScriptedGameplayRuntime& gameplay,
                      const UtilityAiEngine& ai,
                      const NotificationRuntime& notifications,
                      const OnActionRuntime& on_actions,
                      const std::filesystem::path& target_path,
                      std::uint64_t content_hash = 0,
                      std::uint64_t world_pack_hash = 0);

    // Direct byte payload background save
    bool trigger_save_blob(SaveGameBlob blob,
                           const std::filesystem::path& target_path);

    [[nodiscard]] bool is_busy() const noexcept;
    [[nodiscard]] AsyncSaveStatus last_status() const;
    void wait_completion();

private:
    struct SaveTask {
        std::optional<SaveGameBlob> pre_encoded_blob;
        std::optional<World> world;
        std::optional<GameClock> clock;
        std::vector<std::byte> serialized_data;
        std::filesystem::path target_path;
        std::uint64_t content_hash = 0;
        std::uint64_t world_pack_hash = 0;
        // Direct snapshots for runtime if world was copied
        std::uint64_t world_checksum = 0;
    };

    void worker_loop();
    void process_task(SaveTask task);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable completion_cv_;
    std::optional<SaveTask> pending_task_;
    AsyncSaveStatus last_status_{};
    bool busy_ = false;
    bool stopping_ = false;
    std::jthread worker_;
};

} // namespace thunder
