# Third-party notices

## SDL 3.4.14

Destiny's optional desktop host builds from the pinned SDL source archive at
`thirdparty/SDL3-3.4.14.zip`. SDL is distributed under the zlib license. The
archive is included so ordinary desktop compilation does not require network
access. Upstream: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.14

Thunder itself is distributed under the MIT License. Third-party components keep
their original licenses.

## Vendored

### Khronos Vulkan-Headers

`thirdparty/vulkan-headers/` contains a Vulkan-Headers distribution from The
Khronos Group. Its files are offered under Apache-2.0 or MIT terms as described
in `thirdparty/vulkan-headers/LICENSE.md`. The directory is marked as vendored
for GitHub language statistics.

- Upstream: `https://github.com/KhronosGroup/Vulkan-Headers.git`
- Imported version: Vulkan 1.4 Header Release (`VK_HEADER_VERSION 360` / `1.4.360`, compatible with Vulkan SDK 1.4.303+)
- Imported revision: `b51f6b865c18fc5b33990d12f75e8dfd672cede6`

## Bundled UI fonts

`assets/fonts/` ships MSDF atlases baked from two fonts distributed under the
SIL Open Font License 1.1:

- **Playfair Display Bold** (display/title atlas `ui_display`) — Copyright 2017
  The Playfair Display Project Authors (https://github.com/clausru/Playfair-Display).
- **EB Garamond Medium** (body atlas `ui_body`) — Copyright 2017 The EB Garamond
  Project Authors (https://github.com/octaviopardo/EBGaramond12).

Both are licensed under the SIL Open Font License, Version 1.1; see
https://openfontlicense.org for the full license text. The atlases were
generated with `tools/assets/cook_fonts.py` (msdf-atlas-gen, MIT).

## Acquired or detected at build time

- Zstandard (`libzstd`) is an optional `.thunderworld` compression backend detected via
  pkg-config (`THUNDER_HAS_ZSTD=1`); distributed under BSD/GPL dual licensing terms.
- xxHash (`libxxhash`) is an optional XXH3-64 integrity backend detected via
  pkg-config (`THUNDER_HAS_XXHASH=1`); distributed under BSD-2-Clause terms.
- Vulkan SDK/loader is an external dependency for host applications that enable
  the optional Vulkan renderer and is distributed separately by its vendor/Khronos
  packages.

No proprietary commercial-game source code or extracted commercial-game asset
is licensed for inclusion in this repository.
