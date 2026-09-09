# Thunder Performance Design Rules

Thunder is a grand-strategy engine. Performance policy is architectural, not a late optimization pass.

## 1. Simulation and renderer never form a blocking lockstep

Simulation publishes immutable compact `RenderSnapshot` payloads through `SnapshotExchange`.
The exchange is SPSC/triple-buffered. Render consumes the newest snapshot available and stale
READY snapshots may be dropped. A slow GPU must not reduce simulation tick throughput.

## 2. No per-frame world checksum

World checksums are for determinism validation, replay checkpoints and save/network diagnostics.
They are deliberately excluded from the ordinary render snapshot hot path. On the initial
100k-country microbenchmark this reduced snapshot construction from ~4.85 ms to ~0.73 ms.

## 3. Frame-local memory is linear

`FrameArena` owns temporary CPU memory for a frame. Allocation is bump-pointer only; the arena is
reset when the corresponding GPU timeline value is retired. Hot render preparation must not use
unbounded general-purpose heap allocation.

## 4. Three frames in flight, timeline-owned resources

`FrameRing<3>` models the renderer frame contexts. Each context will own command pools, transient
CPU allocations, descriptor scratch and upload bookkeeping. Vulkan uses a monotonic timeline for
retirement. The CPU waits only when it is about to reuse a still-live frame slot.

## 5. Modern Vulkan baseline

Thunder targets Vulkan 1.4 where available and requires a modern Vulkan feature set for the main PC
renderer. Architecture uses:

- Dynamic Rendering instead of legacy VkRenderPass/VkFramebuffer graphs.
- Synchronization2 for explicit stage/access dependencies.
- Timeline semaphores for coarse GPU/CPU/queue progress.
- Buffer Device Address for buffer-heavy GPU scene data.
- Descriptor indexing/bindless-style texture access where supported by the target tier.
- Indirect and GPU-driven drawing for buildings, vegetation and map objects.
- Dedicated staging/upload ring with a fixed bytes-per-frame streaming budget.

WSI acquire/present still uses the synchronization primitives required by the platform path.

## 6. RenderGraph compiles hazards once per graph shape

`RenderGraph` derives RAW/WAR/WAW dependencies from declared resource accesses and emits:

- stable topological execution order,
- dependency-free batches suitable for parallel command recording,
- a conservative transition/barrier plan,
- cross-queue transition markers.

The future Vulkan backend maps `RenderUsage` to Synchronization2 stage/access/layout tuples.

## 7. Runtime world is data-oriented

Mutable simulation data uses typed IDs and SoA storage. Hot loops operate on contiguous numeric
columns. Strings and immutable definitions move to the definition database and are not copied into
GPU-facing snapshots.

## 8. Commands are contiguous and reusable

`CommandQueue` is vector-backed with retained capacity. Applying a command batch is a sequential
cache-friendly scan and does not pop/free one container node at a time.

## 9. Modifier invalidation is iterative

Dirty propagation uses a reusable iterative stack, not recursive traversal. Derived nodes can be
batch recomputed in ID order because dependencies are required to exist before a derived node is
created, establishing a topological insertion order.

## 10. Tick graph is parallel-ready but deterministic-first

`TickScheduler::compile()` emits topological waves. 0.2 still executes sequentially. The worker
system will later schedule each due wave concurrently, but world mutation/reduction rules must be
explicit so parallel execution cannot alter deterministic results.

## 11. Budgets are enforced with telemetry

Renderer telemetry records frame number, CPU/GPU/submit time, draw/dispatch counts, triangles and
uploaded bytes. Simulation will receive equivalent per-task tick telemetry before 0.7.

## 12. No object-per-map-feature architecture

Province, building, pop, tree, road segment and similar high-cardinality data may not map 1:1 to
heap objects or scene nodes. Rendering uses chunks, instance buffers, indirect commands and GPU
lookup tables. Simulation uses packed IDs and domain-specific arrays.
