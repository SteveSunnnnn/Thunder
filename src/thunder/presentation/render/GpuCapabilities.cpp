#include "thunder/presentation/render/GpuCapabilities.hpp"

#include <algorithm>

namespace thunder {

namespace {

constexpr std::uint64_t gib = 1024ull * 1024ull * 1024ull;

bool at_least_vulkan_13(const GpuCapabilities& gpu) noexcept {
    return gpu.api_major > 1u || (gpu.api_major == 1u && gpu.api_minor >= 3u);
}

bool at_least_vulkan_14(const GpuCapabilities& gpu) noexcept {
    return gpu.api_major > 1u || (gpu.api_major == 1u && gpu.api_minor >= 4u);
}

void require(bool condition, const char* name, DeviceSelectionResult& out) {
    if (!condition) {
        out.missing_required.emplace_back(name);
    }
}

} // namespace

DeviceSelectionResult evaluate_gpu(const GpuCapabilities& gpu) {
    DeviceSelectionResult out;
    require(at_least_vulkan_13(gpu), "Vulkan 1.3", out);
    require(gpu.dynamic_rendering, "dynamic rendering", out);
    require(gpu.synchronization2, "synchronization2", out);
    require(gpu.timeline_semaphore, "timeline semaphore", out);
    require(gpu.buffer_device_address, "buffer device address", out);
    require(gpu.shader_draw_parameters, "shader draw parameters", out);

    if (!out.missing_required.empty()) {
        return out;
    }

    out.compatible = true;
    out.tier = GpuTier::Compatibility;
    out.score = 1'000;

    if (gpu.type == GpuType::Discrete) out.score += 500;
    if (gpu.type == GpuType::Integrated) out.score += 100;
    if (at_least_vulkan_14(gpu)) out.score += 150;
    if (gpu.descriptor_indexing) out.score += 250;
    if (gpu.draw_indirect_count) out.score += 200;
    if (gpu.sampler_anisotropy) out.score += 50;
    if (gpu.storage_buffer_16bit) out.score += 35;
    if (gpu.dedicated_compute_queue) out.score += 80;
    if (gpu.dedicated_transfer_queue) out.score += 60;

    const auto vram_gib = static_cast<std::int64_t>(gpu.device_local_bytes / gib);
    out.score += std::clamp<std::int64_t>(vram_gib * 45, 0, 720);

    if (gpu.descriptor_indexing && gpu.draw_indirect_count && gpu.max_sampled_images >= 4096u &&
        gpu.device_local_bytes >= 4ull * gib) {
        out.tier = GpuTier::Recommended;
    }
    if (out.tier == GpuTier::Recommended && gpu.device_local_bytes >= 8ull * gib &&
        // Was `gpu.api_minor >= 4u` alone, which is also true for a hypothetical
        // Vulkan 2.0 device reporting minor 0 — test the major version too.
        gpu.dedicated_compute_queue && gpu.dedicated_transfer_queue && at_least_vulkan_14(gpu)) {
        out.tier = GpuTier::HighEnd;
    }

    return out;
}

} // namespace thunder
