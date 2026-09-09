#pragma once
#include "thunder/foundation/memory/FrameArena.hpp"
#include "thunder/foundation/jobs/StablePartition.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace thunder {

struct JobContext {
    std::uint32_t worker_index = 0;
    FrameArena& scratch;
};

struct JobDispatchStats {
    std::size_t jobs = 0;
    std::size_t worker_slots = 0;
    std::size_t workers_used = 0;
    std::chrono::nanoseconds elapsed{};
};

// A persistent CPU worker pool for simulation kernels. Thunder deliberately keeps
// the scheduling primitive small: callers expose deterministic, independent
// chunks and workers dynamically claim chunk IDs. Chunk identity never depends
// on which worker executes it, so reductions can merge by chunk ID and remain
// bit-stable across worker counts.
class JobSystem {
public:
    using WorkFn = void (*)(void*, JobContext&, std::size_t);

    explicit JobSystem(std::size_t background_threads = recommended_background_threads(),
                       std::size_t scratch_bytes_per_worker = 256u * 1024u);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    static constexpr std::size_t inline_job_threshold = 2u;
    [[nodiscard]] static std::size_t recommended_background_threads() noexcept;
    [[nodiscard]] std::size_t background_thread_count() const noexcept { return workers_.size(); }
    [[nodiscard]] std::size_t parallelism() const noexcept { return workers_.size() + 1u; }
    [[nodiscard]] std::size_t scratch_capacity_per_worker() const noexcept { return scratch_capacity_per_worker_; }

    JobDispatchStats run_indexed(std::size_t jobs, void* user, WorkFn function);

    template <typename Fn>
    JobDispatchStats parallel_for(std::size_t item_count, std::size_t grain_size, Fn&& function) {
        const StablePartition partition{item_count, grain_size};
        if (partition.item_count() == 0u) return {};
        const std::size_t chunk_count = partition.chunk_count();
        grain_size = partition.grain_size();
        using FnType = std::remove_reference_t<Fn>;
        struct Payload {
            FnType* function = nullptr;
            std::size_t item_count = 0;
            std::size_t grain_size = 0;
        } payload{&function, item_count, grain_size};

        return run_indexed(chunk_count, &payload,
            [](void* raw, JobContext& context, std::size_t chunk_index) {
                auto& p = *static_cast<Payload*>(raw);
                const std::size_t begin = chunk_index * p.grain_size;
                const std::size_t remaining = p.item_count - begin;
                const std::size_t end = p.grain_size < remaining ? begin + p.grain_size : p.item_count;
                (*p.function)(context, chunk_index, begin, end);
            });
    }

private:
    struct alignas(64) Dispatch {
        void* user = nullptr;
        WorkFn function = nullptr;
        std::size_t jobs = 0;
        alignas(64) std::atomic<std::size_t> next{0};
        alignas(64) std::atomic<std::size_t> remaining{0};
        alignas(64) std::atomic<std::uint64_t> worker_mask{0};
        alignas(64) std::atomic<std::size_t> active_background{0};
        std::exception_ptr exception;
        std::mutex exception_mutex;
    };

    void worker_loop(std::uint32_t worker_index);
    void execute_claims(Dispatch& dispatch, std::uint32_t worker_index);
    void execute_one(Dispatch& dispatch, std::uint32_t worker_index, std::size_t job_index) noexcept;
    JobDispatchStats run_nested_serial(std::size_t jobs, void* user, WorkFn function);

    std::vector<std::jthread> workers_;
    std::vector<FrameArena> scratch_;
    std::size_t scratch_capacity_per_worker_ = 0;

    std::mutex dispatch_mutex_;
    std::mutex state_mutex_;
    std::condition_variable work_cv_;
    Dispatch* current_dispatch_ = nullptr;
    std::uint64_t generation_ = 0;
    bool stopping_ = false;

    static thread_local JobSystem* tls_system_;
    static thread_local std::uint32_t tls_worker_index_;
};

} // namespace thunder
