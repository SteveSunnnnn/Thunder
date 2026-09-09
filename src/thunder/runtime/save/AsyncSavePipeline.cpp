#include "thunder/runtime/save/AsyncSavePipeline.hpp"
#include "thunder/foundation/base/Hash.hpp"

#include <chrono>
#include <fstream>
#include <system_error>

namespace thunder {

AsyncSavePipeline::AsyncSavePipeline() {
    worker_ = std::jthread([this] { worker_loop(); });
}

AsyncSavePipeline::~AsyncSavePipeline() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool AsyncSavePipeline::trigger_save(const World& world,
                                     const GameClock& clock,
                                     const ScriptedGameplayRuntime& gameplay,
                                     const UtilityAiEngine& ai,
                                     const NotificationRuntime& notifications,
                                     const OnActionRuntime& on_actions,
                                     const std::filesystem::path& target_path,
                                     std::uint64_t content_hash,
                                     std::uint64_t world_pack_hash) {
    std::lock_guard lock(mutex_);
    if (busy_ || pending_task_.has_value()) {
        return false;
    }

    // High-speed encode on tick boundary
    SaveTask task;
    task.pre_encoded_blob = SaveGameCodec::encode(world, clock, gameplay, ai,
                                                  notifications, on_actions,
                                                  content_hash, world_pack_hash);
    task.target_path = target_path;
    task.content_hash = content_hash;
    task.world_pack_hash = world_pack_hash;
    task.world_checksum = world.checksum();

    pending_task_ = std::move(task);
    busy_ = true;
    last_status_.state = AsyncSaveState::InProgress;
    last_status_.target_path = target_path.string();
    last_status_.error_message.clear();

    cv_.notify_one();
    return true;
}

bool AsyncSavePipeline::trigger_save_blob(SaveGameBlob blob,
                                          const std::filesystem::path& target_path) {
    std::lock_guard lock(mutex_);
    if (busy_ || pending_task_.has_value()) {
        return false;
    }

    SaveTask task;
    task.pre_encoded_blob = std::move(blob);
    task.target_path = target_path;

    pending_task_ = std::move(task);
    busy_ = true;
    last_status_.state = AsyncSaveState::InProgress;
    last_status_.target_path = target_path.string();
    last_status_.error_message.clear();

    cv_.notify_one();
    return true;
}

bool AsyncSavePipeline::is_busy() const noexcept {
    std::lock_guard lock(mutex_);
    return busy_;
}

AsyncSaveStatus AsyncSavePipeline::last_status() const {
    std::lock_guard lock(mutex_);
    return last_status_;
}

void AsyncSavePipeline::wait_completion() {
    std::unique_lock lock(mutex_);
    completion_cv_.wait(lock, [this] { return !busy_ && !pending_task_.has_value(); });
}

void AsyncSavePipeline::worker_loop() {
    while (true) {
        SaveTask task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || pending_task_.has_value(); });
            if (stopping_ && !pending_task_.has_value()) {
                break;
            }
            if (pending_task_.has_value()) {
                task = std::move(*pending_task_);
                pending_task_.reset();
            }
        }

        process_task(std::move(task));

        {
            std::lock_guard lock(mutex_);
            busy_ = false;
        }
        completion_cv_.notify_all();
    }
}

void AsyncSavePipeline::process_task(SaveTask task) {
    AsyncSaveStatus status;
    status.target_path = task.target_path.string();

    try {
        if (!task.pre_encoded_blob.has_value()) {
            throw std::runtime_error("save task missing payload blob");
        }

        const auto& blob = *task.pre_encoded_blob;
        status.world_checksum = blob.metadata.world_checksum;
        status.runtime_checksum = blob.metadata.runtime_checksum;
        status.bytes_written = blob.bytes.size();

        // Ensure parent directories exist
        const auto parent = task.target_path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        // Write to temporary file first for crash resilience
        const auto tmp_path = task.target_path.string() + ".tmp";
        {
            std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                throw std::runtime_error("failed to open temporary save file: " + tmp_path);
            }

            // Stream write in 64KB chunks
            constexpr std::size_t chunk_size = 64u * 1024u;
            const auto* data_ptr = reinterpret_cast<const char*>(blob.bytes.data());
            std::size_t remaining = blob.bytes.size();

            while (remaining > 0) {
                const std::size_t to_write = std::min(remaining, chunk_size);
                file.write(data_ptr, static_cast<std::streamsize>(to_write));
                if (!file) {
                    throw std::runtime_error("failed during stream write to: " + tmp_path);
                }
                data_ptr += to_write;
                remaining -= to_write;
            }

            file.flush();
        }

        // Atomic rename to final path
        std::error_code ec;
        std::filesystem::rename(tmp_path, task.target_path, ec);
        if (ec) {
            // Fallback for filesystem copy-replace if rename fails
            std::filesystem::copy_file(tmp_path, task.target_path,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tmp_path, ec);
            if (ec) {
                throw std::runtime_error("failed to rename temporary save file to target: " + ec.message());
            }
        }

        status.state = AsyncSaveState::Completed;
    } catch (const std::exception& e) {
        status.state = AsyncSaveState::Failed;
        status.error_message = e.what();
    } catch (...) {
        status.state = AsyncSaveState::Failed;
        status.error_message = "unknown exception in async save pipeline";
    }

    {
        std::lock_guard lock(mutex_);
        last_status_ = std::move(status);
    }
}

} // namespace thunder
