# Thunder Strategic Terrain Architecture

Status: 0.3 development foundation. The CPU-side terrain model is implemented and validated; live Vulkan rendering is still pending desktop SDK/GPU validation.

## Goals

Thunder terrain is optimized for a continuously zoomable grand-strategy map, not a character-scale open world. The renderer must keep CPU work nearly constant as the world grows, preserve precision across global coordinates, stream without long frames, and reserve GPU budget for settlements, vegetation, infrastructure, atmosphere and strategy overlays.

## 1. Coordinate precision & Projection

GIS/world coordinates are stored in double-precision projection meters on the CPU. Thunder standardizes on **Mercator Projection** (`mercator`, EPSG:3857) to ensure conformal 1:1 aspect ratio across all tiles and clipmap patches, eliminating the need for expensive multi-pass dynamic reprojection and warping on the GPU. Thunder also retains foundational support for Gall Stereographic Projection (`gall_stereographic`, EPSG:54016). GPU transforms do not receive global float coordinates. Each frame the renderer snaps a floating origin near the camera and emits patch positions relative to that origin as 32-bit floats.

This avoids visible jitter when the camera is thousands of kilometres from the projection origin while keeping GPU vertex formats compact and fast.

Pipeline:

```
lat/lon (WGS84) -> double Mercator projection meters -> snapped camera origin -> relative float GPU position
```

## 2. Clipmap topology

Default clipmap configuration:

- 8 levels
- 8x8 patches on the finest level
- outer levels are square rings that omit the inner 4x4 area
- 1,024 m base patch size
- 256 m floating-origin snap
- 400 active patch instances total

The number of active terrain patches is therefore independent of total world area.

## 3. Static grid reuse and mesh LOD

Thunder does not upload a unique vertex mesh per terrain tile. A small set of static grid vertex/index buffers is reused by every patch instance. Height comes from height pages.

Mesh grids:

| Clipmap level | Grid | Triangles/patch |
|---|---:|---:|
| 0 | 65x65 samples | 8,192 |
| 1-2 | 33x33 samples | 2,048 |
| 3+ | 17x17 samples | 512 |

With the default 400-patch clipmap this produces an estimated 843,776 terrain triangles before frustum/occlusion rejection. Using 65x65 for all 400 patches would be ~3.28 million triangles, so distance-dependent grid LOD removes roughly 74% of baseline terrain geometry while retaining near-camera density.

The current CPU patch payload is 40 bytes or less; 400 patches consume 16 KB.

## 4. Height page format

A terrain height page contains 65x65 unsigned 16-bit samples:

- 4,225 samples
- 8,450 bytes/page
- absolute range: -12,000 m to +20,767.5 m
- step: 0.5 m
- maximum quantization error: ~0.25 m

Absolute quantization is intentional. Per-tile min/max quantization gives slightly better local precision but can reconstruct a shared edge differently on two adjacent tiles. Thunder uses one absolute scale so matching source samples decode to matching heights.

Material/normal pages will use GPU block compression; they are intentionally separate from height residency.

## 5. Residency and streaming

`TerrainPageCache` maps terrain patch keys to fixed GPU page indices. It reserves lookup capacity up front and uses a clock/oldest-frame eviction scan when full. No GPU page identity is tied to a heap object or scene node. The political/map page stream uses its own `WorldMapPageCache` and `WorldMapPageKey`; it does not make terrain geometry the owner of map identity.

`TerrainStreamingPlanner` considers only non-resident visible patches and respects a strict byte budget. Requests prioritize fine clipmap levels before coarse levels.

`StreamingBudgetController` adapts bytes/frame using smoothed CPU/GPU frame time:

- reduce uploads when frame time exceeds the target budget,
- gradually raise uploads when substantial headroom exists,
- enforce hard min/max byte caps.

A camera teleport may therefore show lower-detail/fallback data for a short period, but it should not create an unbounded resource-upload frame.

## 6. RenderGraph terrain path

The terrain graph currently declares three logical passes:

```
Transfer: terrain_stream_upload
                |
                +--> height/material caches
                +--> patch instance buffer

Compute:  terrain_cull
                |
                +--> indirect command buffer

Graphics: terrain_draw
                |
                +--> depth
                +--> HDR color
```

The Vulkan backend will map graph usages to Synchronization2 stage/access/layout tuples and use indirect drawing for the patch groups.

## 7. GPU device tiers

Thunder does not equate a Vulkan version number with renderer readiness. `GpuCapabilities` normalizes physical-device features and scores a device.

Required renderer capabilities currently include:

- Vulkan 1.3-equivalent baseline
- dynamic rendering
- Synchronization2
- timeline semaphores
- buffer device address
- shader draw parameters

Recommended/high-end tiers additionally reward:

- Vulkan 1.4
- descriptor indexing
- indirect draw count
- dedicated compute/transfer queues
- larger device-local memory

This allows Thunder to use a modern 1.4 path where available while not needlessly excluding a capable 1.3 implementation exposing the required feature set.

## 8. Next terrain work

1. Live Vulkan page images and staging ring.
2. Static 65/33/17 grid buffers and indirect draw groups.
3. Britain DEM compiler input.
4. Height page mip/downsample cook.
5. slope/curvature/biome material masks.
6. coast signed-distance field.
7. shallow-water and foam shader.
8. frustum + horizon/occlusion culling.
9. GPU timestamp budgets per terrain pass.

## 9. AI-Generated Map Ingestion & Geometric Purification Protocol

> [!IMPORTANT]
> **CRITICAL REMINDER FOR AI CODERS & ART ASSET PIPELINES**:
> Raw images generated by AI image models (Diffusion/GAN) are **continuous probabilistic RGB rasters with anti-aliasing gradients, JPEG artifacts, and fuzzy boundary pixels**. They MUST NEVER be ingested directly into the engine as province ID or coastline maps without geometric purification.

### Required Ingestion Steps for AI-Created Maps:
1. **Palette Hard Quantization (去模糊最近邻量化)**:
   - All AI edge-blended transitional pixels must be clamped/snapped to exact valid integer Province IDs via nearest Euclidean palette distance (NO fuzzy anti-aliased pixels on borders).
2. **Marching Squares & Bezier Spline Vectorization (轮廓追踪与矢量样条平滑)**:
   - Extract discrete raster borders via Marching Squares into polygonal contours;
   - Apply Chaikin or Cubic Bezier curvature smoothing to turn raster staircases into continuous smooth vector curves.
3. **Exact Euclidean Distance Transform (EDT / JFA 亚像素 SDF 烘焙)**:
   - Bake vector coastlines and borders into signed distance fields (`CoastDistancePage`), allowing the GPU shader's `coast_sdf_smooth` and `fwidth()` to reconstruct infinite resolution, vector-smooth coastlines and national borders at any camera zoom.

