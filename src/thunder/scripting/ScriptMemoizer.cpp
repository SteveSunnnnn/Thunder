#include "thunder/scripting/ScriptMemoizer.hpp"

namespace thunder {

bool ConditionMemoizer::lookup(std::uint64_t program_key,
                              ScopeType scope_type,
                              std::uint32_t scope_raw_id,
                              std::uint64_t salt,
                              bool& out_result) const noexcept {
    MemoKey k;
    k.program_key = program_key;
    k.scope_type = static_cast<std::uint8_t>(scope_type);
    k.scope_raw_id = scope_raw_id;
    k.salt = salt;

    const auto it = cache_.find(k);
    if (it != cache_.end()) {
        out_result = it->second;
        ++hits_;
        return true;
    }
    ++misses_;
    return false;
}

void ConditionMemoizer::record(std::uint64_t program_key,
                               ScopeType scope_type,
                               std::uint32_t scope_raw_id,
                               std::uint64_t salt,
                               bool result) {
    MemoKey k;
    k.program_key = program_key;
    k.scope_type = static_cast<std::uint8_t>(scope_type);
    k.scope_raw_id = scope_raw_id;
    k.salt = salt;

    // insert_or_assign avoids the default-construct-then-assign round-trip
    // that operator[] performs on unordered_map; for an existing key the
    // value is updated in place without touching the node, and for a new key
    // the entry is constructed once. size()/hits()/misses() are unaffected.
    cache_.insert_or_assign(k, result);
}

void ConditionMemoizer::reset_tick() noexcept {
    cache_.clear();
}

std::size_t ConditionMemoizer::memory_bytes() const noexcept {
    return sizeof(*this) + cache_.bucket_count() * sizeof(void*) +
           cache_.size() * (sizeof(MemoKey) + sizeof(bool) + sizeof(void*));
}

std::unique_ptr<ScriptExecutionContext> ScopeContextPool::acquire(
    ScopeRef root, ScopeRef from, std::uint64_t seed) {
    std::unique_ptr<ScriptExecutionContext> ctx;
    if (!pool_.empty()) {
        ctx = std::move(pool_.back());
        pool_.pop_back();
    } else {
        ctx = std::make_unique<ScriptExecutionContext>();
        ++total_created_;
    }

    ctx->root = root;
    ctx->from = from;
    ctx->current = root;
    ctx->random_seed = seed;
    ctx->previous.clear();
    ctx->calls.clear();
    ctx->calls.emplace_back();
    ctx->event_targets.clear();
    ctx->collections.clear();
    ctx->random_draws.clear();
    ctx->transient_work_remaining = ScriptExecutionContext::default_work_budget;

    return ctx;
}

void ScopeContextPool::release(std::unique_ptr<ScriptExecutionContext> ctx) {
    if (!ctx) return;
    // Keep max 64 contexts in pool to avoid unbounded memory
    if (pool_.size() < 64u) {
        ctx->previous.clear();
        ctx->calls.clear();
        ctx->event_targets.clear();
        ctx->collections.clear();
        ctx->random_draws.clear();
        pool_.push_back(std::move(ctx));
    }
}

void ScopeContextPool::clear_pool() {
    pool_.clear();
}

} // namespace thunder
