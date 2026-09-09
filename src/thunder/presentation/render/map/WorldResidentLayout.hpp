#pragma once

#include <cstdint>

namespace thunder {

// ---- World-resident pyramid layout (A2-R) ---------------------------------
//
// The whole clipmap (all four levels, every page) lives permanently in three
// global textures; nothing is streamed after startup. Pages are tiled into
// each level's rectangle at a fixed pixel stride, so a page's texel origin is
// a compile-time-known affine map of its (level, x, y) key. Levels sit side
// by side inside one texture like a hand-laid mip chain, which keeps every
// sampler dynamically uniform (no descriptor arrays, no nonuniform indexing)
// and makes cross-page filtering free: same-level pages are physically
// adjacent, so LINEAR taps may cross page borders without any slot clamping.
//
//   height_pyramid : R16_UNORM 3900x1820. 65x65 corner samples per page at a
//                    65-px stride; adjacent pages share their baked edge
//                    column value, so texel = origin + pg*65 + 0.5 + f*64.
//   province_id    : R16_UINT 7680x3584. Native 128x128 cell-centred page at
//                    a 128-px stride, sampled with texelFetch only.
//   coast_sdf      : R16_SNORM 7680x3584. Same tiling, hardware LINEAR.
//
// Level 3 keeps (5, 3.5) pages: the bottom page row is allocated in full
// (rows = ceil), but only its upper half is inside the world. A real mip
// chain could not express this (1820/8 = 227.5 rows, half a row off), which
// is why the layout is baked by hand.

struct WorldLevelLayout {
    std::uint32_t cols;
    std::uint32_t rows;    // ceil for the last partial row (level 3: 3.5)
    std::uint32_t h_ox;
    std::uint32_t h_oy;
    std::uint32_t p_ox;
    std::uint32_t p_oy;
};

inline constexpr WorldLevelLayout kWorldLevels[4] = {
    {40, 28, 0,    0,    0,    0},
    {20, 14, 2600, 0,    5120, 0},
    {10, 7, 2600,  910,  5120, 1792},
    {5,  4, 3250,  910,  6400, 1792},
};

inline constexpr std::uint32_t kWorldLevelCount = 4u;
inline constexpr std::uint32_t kHeightPyramidW = 3900u;
inline constexpr std::uint32_t kHeightPyramidH = 1820u;
inline constexpr std::uint32_t kPagePyramidW = 7680u;
inline constexpr std::uint32_t kPagePyramidH = 3584u;
// 65x65 corner samples, u16, row-padded to a multiple of 4 bytes.
inline constexpr std::uint32_t kHeightPageBytes = 65u * 65u * 2u;
inline constexpr std::uint32_t kHeightPageStride = 8452u;
// One 128x128 u16 plane (province ids or coast SDF) per page.
inline constexpr std::uint32_t kPagePlaneBytes = 128u * 128u * 2u;
// Per-page upload block inside the staging ring: height plane + both page
// planes, partitioned as [height | province | sdf].
inline constexpr std::uint32_t kWorldUploadPageStride =
    kHeightPageStride + 3u * kPagePlaneBytes;
inline constexpr std::uint32_t kWorldUploadPageBudget = 128u;
inline constexpr std::uint32_t kBasePageCols = 40u;
inline constexpr std::uint32_t kBasePageRows = 28u;
// Pack encoding: height = raw * 0.5 m - 12000 m. Through R16_UNORM the raw
// value arrives as raw/65535, so metres = n * 32767.5 - 12000 exactly.
inline constexpr float kHeightMetersPerUnit = 32767.5f;
inline constexpr float kHeightBiasMeters = -12000.0f;

// std140 mirror of the WorldLevels UBO (48 floats: three vec4s, two vec4[4]
// arrays, one vec4). vec4 arrays keep the 16-byte stride, so the host struct
// is a plain float array.
inline constexpr std::uint32_t kWorldLevelsUboFloats =
    4u + 4u + 4u + 16u + 16u + 4u;

inline constexpr std::uint32_t world_page_count(std::uint32_t level) noexcept {
    return kWorldLevels[level].cols * kWorldLevels[level].rows;
}

inline constexpr std::uint32_t world_page_index(std::uint32_t level,
                                                std::uint32_t x,
                                                std::uint32_t y) noexcept {
    return y * kWorldLevels[level].cols + x;
}

inline constexpr std::uint32_t world_total_page_count() noexcept {
    return world_page_count(0u) + world_page_count(1u) +
           world_page_count(2u) + world_page_count(3u);
}

} // namespace thunder
