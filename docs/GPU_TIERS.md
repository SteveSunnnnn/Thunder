# Thunder GPU Capability Policy

Thunder is a dedicated PC grand-strategy renderer. It prefers a modern Vulkan 1.4 implementation but feature-checks capabilities rather than rejecting a device solely because its advertised thunder version is 1.3.

## Compatibility tier

Required:

- Vulkan 1.3 or newer
- dynamic rendering
- Synchronization2
- timeline semaphore
- buffer device address
- shader draw parameters

The compatibility renderer may use smaller texture/page caches and more CPU-produced draw lists.

## Recommended tier

Adds:

- descriptor indexing
- draw indirect count
- >= 4 GiB device-local memory
- >= 4096 sampled-image capacity

This is the target tier for the normal Thunder renderer.

## High-end tier

Adds approximately:

- Vulkan 1.4
- >= 8 GiB device-local memory
- dedicated compute queue
- dedicated transfer queue

High-end can increase terrain/material residency, vegetation density, shadow distance and async compute overlap.

## Performance rule

Feature tiers alter quality/budgets; simulation rules never depend on GPU tier. A lower graphics tier cannot change deterministic world results.
