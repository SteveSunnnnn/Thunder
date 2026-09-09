#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/ScriptContext.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace thunder {

struct MemoKey {
    std::uint64_t program_key = 0;
    std::uint32_t scope_raw_id = 0;
    std::uint8_t scope_type = 0;
    std::uint8_t reserved[7]{};
    std::uint64_t salt = 0;

    bool operator==(const MemoKey& other) const noexcept {
        return program_key == other.program_key &&
               scope_raw_id == other.scope_raw_id &&
               scope_type == other.scope_type &&
               salt == other.salt;
    }
};

struct MemoKeyHash {
    std::size_t operator()(const MemoKey& k) const noexcept {
        Fnv1a64 h;
        h.add(k.program_key);
        h.add(k.scope_raw_id);
        h.add(k.scope_type);
        h.add(k.salt);
        return static_cast<std::size_t>(h.value());
    }
};

// Tick-level condition evaluation memoizer for pure trigger queries
class ConditionMemoizer {
public:
    [[nodiscard]] bool lookup(std::uint64_t program_key,
                             ScopeType scope_type,
                             std::uint32_t scope_raw_id,
                             std::uint64_t salt,
                             bool& out_result) const noexcept;

    void record(std::uint64_t program_key,
                ScopeType scope_type,
                std::uint32_t scope_raw_id,
                std::uint64_t salt,
                bool result);

    void reset_tick() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return cache_.size(); }
    [[nodiscard]] std::uint64_t hits() const noexcept { return hits_; }
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    std::unordered_map<MemoKey, bool, MemoKeyHash> cache_;
    mutable std::uint64_t hits_ = 0;
    mutable std::uint64_t misses_ = 0;
};

// Reusable object pool for ScriptExecutionContext to eliminate heap churn
class ScopeContextPool {
public:
    ScopeContextPool() = default;
    ~ScopeContextPool() = default;

    ScopeContextPool(const ScopeContextPool&) = delete;
    ScopeContextPool& operator=(const ScopeContextPool&) = delete;

    [[nodiscard]] std::unique_ptr<ScriptExecutionContext> acquire(
        ScopeRef root, ScopeRef from = {}, std::uint64_t seed = 0);

    void release(std::unique_ptr<ScriptExecutionContext> ctx);

    [[nodiscard]] std::size_t available_count() const noexcept { return pool_.size(); }
    [[nodiscard]] std::size_t total_created() const noexcept { return total_created_; }
    void clear_pool();

private:
    std::vector<std::unique_ptr<ScriptExecutionContext>> pool_;
    std::size_t total_created_ = 0;
};

} // namespace thunder
