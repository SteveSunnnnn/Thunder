# Script-first game content contract

Thunder is an engine runtime, not the first game's rules database. Countries,
goods, buildings, production methods, needs, technologies, events, decisions,
journals, AI plans, notifications, on-actions, localization and scripted GUI
belong in mounted content packages. C++ may provide only reusable state stores,
validated commands, deterministic simulation kernels, scope traversal and
rendering primitives.

## Boundary rule

A new game rule must first be attempted as a definition plus ThunderScript. Add a
C++ primitive only when at least one of these conditions is true:

- the operation is a high-cardinality deterministic kernel that cannot meet the
  tick budget in bytecode;
- it owns authoritative state and therefore needs save/checksum/migration
  integration;
- it exposes a generic query, iterator or command that multiple content
  packages can reuse;
- it crosses an OS, renderer, asset or networking boundary.

A primitive must not contain country names, historical dates, balance values,
event prose, UI labels or first-game-specific branching. Those remain content.
Every authoritative primitive requires stable identity, bounded validation,
save/load, checksum and deterministic continuation tests.

## Naming contract

Public engine identifiers, filenames, resource keys, diagnostics and built-in
examples must use capability-oriented names that remain valid across eras and
game projects. Historical periods, countries, commercial titles and a specific
game's art direction belong to mounted content packages. The engine may expose
generic style primitives such as parchment panels, ornamental frames or warm
archival grading, but it must not encode a particular setting in their names.

This rule is enforced without legacy aliases: renaming a topic-bound engine API
removes the old symbol so new scripts and native integrations cannot depend on
it accidentally.

## Economy definitions

The content loader accepts the following strict, replace-by-key definitions.
Unknown fields are errors. References are resolved transactionally by
`DefinitionDatabase::bind_economy`; a failed bind leaves the destination
`EconomyDefinitions` unchanged.

```thunder
good grain {
    base_price_milli = 1000
    category = staple
}
```

Goods carry a `category` — `staple`, `luxury`, `military`, `industrial` or
`currency` (monetary metals/coinage whose market price anchors CurrencyStore
parities). A currency good names its metal explicitly:
`monetary_metal = gold|silver` (requires `category = currency`; the market
price of the bound good drives `update_exchange_rates`). Unknown categories or
metals are rejected at ingest. Any numeric field accepts
a constant arithmetic expression folded at parse time, e.g.
`base_price_milli = (900 - 100) / 4`; see `docs/THUNDER_SCRIPT.md`.

```thunder
good gold_bullion {
    base_price_milli = 32000
    category = currency
    monetary_metal = gold
}
construction_goods base {
    input = { good = tools quantity_milli = 40 }
    input = { good = grain quantity_milli = 25 }
}
```

The `construction_goods` block declares the materials basket consumed per
construction point (milli-units). The block name is ignored and `input` rows
accumulate across files; shortages throttle queued projects (Leontief). An
absent block keeps the money-only legacy construction model.

building_type farm {
    visual = farm
    workers_per_level = 5000
    input  = { good = tools quantity_milli = 50 }
    output = { good = grain quantity_milli = 1200 }
}

production_method intensive_farming {
    building_type = farm
    throughput_ppm = 1250000
    input  = { good = tools quantity_milli = 90 }
    output = { good = grain quantity_milli = 1700 }
}

need_profile workers {
    need = { good = grain quantity_milli = 100 }
}
```

Game content should use stable symbolic keys. Dense `GoodId`,
`BuildingTypeId`, `ProductionMethodId` and `NeedProfileId` values are generated
only during binding and must not appear in authored files. Optional presentation
roles such as `building_type.visual` are also authored symbolically; the client
does not infer them from the type key.

## UI and fonts

UI scripts operate in logical window coordinates with a top-left origin and
positive Y downward. The Vulkan backend converts scissors to swapchain pixels,
so high-DPI windows must not author a second scale factor.

Production text uses an MSDF font package: a `.thunderfont` metrics file and a
matching `_atlas.thunderimg` image. Generate both from a game-owned font:

```powershell
python tools/assets/cook_fonts.py --font <font-file> --out-dir <output> `
  --name ui_serif --atlas-key fonts/ui_serif
```

Thunder does not prescribe a typeface family. Serif, sans-serif and CJK fonts use
the same UTF-8/MSDF path; the mounted content package selects the font file and
the charset it needs. Pass `--charset <file>` for a UTF-8 charset specification
containing project-specific ranges or code points.

An engine host may provide content-owned resources before renderer
initialization:

```powershell
$env:THUNDER_UI_FONT_ATLAS = "<output>/ui_serif_atlas.thunderimg"
$env:THUNDER_UI_FONT_METRICS = "<output>/ui_serif.thunderfont"
```

The world map is not a UI texture. An engine host supplies an external
`.thunderworld` path; the map pipeline streams page, height, coast and
political-palette chunks directly from that pack.

World entities stay data-defined as well. After the pack resolves stable
province/state/market IDs, the content layer can materialize script objects such
as:

```text
building london_farm {
    province = london
    type = farm
    production_method = farm_standard
    level = 3
}

pop london_workers {
    province = london
    size = 120000
    need_profile = household
    employer = london_farm
}
```

The engine provides only the schema and binding mechanics; these values are
not embedded in C++ or generated from GIS economics.

Country presentation data follows the same rule, for example
`map_color = { r = 150 g = 72 b = 64 a = 255 }` belongs in the country script.

The built-in 5x7 pixel font is a diagnostics fallback only. It is intentionally
not a shipping UI path, does not provide CJK coverage and emits a desktop
warning whenever no MSDF package is configured.

## Content acceptance gate

Before a package is installed into an engine world:

1. mount the deterministic mod load plan;
2. compile through the engine scripting pipeline and reject all parse,
   unknown-field, link, type and reference diagnostics;
3. bind definitions into staging stores and commit only after all references
   resolve;
4. include the effective content hash in save and multiplayer compatibility;
5. validate deterministic continuation after save/restore.
