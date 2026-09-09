#include "thunder/foundation/jobs/JobSystem.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace thunder {

thread_local JobSystem* JobSystem::tls_system_ = nullptr;
thread_local std::uint32_t JobSystem::tls_worker_index_ = 0;

std::size_t JobSystem::recommended_background_threads() noexcept {
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware <= 1u) return 0u;
    // A grand-strategy simulation generally benefits from a compact pool more
    // than from blindly mirroring very large SMT counts. Users/platform code may
    // override this at construction after topology probing is added.
    return std::min<std::size_t>(static_cast<std::size_t>(hardware - 1u), 15u);
}

JobSystem::JobSystem(std::size_t background_threads, std::size_t scratch_bytes_per_worker)
    : scratch_capacity_per_worker_(scratch_bytes_per_worker) {
    if (background_threads > 63u) background_threads = 63u; // worker mask is 64-bit incl. caller.
    scratch_.reserve(background_threads + 1u);
    for (std::size_t i = 0; i < background_threads + 1u; ++i) {
        scratch_.emplace_back(scratch_bytes_per_worker);
    }
    workers_.reserve(background_threads);
    for (std::size_t i = 0; i < background_threads; ++i) {
        const auto worker_index = static_cast<std::uint32_t>(i);
        workers_.emplace_back([this, worker_index] { worker_loop(worker_index); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock{state_mutex_};
        stopping_ = true;
        ++generation_;
    }
    work_cv_.notify_all();
    // Join explicitly while mutexes/condition variables are still alive. Member
    // destruction happens in reverse declaration order, so relying on jthread's
    // later destructor could let a worker touch done_cv_ after it was destroyed.
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

void JobSystem::worker_loop(std::uint32_t worker_index) {
    std::uint64_t observed_generation = 0;
    for (;;) {
        Dispatch* dispatch = nullptr;
        {
            std::unique_lock lock{state_mutex_};
            work_cv_.wait(lock, [&] { return stopping_ || generation_ != observed_generation; });
            if (stopping_) return;
            observed_generation = generation_;
            dispatch = current_dispatch_;
            if (dispatch != nullptr) dispatch->active_background.fetch_add(1u, std::memory_order_relaxed);
        }
        if (dispatch != nullptr) {
            execute_claims(*dispatch, worker_index);
            if (dispatch->active_background.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
                dispatch->active_background.notify_one();
            }
        }
    }
}

void JobSystem::execute_one(Dispatch& dispatch, std::uint32_t worker_index, std::size_t job_index) noexcept {
    const auto previous_system = tls_system_;
    const auto previous_worker = tls_worker_index_;
    tls_system_ = this;
    tls_worker_index_ = worker_index;

    if (worker_index < 64u) {
        dispatch.worker_mask.fetch_or(std::uint64_t{1} << worker_index, std::memory_order_relaxed);
    }

    auto& worker_scratch = scratch_[worker_index];
    const auto scratch_marker = worker_scratch.mark();
    try {
        JobContext context{worker_index, worker_scratch};
        dispatch.function(dispatch.user, context, job_index);
    } catch (...) {
        std::lock_guard lock{dispatch.exception_mutex};
        if (!dispatch.exception) dispatch.exception = std::current_exception();
    }

    worker_scratch.rewind(scratch_marker);
    tls_system_ = previous_system;
    tls_worker_index_ = previous_worker;

    if (dispatch.remaining.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
        dispatch.remaining.notify_one();
    }
}

void JobSystem::execute_claims(Dispatch& dispatch, std::uint32_t worker_index) {
    for (;;) {
        const std::size_t job_index = dispatch.next.fetch_add(1u, std::memory_order_relaxed);
        if (job_index >= dispatch.jobs) return;
        execute_one(dispatch, worker_index, job_index);
    }
}

JobDispatchStats JobSystem::run_nested_serial(std::size_t jobs, void* user, WorkFn function) {
    const auto begin = std::chrono::steady_clock::now();
    Dispatch dispatch;
    dispatch.user = user;
    dispatch.function = function;
    dispatch.jobs = jobs;
    dispatch.remaining.store(jobs, std::memory_order_relaxed);
    const std::uint32_t worker_index = (tls_system_ == this)
        ? tls_worker_index_ : static_cast<std::uint32_t>(workers_.size());
    for (std::size_t i = 0; i < jobs; ++i) execute_one(dispatch, worker_index, i);
    if (dispatch.exception) std::rethrow_exception(dispatch.exception);
    const auto end = std::chrono::steady_clock::now();
    return {jobs, 1u, jobs == 0u ? 0u : 1u,
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)};
}

JobDispatchStats JobSystem::run_indexed(std::size_t jobs, void* user, WorkFn function) {
    if (jobs == 0u) return {};
    if (function == nullptr) throw std::invalid_argument("JobSystem requires a work function");
    if (tls_system_ == this) return run_nested_serial(jobs, user, function);

    std::unique_lock dispatch_lock{dispatch_mutex_}; // One producer dispatch at a time by design.
    // Waking a worker pool for a handful of tiny chunks costs more than doing the
    // work inline. Dense simulation kernels should expose enough chunks to clear
    // this threshold; top-level tick waves with only 2-4 tasks generally should not.
    if (workers_.empty() || jobs < inline_job_threshold) {
        scratch_[workers_.size()].reset();
        return run_nested_serial(jobs, user, function);
    }

    const auto begin = std::chrono::steady_clock::now();

    for (auto& arena : scratch_) arena.reset();

    Dispatch dispatch;
    dispatch.user = user;
    dispatch.function = function;
    dispatch.jobs = jobs;
    dispatch.next.store(0u, std::memory_order_relaxed);
    dispatch.remaining.store(jobs, std::memory_order_relaxed);
    dispatch.worker_mask.store(0u, std::memory_order_relaxed);
    dispatch.active_background.store(0u, std::memory_order_relaxed);

    {
        std::lock_guard lock{state_mutex_};
        current_dispatch_ = &dispatch;
        ++generation_;
    }
    work_cv_.notify_all();

    const auto caller_index = static_cast<std::uint32_t>(workers_.size());
    execute_claims(dispatch, caller_index);

    for (;;) {
        const auto remaining = dispatch.remaining.load(std::memory_order_acquire);
        if (remaining == 0u) break;
        dispatch.remaining.wait(remaining, std::memory_order_acquire);
    }

    {
        // Stop late workers from acquiring this stack-owned dispatch. Workers
        // that already captured it incremented active_background while holding
        // state_mutex_, so the atomic wait below only covers workers that truly
        // hold the dispatch. Using atomic::wait avoids condition-variable lost
        // wakeups because the predicate and wait operate on the same object.
        std::lock_guard lock{state_mutex_};
        if (current_dispatch_ == &dispatch) current_dispatch_ = nullptr;
    }
    for (;;) {
        const auto active = dispatch.active_background.load(std::memory_order_acquire);
        if (active == 0u) break;
        dispatch.active_background.wait(active, std::memory_order_acquire);
    }

    if (dispatch.exception) std::rethrow_exception(dispatch.exception);

    const auto end = std::chrono::steady_clock::now();
    const std::uint64_t mask = dispatch.worker_mask.load(std::memory_order_relaxed);
    return {jobs, parallelism(), static_cast<std::size_t>(std::popcount(mask)),
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)};
}

} // namespace thunder
