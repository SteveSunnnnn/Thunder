# Thunder Determinism Contract

Grand-strategy simulation must produce the same logical result regardless of worker scheduling and,
where supported by a kernel, regardless of worker count.

## Fixed partitioning

`StablePartition` uses an explicit grain size. Grain is a simulation/runtime constant, not derived
from CPU thunder count. A 4-thunder and 32-thunder machine therefore produce the same chunk boundaries.

This matters for:

- floating-point accumulation order;
- random stream assignment;
- command merge order;
- replay/OOS diagnostics.

Performance tuning may change grain between engine/content versions, but such a change is treated as
a deterministic-runtime change and must be versioned/tested.

## Deterministic reductions

`DeterministicReduction<T>` stores one partial per chunk ID. Work stealing/dynamic claiming can move a
chunk to another worker, but final folding always walks chunk IDs from 0..N-1. Tests compare the
bit representation of floating-point results across different worker counts.

## Randomness

Stateful RNG shared by workers is forbidden for parallel simulation. `DeterministicRng::keyed_u64`
and `keyed_unit` derive samples from stable `(base_seed, stream_key, counter)` values. A useful stream
key is an entity ID plus a system-specific salt; the counter can be the game tick or local sample
index.

A worker ID must never participate in a gameplay RNG seed.

## Command staging

Parallel jobs should not push directly into the global `CommandQueue`. `DeterministicCommandStage`
provides chunk-owned, cache-line-separated lanes. Each chunk may emit commands independently; flush
walks lanes in ascending chunk order and only then assigns global command sequence numbers.

This makes the commit order independent of which worker completed first.

## Mutable world access

`TickTaskMode::ParallelSafe` is an explicit contract. A parallel-safe task may only:

- write disjoint entity ranges;
- write its own chunk-local outputs;
- write deterministic staging/reduction slots;
- read immutable definitions or data guaranteed stable for that wave.

It may not mutate shared containers or overlapping SoA elements without a dedicated deterministic
staging protocol.

## Floating point

Money, prices and market quantities are authoritative in signed 64-bit fixed point
(milli-units; see ECONOMY_ARCHITECTURE.md). Doubles still live on simulation hot paths
(country aggregates, modifier graphs, script values, command payloads), and `Fnv1a64`
hashes them by bit pattern (normalizing only `-0.0` and NaN payloads), so any FP
evaluation difference between builds or machines becomes checksum divergence.

Rules for simulation code:

- Allowed: `+`, `-`, `*`, `/`, `sqrt` (all correctly rounded in IEEE-754 binary64),
  comparisons, and `llround` of bounded values. Thunder simulation and scripting use
  nothing else; the only libm call in simulation is `std::sqrt` in
  `HierarchicalPathfinder`.
- Forbidden in simulation state: transcendental libm calls (`sin`/`cos`/`pow`/`exp`/
  `log`, ...), whose results are not bit-stable across platforms and library versions.
  Presentation code may use them freely; the boundary is the render payload.
- Forbidden build-wide: fast-math family flags (`-ffast-math`,
  `-funsafe-math-optimizations`, `-ffinite-math-only`, `-ffp-contract=fast`, `-Ofast`,
  `/fp:fast`, `-fp-model=fast`). `CMakeLists.txt` rejects them at configure time.
- No reliance on FP contraction: a plain `a*b+c` must round twice. Contracted and
  strict builds are both valid C++ but produce different checksums on identical inputs.
- Country treasury compatibility doubles are quantized to milli at every write
  boundary (`CountryStore`), so the legacy double column is always an exact view of
  the authoritative integer and cannot accumulate its own rounding error.

`fp_environment_fingerprint()` (`foundation/base/FpEnvironment.hpp`) probes contraction,
flush-to-zero/denormals-as-zero and the rounding mode, and folds the results into a
per-build fingerprint. Compare fingerprints across builds/machines before trusting
shared checksums, replays or future multiplayer handshakes. A fingerprint change is a
deterministic-runtime change and must be versioned/tested like a grain change.

x87 excess precision is not a concern: x64 implies SSE2 scalar math, and 32-bit x86
builds are unsupported.

## Checksums and compatibility

Thunder already maintains world checksums, effective-content hashes and `.thunderworld` build hashes. These
will be combined with replay command streams in later multiplayer/OOS work. Timing/profiler data is
never included in simulation checksums.
