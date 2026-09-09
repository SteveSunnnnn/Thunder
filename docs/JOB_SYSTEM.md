# Thunder Simulation Job System

> Top-level boundaries and dependency direction live in [ARCHITECTURE.md](ARCHITECTURE.md);
> this file is the scheduling subsystem contract.

Thunder's job system is specialized for deterministic grand-strategy simulation. It is not a generic
fiber runtime and does not allow scheduling order to define game results.

## Execution model

- A persistent background worker pool is created once and reused.
- The calling simulation thread participates as an additional worker slot.
- Dense kernels expose a fixed `StablePartition(item_count, grain_size)`.
- Workers dynamically claim deterministic chunk IDs with an atomic cursor.
- Chunk identity is independent of the worker that executes it.
- Nested dispatches execute inline on the current worker instead of recursively blocking the pool.
- Dispatches with fewer than `JobSystem::inline_job_threshold` (8) jobs execute inline; waking the
  pool for very small work was measured to cost more than the work itself.

The default pool caps background workers at 15 until CPU-topology/NUMA probing is implemented. The
platform layer may explicitly choose another count.

## Scratch memory

Each worker owns a `FrameArena`. A dispatch resets worker arenas once, then every job executes with a
`mark()/rewind()` scope. This gives jobs temporary storage without heap churn and prevents hundreds
of chunks handled by one worker from accumulating scratch allocations. Nested jobs preserve the
outer job's scratch allocation because their marker starts at the current head.

## Tick DAG integration

`TickScheduler` still compiles dependency-free waves. A task remains `Serial` by default. Only tasks
explicitly marked `ParallelSafe` are eligible for same-wave parallel execution.

If one due task in a wave is serial, the whole due wave executes in stable order. This conservative
rule prevents an old/staged task from silently changing semantics because a neighboring task was
marked parallel-safe.

For high-cardinality simulation, the preferred pattern is usually:

1. one top-level tick task;
2. fixed entity ranges inside that task;
3. `JobSystem::parallel_for()` over those ranges;
4. deterministic reduction/staging;
5. a stable commit phase.

This is safer than trying to run many arbitrary world-mutating top-level tasks concurrently.

## Waiting and lifecycle

0.7 originally used a condition variable to wait on atomic `remaining` / `active_background`
counters. Stress testing exposed a rare lost-wakeup window because the predicate changed without
holding the condition variable's mutex. The final implementation uses C++20 `atomic::wait/notify`
for those counters.

ThreadSanitizer also exposed a destructor ordering race: relying on `std::jthread` member destruction
could destroy condition variables before workers fully exited. Thunder now explicitly joins workers in
the `JobSystem` destructor while synchronization primitives are still alive.

## Profiling

`JobDispatchStats` records jobs, available worker slots, workers that actually executed work and wall
clock duration. `TickExecutionProfile` records each due wave, actual multi-thread use and elapsed
nanoseconds. `TickScheduler::to_dot()` exports the compiled task graph to Graphviz DOT for visual
inspection.

These are diagnostic interfaces; they do not feed timing back into simulation decisions.
