#include "thunder/simulation/kernel/TickScheduler.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <sstream>
#include <unordered_map>

namespace thunder {

void TickScheduler::add(TickTask task) {
    if (!task.execute) throw std::invalid_argument("TickTask requires execute callback");
    tasks_.push_back(std::move(task));
}

void TickScheduler::compile() {
    std::unordered_map<std::string, std::size_t> by_name;
    by_name.reserve(tasks_.size());
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
        if (!by_name.emplace(tasks_[i].name, i).second) {
            throw std::runtime_error("duplicate tick task: " + tasks_[i].name);
        }
    }

    std::vector<std::vector<std::size_t>> edges(tasks_.size());
    std::vector<std::size_t> indegree(tasks_.size(), 0);
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
        edges[i].reserve(4);
        for (const auto& dep : tasks_[i].after) {
            const auto it = by_name.find(dep);
            if (it == by_name.end()) throw std::runtime_error("unknown tick dependency: " + dep);
            edges[it->second].push_back(i);
            ++indegree[i];
        }
    }

    std::vector<std::size_t> ready;
    ready.reserve(tasks_.size());
    for (std::size_t i = 0; i < indegree.size(); ++i) if (indegree[i] == 0) ready.push_back(i);

    order_.clear();
    order_names_.clear();
    batches_.clear();
    order_.reserve(tasks_.size());
    order_names_.reserve(tasks_.size());

    while (!ready.empty()) {
        std::sort(ready.begin(), ready.end());
        batches_.push_back(ready);
        std::vector<std::size_t> next;
        for (const auto n : ready) {
            order_.push_back(n);
            order_names_.push_back(tasks_[n].name);
            for (const auto child : edges[n]) if (--indegree[child] == 0) next.push_back(child);
        }
        ready = std::move(next);
    }

    if (order_.size() != tasks_.size()) throw std::runtime_error("cycle detected in tick task graph");
}

bool TickScheduler::due(TickFrequency frequency, const GameClock& clock) noexcept {
    switch (frequency) {
        case TickFrequency::EveryTick: return true;
        case TickFrequency::Daily: return clock.is_daily_boundary();
        case TickFrequency::Weekly: return clock.is_weekly_boundary();
        case TickFrequency::Monthly: return clock.is_monthly_boundary();
        case TickFrequency::Yearly: return clock.is_yearly_boundary();
    }
    return false;
}


std::string TickScheduler::to_dot() const {
    if (order_.size() != tasks_.size()) throw std::runtime_error("TickScheduler must be compiled before to_dot");
    auto frequency_name = [](TickFrequency frequency) -> const char* {
        switch (frequency) {
            case TickFrequency::EveryTick: return "tick";
            case TickFrequency::Daily: return "daily";
            case TickFrequency::Weekly: return "weekly";
            case TickFrequency::Monthly: return "monthly";
            case TickFrequency::Yearly: return "yearly";
        }
        return "unknown";
    };

    std::ostringstream out;
    out << "digraph ThunderTickGraph {\n  rankdir=LR;\n";
    for (const auto& task : tasks_) {
        out << "  \"" << task.name << "\" [label=\"" << task.name << "\\n"
            << frequency_name(task.frequency) << "\\n"
            << (task.mode == TickTaskMode::ParallelSafe ? "parallel-safe" : "serial") << "\"];\n";
    }
    for (const auto& task : tasks_) {
        for (const auto& dependency : task.after) {
            out << "  \"" << dependency << "\" -> \"" << task.name << "\";\n";
        }
    }
    out << "}\n";
    return out.str();
}

void TickScheduler::run_due(TickContext& context) const {
    if (order_.size() != tasks_.size()) throw std::runtime_error("TickScheduler must be compiled before run_due");
    for (const auto index : order_) {
        const auto& task = tasks_[index];
        if (due(task.frequency, context.clock)) task.execute(context);
    }
}

void TickScheduler::run_due_parallel(TickContext& context, JobSystem& jobs, TickExecutionProfile* profile) const {
    if (order_.size() != tasks_.size()) throw std::runtime_error("TickScheduler must be compiled before run_due_parallel");
    if (profile != nullptr) profile->reset(batches_.size());
    const auto total_begin = std::chrono::steady_clock::now();

    for (std::size_t wave_index = 0; wave_index < batches_.size(); ++wave_index) {
        const auto& batch = batches_[wave_index];
        std::size_t due_count = 0;
        bool all_parallel_safe = true;
        for (const auto task_index : batch) {
            const auto& task = tasks_[task_index];
            if (!due(task.frequency, context.clock)) continue;
            ++due_count;
            if (task.mode != TickTaskMode::ParallelSafe) all_parallel_safe = false;
        }
        if (due_count == 0u) continue;

        const auto wave_begin = std::chrono::steady_clock::now();
        std::size_t workers_used = 1u;
        bool executed_parallel = false;
        const bool use_parallel = due_count > 1u && all_parallel_safe && jobs.parallelism() > 1u;
        if (use_parallel) {
            struct Payload {
                const TickScheduler* scheduler = nullptr;
                TickContext* context = nullptr;
                const std::vector<std::size_t>* batch = nullptr;
            } payload{this, &context, &batch};

            const auto stats = jobs.run_indexed(batch.size(), &payload,
                [](void* raw, JobContext&, std::size_t item) {
                    auto& p = *static_cast<Payload*>(raw);
                    const auto task_index = (*p.batch)[item];
                    const auto& task = p.scheduler->tasks_[task_index];
                    if (TickScheduler::due(task.frequency, p.context->clock)) task.execute(*p.context);
                });
            workers_used = stats.workers_used;
            executed_parallel = stats.workers_used > 1u;
        } else {
            // If even one due task in this dependency wave is not explicitly safe,
            // preserve the stable compiled order for the whole wave. This avoids
            // silently changing semantics around staged/legacy tasks.
            for (const auto task_index : batch) {
                const auto& task = tasks_[task_index];
                if (due(task.frequency, context.clock)) task.execute(context);
            }
        }
        const auto wave_end = std::chrono::steady_clock::now();

        if (profile != nullptr) {
            profile->waves.push_back({wave_index, due_count, executed_parallel, workers_used,
                std::chrono::duration_cast<std::chrono::nanoseconds>(wave_end - wave_begin)});
        }
    }

    const auto total_end = std::chrono::steady_clock::now();
    if (profile != nullptr) {
        profile->total = std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_begin);
    }
}

} // namespace thunder
