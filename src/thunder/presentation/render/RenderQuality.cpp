#include "thunder/presentation/render/RenderQuality.hpp"

#include <algorithm>

namespace thunder {
namespace {

// Round a requested sample count down to the nearest power of two that the
// device actually advertises. Asking for samples the hardware lacks is a
// hard pipeline-creation failure, so every tier is clamped here rather than at
// each call site.
[[nodiscard]] std::uint32_t clamp_samples(std::uint32_t requested, std::uint32_t max_supported) noexcept {
    if (max_supported == 0u) {
        return 1u;
    }
    for (std::uint32_t samples : {8u, 4u, 2u}) {
        if (requested >= samples && (max_supported & samples) != 0u) {
            return samples;
        }
    }
    return 1u;
}

} // namespace

RenderQualitySettings make_quality_settings(RenderQuality tier, std::uint32_t max_msaa) noexcept {
    RenderQualitySettings settings;

    // Legacy is a frozen snapshot of the pre-optimisation path. It must stay
    // byte-for-byte equivalent to the original behaviour or the benchmark
    // comparison stops meaning anything.
    if (tier == RenderQuality::Legacy) {
        settings.msaa_samples = 1;
        settings.fxaa = false;
        settings.dither = false;
        settings.depth_buffer = false;
        settings.hdr_target = false;
        // Legacy must use the original octave counts so the benchmark baseline
        // reproduces the pre-optimisation shader cost exactly.
        settings.terrain_octaves = 5;
        settings.terrain_detail_octaves = 5;
        settings.lod_culling = false;
        settings.render_scale = 1.0f;
        return settings;
    }

    switch (tier) {
        case RenderQuality::Low:
            // Integrated and low-VRAM parts: skip MSAA bandwidth entirely and
            // lean on FXAA, which costs one fullscreen pass instead of
            // multiplying every scene sample.
            settings.msaa_samples = 1;
            settings.fxaa = true;
            settings.terrain_octaves = 3;
            settings.terrain_detail_octaves = 2;
            settings.render_scale = 1.0f;
            break;
        case RenderQuality::Medium:
            settings.msaa_samples = 2;
            settings.fxaa = false;
            settings.terrain_octaves = 4;
            settings.terrain_detail_octaves = 3;
            settings.render_scale = 1.0f;
            break;
        case RenderQuality::High:
            settings.msaa_samples = 4;
            settings.fxaa = false;
            // Four octaves read indistinguishable from five at strategy zoom
            // levels but cut the dominant fullscreen cost by ~20%.
            settings.terrain_octaves = 4;
            settings.terrain_detail_octaves = 2;
            settings.render_scale = 1.0f;
            break;
        case RenderQuality::Ultra:
            settings.msaa_samples = 8;
            settings.fxaa = false;
            settings.terrain_octaves = 5;
            settings.terrain_detail_octaves = 3;
            settings.render_scale = 1.0f;
            break;
        case RenderQuality::Legacy:
            return make_quality_settings(RenderQuality::Legacy, max_msaa);
    }

    // Everything above Legacy renders through the HDR target, which is what
    // makes dithering, FXAA and further post-processing possible at all.
    settings.hdr_target = true;
    settings.dither = true;
    settings.depth_buffer = true;
    settings.lod_culling = true;

    settings.msaa_samples = clamp_samples(settings.msaa_samples, max_msaa);
    return settings;
}

RenderQuality auto_quality_tier(const GpuTierInputs& gpu) noexcept {
    constexpr std::uint64_t gb = 1024ull * 1024ull * 1024ull;

    // Integrated or memory-constrained parts get the FXAA path; there is no
    // point paying MSAA resolve bandwidth on hardware that cannot afford it.
    if (!gpu.discrete_gpu) {
        return RenderQuality::Low;
    }
    if (gpu.device_local_bytes < 4ull * gb) {
        return gpu.supports_msaa4 ? RenderQuality::Medium : RenderQuality::Low;
    }
    if (gpu.device_local_bytes >= 8ull * gb) {
        return RenderQuality::Ultra;
    }
    return RenderQuality::High;
}

} // namespace thunder
