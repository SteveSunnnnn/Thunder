# Thunder Map Mode Architecture

> Top-level boundaries and dependency direction live in [ARCHITECTURE.md](ARCHITECTURE.md);
> this file is the map-mode subsystem contract.

Map mode switching must not regenerate province geometry or repaint a world-sized texture.

## Resident data

`MapModeStore` keeps seven data modes in compact mode-major arrays:

- population — log1p + UNORM16
- GDP — log1p + UNORM16
- GDP per capita — log1p + UNORM16
- standard of living — linear + UNORM16
- market — uint16 category
- culture — uint16 category
- religion — uint16 category

Political ownership stays in `PoliticalMapState`; terrain needs no per-province map-mode value.

At 8,000 provinces:

```text
8,000 * 7 modes * 2 bytes = 112,000 bytes
```

All common modes can therefore remain resident on GPU. Switching mode changes only a small view /
uniform containing mode kind, element offset, transform range and generation.

## Scalar transforms

Population and GDP-like values are encoded after `log1p` to prevent a handful of extreme provinces
from consuming almost the entire visual range. Standard of living remains linear.

A full scalar rebuild computes min/max and encodes to 16-bit. Single-province updates reuse the
current range and clamp if necessary; periodic full rebuilds refresh the range.

## GPU compatibility

The CPU store is 16-bit. GPUs exposing storage-buffer 16-bit access may consume it directly.
Compatibility devices can expand to 32-bit during upload without changing simulation or content
formats. 16-bit storage is an optimization, not a minimum engine requirement.

## Performance rule

Map-mode selection itself must never trigger GIS work, province raster rebuild, ownership rebuild or
full color-texture generation. Data changes upload compact province values; mode selection is a
uniform/view change.
