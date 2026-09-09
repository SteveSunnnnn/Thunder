# Thunder Living Map Architecture

> Top-level boundaries and dependency direction live in [ARCHITECTURE.md](ARCHITECTURE.md);
> this file is the Living Map subsystem contract.

The Living Map converts simulation state into compact, chunk-streamable visual state without making render objects authoritative simulation entities.

## Spatial authority

Simulation entities use stable IDs and Province membership. Visual placement is supplied by `SpatialPlacementDatabase`, loaded from `.thunderworld` by `WorldBootstrap`.

The current authored spatial inputs are:
- `PlacementCandidates` classified as Buildable, Urban, Rural, Industrial, Farm, Mine, Port or Vegetation.
- `SettlementAnchors` with explicit 64 km chunk-local positions and importance.
- Province definitions/centres as deterministic fallback only.

A Province may span multiple Living Map chunks. Province → candidate and Province → chunk indices are built once after loading. A Province simulation change marks all chunks associated with that Province; unchanged chunks retain their version and require no upload.

## Instance generation

Visual categories choose authored candidates by semantic class:
- Residential / commercial → Urban, then Buildable.
- Factory → Industrial, then Urban, then Buildable.
- Farm → Farm, then Rural, then Buildable.
- Mine → Mine, then Buildable.
- Port → Port, then Buildable.

Weighted deterministic selection uses stable IDs/seed inputs. Shipping packs are expected to carry authored semantic hubs and placement candidates; missing authored placement is a content-check failure rather than a second map production path.

## Settlement clusters

Medium/far settlement clusters prefer the highest-importance authored `SettlementAnchor`. If none exists, Urban/Buildable placement can seed the cluster; Province centre is the final fallback.

## GPU payloads and streaming

Near-detail instances and medium/far clusters use compact 16-byte GPU-facing records. Chunks maintain render versions. The streaming planner selects near instances versus medium/far clusters by camera distance and instance budget; stale simulation frames may be skipped rather than blocking the simulation thread.

The CPU system deliberately does not own Vulkan objects. `LivingMapRenderPlan` describes upload/cull/indirect-draw dependencies for the render backend.

## Transport

Road/rail/canal geometry is split into 64 km chunks. Long segment traversal uses grid DDA rather than bounding-box-area enumeration, so preprocessing is proportional to traversed cells instead of the area of the segment bounding box.

## Remaining visual-production work

The data/placement architecture is implemented, but final visual fidelity remains a renderer/content responsibility: production terrain/material pipelines, vegetation placement, authored road/rail polylines, cultural architecture assets, lighting/atmosphere/post-processing and physical-GPU visual QA are not implied by the CPU Living Map tests.
