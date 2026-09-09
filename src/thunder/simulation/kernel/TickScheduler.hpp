#pragma once
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <chrono>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace thunder {

enum class TickFrequency { EveryTick, Daily, Weekly, Monthly, Yearly };
enum class TickTaskMode { Serial, ParallelSafe };

struct TickContext {
    World& world;
    const GameClock& clock;
};

struct TickTask {
    std::string name;
    TickFrequency frequency = TickFrequency::EveryTick;
    std::vector<std::string> after;
    std::function<void(TickContext&)> execute;
    TickTaskMode mode = TickTaskMode::Serial;
};

struct TickWaveProfile {
    std::size_t wave_index = 0;
    std::size_t due_tasks = 0;
    bool parallel = false;
    std::size_t workers_used = 0;
    std::chrono::nanoseconds elapsed{};
};

struct TickExecutionProfile {
    std::vector<TickWaveProfile> waves;
    std::chrono::nanoseconds total{};

    void reset(std::size_t wave_capacity = 0) {
        waves.clear();
        if (wave_capacity > waves.capacity()) waves.reserve(wave_capacity);
        total = {};
    }
};

class TickScheduler {
public:
    void add(TickTask task);
    void compile();
    void run_due(TickContext& context) const;
    void run_due_parallel(TickContext& context, JobSystem& jobs, TickExecutionProfile* profile = nullptr) const;

    [[nodiscard]] std::string to_dot() const;
    [[nodiscard]] const std::vector<std::string>& execution_order() const noexcept { return order_names_; }
    [[nodiscard]] std::span<const std::vector<std::size_t>> execution_batches() const noexcept { return batches_; }

private:
    [[nodiscard]] static bool due(TickFrequency frequency, const GameClock& clock) noexcept;

    std::vector<TickTask> tasks_;
    std::vector<std::size_t> order_;
    std::vector<std::string> order_names_;
    std::vector<std::vector<std::size_t>> batches_;
};

} // namespace thunder
