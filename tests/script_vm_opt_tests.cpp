#include "thunder/scripting/ScriptBytecode.hpp"
#include "thunder/scripting/ScriptMemoizer.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace thunder;

static void test_bytecode_compiler_and_vm() {
    ScriptRegistry registry = ScriptRegistry::make_builtin();
    World world;
    const auto country = world.countries.create({
        .tag = "FRA",
        .population = 1000.0,
        .gdp = 1000.0,
        .treasury = 5000.0,
        .tax_rate = 0.25
    });

    // Construct a composite AST condition:
    // ALL {
    //    Trigger(TreasuryAbove 1000.0) -> True (since treasury = 5000.0)
    //    NOT { Trigger(TreasuryAbove 10000.0) } -> True (not > 10000)
    //    ANY {
    //       Trigger(TreasuryAbove 20000.0) -> False
    //       Trigger(TreasuryAbove 2000.0) -> True
    //    }
    // }

    const auto trig_treasury = registry.find_trigger("treasury_above");
    assert(trig_treasury.valid());

    CompiledScopedCondition c1;
    c1.kind = ScopedConditionKind::Trigger;
    c1.primitive = trig_treasury;
    c1.argument.source = ScriptArgumentSourceKind::Literal;
    c1.argument.literal = ScriptArgument::numeric(1000.0);

    CompiledScopedCondition c2_sub;
    c2_sub.kind = ScopedConditionKind::Trigger;
    c2_sub.primitive = trig_treasury;
    c2_sub.argument.source = ScriptArgumentSourceKind::Literal;
    c2_sub.argument.literal = ScriptArgument::numeric(10000.0);

    CompiledScopedCondition c2;
    c2.kind = ScopedConditionKind::Not;
    c2.children.push_back(c2_sub);

    CompiledScopedCondition c3_sub1;
    c3_sub1.kind = ScopedConditionKind::Trigger;
    c3_sub1.primitive = trig_treasury;
    c3_sub1.argument.source = ScriptArgumentSourceKind::Literal;
    c3_sub1.argument.literal = ScriptArgument::numeric(20000.0);

    CompiledScopedCondition c3_sub2;
    c3_sub2.kind = ScopedConditionKind::Trigger;
    c3_sub2.primitive = trig_treasury;
    c3_sub2.argument.source = ScriptArgumentSourceKind::Literal;
    c3_sub2.argument.literal = ScriptArgument::numeric(2000.0);

    CompiledScopedCondition c3;
    c3.kind = ScopedConditionKind::Any;
    c3.children.push_back(c3_sub1);
    c3.children.push_back(c3_sub2);

    std::vector<CompiledScopedCondition> roots = {c1, c2, c3};

    const auto bytecode = BytecodeCompiler::compile(roots);
    assert(!bytecode.empty());
    assert(bytecode.checksum != 0);

    auto ctx = ScriptExecutionContext::rooted(ScopeRef::country(country));
    const bool res = BytecodeVm::evaluate(bytecode, registry, world, ctx);
    assert(res == true);

    // Test failing condition
    CompiledScopedCondition fail_cond;
    fail_cond.kind = ScopedConditionKind::Trigger;
    fail_cond.primitive = trig_treasury;
    fail_cond.argument.source = ScriptArgumentSourceKind::Literal;
    fail_cond.argument.literal = ScriptArgument::numeric(99999.0);

    std::vector<CompiledScopedCondition> fail_roots = {c1, fail_cond};
    const auto fail_bytecode = BytecodeCompiler::compile(fail_roots);

    auto fail_ctx = ScriptExecutionContext::rooted(ScopeRef::country(country));
    const bool fail_res = BytecodeVm::evaluate(fail_bytecode, registry, world, fail_ctx);
    assert(fail_res == false);

    // Queued bytecode can outlive its entity.  A destroyed POP scope must
    // short-circuit the trigger rather than calling a checked SoA accessor.
    const auto dead_pop = world.pops.create({});
    CompiledScopedCondition pop_cond;
    pop_cond.kind = ScopedConditionKind::Trigger;
    pop_cond.primitive = registry.find_trigger("pop_size_above");
    pop_cond.argument.source = ScriptArgumentSourceKind::Literal;
    pop_cond.argument.literal = ScriptArgument::numeric(1.0);
    const auto pop_bytecode = BytecodeCompiler::compile(std::span{&pop_cond, 1u});
    world.pops.destroy(dead_pop);
    auto dead_ctx = ScriptExecutionContext::rooted(ScopeRef::pop(dead_pop));
    assert(!BytecodeVm::evaluate(pop_bytecode, registry, world, dead_ctx));

    std::cout << "[PASS] Bytecode compiler and BytecodeVm execution\n";
}

static void test_condition_memoizer() {
    ConditionMemoizer memo;

    const std::uint64_t program_key = 0xabcdef1234ull;
    const ScopeType scope_type = ScopeType::Country;
    const std::uint32_t scope_id = 42;
    const std::uint64_t salt = 1001;

    bool out = false;
    assert(memo.lookup(program_key, scope_type, scope_id, salt, out) == false);
    assert(memo.misses() == 1);
    assert(memo.hits() == 0);

    memo.record(program_key, scope_type, scope_id, salt, true);
    assert(memo.size() == 1);

    assert(memo.lookup(program_key, scope_type, scope_id, salt, out) == true);
    assert(out == true);
    assert(memo.hits() == 1);
    assert(memo.misses() == 1);

    // Reset tick
    memo.reset_tick();
    assert(memo.size() == 0);
    assert(memo.lookup(program_key, scope_type, scope_id, salt, out) == false);
    assert(memo.misses() == 2);

    std::cout << "[PASS] Condition memoizer lookup, caching and tick reset\n";
}

static void test_scope_context_pool() {
    ScopeContextPool pool;

    const auto root_scope = ScopeRef::country(CountryId{1});
    auto ctx = pool.acquire(root_scope);
    assert(ctx != nullptr);
    assert(ctx->root == root_scope);
    assert(pool.total_created() == 1);
    assert(pool.available_count() == 0);

    // Add state to context
    ctx->set_variable(0x1234, ScriptArgument::numeric(42.0));
    assert(ctx->has_variable(0x1234));

    // Release back to pool
    pool.release(std::move(ctx));
    assert(pool.available_count() == 1);

    // Re-acquire: should recycle without new allocation
    const auto root_scope_2 = ScopeRef::country(CountryId{2});
    auto ctx2 = pool.acquire(root_scope_2);
    assert(ctx2 != nullptr);
    assert(ctx2->root == root_scope_2);
    assert(!ctx2->has_variable(0x1234)); // cleaned
    assert(pool.total_created() == 1); // no new creation
    assert(pool.available_count() == 0);

    pool.release(std::move(ctx2));
    assert(pool.available_count() == 1);

    std::cout << "[PASS] ScopeContextPool zero-allocation reuse\n";
}

int main() {
    std::cout << "Running Script VM Optimization tests...\n";
    test_bytecode_compiler_and_vm();
    test_condition_memoizer();
    test_scope_context_pool();
    std::cout << "All Script VM Optimization tests passed successfully!\n";
    return 0;
}
