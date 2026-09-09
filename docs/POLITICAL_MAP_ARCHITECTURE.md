# Thunder Political Map Architecture

> Top-level boundaries and dependency direction live in [ARCHITECTURE.md](ARCHITECTURE.md);
> this file is the political-map subsystem contract.

Thunder treats the political map as a **virtualized lookup surface**, not as thousands of mutable meshes.
The permanent GIS geometry is compiled once; normal gameplay changes only small indirection buffers.

## 1. Runtime data path

```text
World-map / screen position
        |
        v
Province ID virtual page (R16_UINT)
        |
        +--> province id
                |
                v
       ProvincePoliticalRecord[province]
                |
                +--> owner country id
                +--> visual flags
                |
                v
           CountryColor[country]

Map mode:
Province ID -> ProvinceMapValue[province] -> GPU color ramp
```

A conquest therefore changes one 8-byte `ProvincePoliticalRecord`; it does **not** regenerate a
country mesh or repaint the province raster. Country recoloring is a 4-byte palette edit.

## 2. Province ID pages

`ProvinceRasterPage` is 128 x 128 x uint16 = **32 KiB**.

- encoded ID 0 = water/no province;
- runtime province N = encoded N+1;
- max supported province count in this raster tier = 65,534;
- intended production worlds are far below that limit;
- runtime GPU format target: `R16_UINT`;
- pages form a multiresolution pyramid keyed by `WorldMapPageKey`; terrain
  mesh topology may consume the same geographic page family without owning
  its identity.

At the default level-0 64,000 m page size, one texel represents 500 m. Coarser levels double
page world size. The world compiler will bake all levels so zooming does not require runtime GIS
work. A close-up terrain clipmap can use a finer mesh/page family without changing political
page identity.

## 3. Coast distance pages

`CoastDistancePage` is 128 x 128 x int16 = **32 KiB**.

Signed distance convention:

- negative = water;
- positive = land;
- 0.5 m quantization;
- approximately +/-16.38 km range.

The same page drives shallow-water color, shoreline foam, beach masks and coast highlights. Keeping
one distance field avoids separate coast textures that can drift out of alignment.

Province ID + coast distance are streamed as one **64 KiB atomic bundle**. A shader should never
observe a province page from generation N and a coast page from generation N-1.

## 4. Sparse vs dense political updates

`PoliticalMapState` tracks dirty spans for:

- province owner/flags: 8 bytes/province;
- country color: 4 bytes/country;
- map-mode scalar: 4 bytes/province.

Small edits are merged into sparse upload spans. Once dirty marks exceed roughly 1/32 of the target
buffer, Thunder deliberately switches to one full-buffer upload. This trades a few hundred KiB of
sequential PCIe/staging traffic for eliminating an O(N log N) sparse-range sort. For a typical
8,000-province world, the entire owner+map-value data set is only ~96 KiB.

Measured regression example in this container:

- 100 adjacent ownership changes on an 8,000-province map: about 800 owner bytes uploaded;
- 10,000 random edits on a synthetic 100,000-province map: density threshold selects a ~1.2 MiB
  full owner+map-value upload instead of sorting thousands of tiny copies.

The exact crossover can later be tuned from real GPU traces.

## 5. Picking / hover

Thunder does not use synchronous GPU readback for ordinary province hover.

`ProvincePickingCache` keeps a small CPU mirror of recently streamed `ProvinceRasterPage`s. A pick is:

```text
world double coordinate
  -> virtual page key
  -> hash lookup
  -> local texel
  -> uint16 province id
```

If the preferred high-resolution page is absent, picking may fall back through coarser resident
levels. This avoids `vkCmdCopyImageToBuffer`, fences and one-frame latency in the input hot path.

A 64-page CPU picking cache is about 2 MiB. The default planned production cache of 512 pages is
about 16 MiB and can be scaled by platform memory tier.

## 6. Borders

Initial GPU border detection can compare neighboring province IDs in the ID texture. Production
quality will add pre-baked edge/SDF support from the world compiler for stable line width at distant
zooms and for country/province/selection/frontier channels.

Ownership remains indirect:

```text
province A id -> owner country A
province B id -> owner country B
owner A != owner B => country border
```

No country-border mesh rebuild is required after conquest.

## 7. Province topology for simulation

`ProvinceAdjacencyGraph` stores immutable topology as CSR:

- `offsets[province_count + 1]`;
- contiguous 8-byte `ProvinceNeighbor` records;
- symmetric edges;
- land / river / strait / impassable flags;
- fixed-point base movement cost.

This layout is deliberately separate from the raster. Rendering wants dense image sampling;
pathfinding, fronts, trade and border logic want contiguous graph neighbor scans.

## 8. Performance budgets

Target steady-state CPU costs at production scale:

| Work | Budget |
|---|---:|
| political state edits | <0.1 ms typical frame |
| dirty-span planning | <0.1 ms typical; dense fallback instead of large sort |
| province hover/pick | effectively negligible relative to frame budget |
| political page streaming plan | <0.05 ms |
| topology neighbor access | contiguous span, no allocation |

GPU cost must be measured once the live Vulkan backend runs. The current flat strategic map pass
uses an instanced visible-page mesh over the resident virtual-page atlas and compact buffers; a
future 3D terrain clipmap may sample the same page family without moving page admission into the
Vulkan facade.
