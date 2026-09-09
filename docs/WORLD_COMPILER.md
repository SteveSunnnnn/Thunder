# Thunder World Compiler / `.thunderworld` format

> Top-level boundaries and dependency direction live in [ARCHITECTURE.md](ARCHITECTURE.md);
> this file is the world-compiler and pack-format contract.

## Goal

Runtime never parses GeoJSON/Shapefile or computes coast distance fields. Expensive GIS repair,
rasterization and derived-data generation happen offline. Runtime sees a seekable, indexed binary
pack whose cost scales with the pages currently needed by the camera.

## Pipeline

```text
GIS / DEM / historical source
        |
        v
source-specific preprocessors
        |
        +-- province-id 128x128 pages (R16_UINT convention, 0 = water)
        +-- coast signed-distance 128x128 pages (int16, 0.5 m units)
        +-- height pages
        +-- lake-mask pages
        +-- adjacency / geography identity / strings
        +-- chunked river/transport vectors
        +-- architecture-region and resource-distribution tables
        |
        v
thunder_world_compiler
        |
        v
world.thunderworld
```

Source-specific GIS tools may use Python/QGIS/GDAL/GeoPandas/Rasterio. Those dependencies are not
part of the shipping game runtime.

## Pack structure

- 64-byte little-endian header (format version 2).
- Independently compressed chunks.
- 64-byte data alignment by default.
- Sorted fixed-size index at end of file.
- Chunk identity: `(type, level, x, y, variant)`.
- Zstd level 3 when it saves at least 5%; otherwise raw.
- Checksum algorithm is stored per chunk (XXH3-64 when available; FNV-1a fallback), so packs are self-describing.
- Header stores a deterministic build hash derived from sorted chunk keys, raw sizes and chunk integrity hashes.
- Duplicate chunk keys are rejected by O(1)-average hash lookup during compilation.

A 64 KiB political page bundle stores:

```text
0 KiB  .. 32 KiB : 128x128 uint16 province ID page
32 KiB .. 64 KiB : 128x128 int16 coast signed-distance page
```

Keeping both layers atomic prevents a frame from observing different residency generations.

The page family also carries one `LakeMask` and one `SpatialMask` page per clip
level. Lake provinces remain real province IDs for picking/history, but the
masks keep the renderer from treating water or non-land pixels as land. Static
vector/table chunks use binary contracts:
`RIV1`/`PTR1` for chunk-local polylines, `ARC1` for province architecture
assignments, and `RDS1` for state resource capacities. Runtime never parses the
source GIS or the compiler's intermediate JSON.

`CountryDefinitions` in a world pack is an identity table (`tag` only in CNT2).
Population, GDP, treasury, tax rules, goods, buildings and POPs are content
script data and are bound by `GameContentRuntime` after the pack is loaded.

## Runtime I/O

`RandomAccessFile` uses POSIX `pread()` on Unix-like systems. This has no shared seek cursor, so
multiple streaming workers can read one file descriptor concurrently. The portable fallback uses a
mutex-protected stream.

Each streaming worker owns a `WorldPackDecodeScratch`:

- compressed input vector capacity is reused;
- decoded vector capacity is reused;
- Zstd decompression context is reused;
- no open/close per page;
- no temporary allocation per ordinary page after warmup.

The build hash is independent of manifest row order after key sorting; it is suitable for cache invalidation, save/mod compatibility diagnostics and OOS reports.

Index lookup is binary search over a contiguous sorted vector. A million lookups are cheap enough
that a hash table is not justified in the runtime hot path at current world sizes.

## Alignment decision

The initial prototype aligned every chunk to 4 KiB. That was rejected: highly compressible map
pages can shrink to hundreds or a few thousand bytes, making 4 KiB-per-chunk holes dominate pack
size. Thunder now defaults to 64-byte chunk alignment. OS page cache and storage sectors still operate
at their natural block size; logical chunks do not need 4 KiB offsets for ordinary buffered
`pread()`.

If DirectStorage/io_uring/O_DIRECT-style paths later prove useful, Thunder can introduce larger packed
I/O blocks without changing logical chunk keys.
