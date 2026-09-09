#pragma once

#include <cstdint>

namespace thunder {

// Render quality tiers.
//
// `Legacy` is not a shipping preset: it reproduces the pre-optimisation render
// path exactly (no MSAA, no depth attachment, direct-to-swapchain, full-octave
// procedural terrain). Keeping it addressable lets a single binary measure the
// baseline and the optimised path under identical driver and clock conditions,
// which is far more trustworthy than comparing two separately built binaries.
enum class RenderQuality : std::uint8_t {
    Legacy = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4,
};

struct RenderQualitySettings {
    // Multisample count for the HDR scene target. 1 disables MSAA entirely.
    std::uint32_t msaa_samples = 1;
    // FXAA is the cheap analytic fallback used when MSAA is too expensive.
    bool fxaa = false;
    // Triangular-PDF dither before 8-bit quantisation. Removes banding on the
    // wide, low-contrast atlas gradients the paper look depends on.
    bool dither = true;
    // Depth attachment for the 3D passes.
    bool depth_buffer = false;
    // Render 3D into an offscreen HDR target and tonemap into the swapchain.
    // Required for dithering, FXAA and any future post-processing.
    bool hdr_target = false;
    // Octave count for the structural (height) terrain FBM. Each octave costs
    // four hash evaluations per fbm sample, so this is the dominant shader knob.
    std::uint32_t terrain_octaves = 5;
    // Octave count for the low-amplitude aesthetic layers: moisture, paper
    // cloud and press wash. These carry very little contrast, so they tolerate
    // aggressive octave reduction with no visible change.
    std::uint32_t terrain_detail_octaves = 5;
    // Frustum + distance LOD culling for instanced living-geometry.
    bool lod_culling = false;
    // Internal render scale. <1.0 renders fewer pixels and upscales; the
    // escape hatch for weak hardware at high display resolutions.
    float render_scale = 1.0f;
};

// Capability snapshot used to auto-select a tier. Kept deliberately narrow:
// only facts that are cheap to query and stable across drivers.
struct GpuTierInputs {
    std::uint64_t device_local_bytes = 0;
    bool discrete_gpu = false;
    // Maximum colour sample count supported by the device (bitmask of
    // VkSampleCountFlagBits). The caller passes the raw limit so this header
    // stays free of Vulkan headers.
    std::uint32_t color_sample_counts = 1u;
    // Set when framebufferColorSampleCounts advertises at least 4x.
    bool supports_msaa4 = false;
};

// Map a tier onto concrete settings. `max_msaa` clamps the multisample request
// to what the device actually advertises, so a tier never asks for samples the
// hardware cannot provide.
[[nodiscard]] RenderQualitySettings make_quality_settings(RenderQuality tier,
                                                          std::uint32_t max_msaa) noexcept;

// Pick a sensible starting tier from device capabilities. Returns Ultra/High
// for discrete parts with headroom and degrades to Low on integrated or
// memory-constrained devices.
[[nodiscard]] RenderQuality auto_quality_tier(const GpuTierInputs& gpu) noexcept;

[[nodiscard]] inline const char* quality_tier_name(RenderQuality tier) noexcept {
    switch (tier) {
        case RenderQuality::Legacy: return "legacy";
        case RenderQuality::Low: return "low";
        case RenderQuality::Medium: return "medium";
        case RenderQuality::High: return "high";
        case RenderQuality::Ultra: return "ultra";
    }
    return "unknown";
}

} // namespace thunder
