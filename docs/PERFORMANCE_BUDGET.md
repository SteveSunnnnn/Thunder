# Thunder Performance Budgets

These are engineering budgets, not promises. They are used to reject designs early.

## Simulation

Reference release target initially assumes a modern 8-thunder desktop CPU.

- Base tick: 6 in-game hours.
- Normal-speed tick p95: < 25 ms simulation-only.
- Weekly heavy tick p95: < 120 ms in mature simulation.
- Renderer must never force the simulation to wait on ordinary GPU work.
- 100k–300k pop cohorts must be feasible without one-object-per-pop allocation patterns.
- Tick profiler must report task name, frequency, wall time, worker utilization and dependency wait.

## Rendering

Initial reference: 2560x1440, RTX 3060/4060 class.

- 60 FPS high-level target: <= 16.6 ms GPU frame.
- Ordinary map camera CPU render submission: < 4 ms.
- Province selection readback must not stall the frame.
- Buildings/vegetation use chunked GPU instance buffers, never node/object-per-instance.
- World streaming has fixed per-frame upload budgets to prevent camera hitching.

## Memory

- Runtime world state favors packed IDs and SoA vectors.
- Shipping world map is cooked binary, mmap/stream friendly.
- Asset lifetime is chunk/stream based.
- Long-lived general heap allocations inside per-pop/per-building tick loops are prohibited.
