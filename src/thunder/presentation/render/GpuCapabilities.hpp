#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace thunder {

enum class GpuType : std::uint8_t {
    Other,
    Integrated,
    Discrete,
    Virtual
};

enum class GpuTier : std::uint8_t {
    Unsupported,
    Compatibility,
    Recommended,
    HighEnd
};

struct GpuCapabilities {
    std::string name;
    GpuType type = GpuType::Other;
    std::uint32_t api_major = 1;
    std::uint32_t api_minor = 0;
    std::uint64_t device_local_bytes = 0;

    bool dynamic_rendering = false;
    bool synchronization2 = false;
    bool timeline_semaphore = false;
    bool buffer_device_address = false;
    bool descriptor_indexing = false;
    bool draw_indirect_count = false;
    bool shader_draw_parameters = false;
    bool sampler_anisotropy = false;
    bool storage_buffer_16bit = false;
    bool dedicated_compute_queue = false;
    bool dedicated_transfer_queue = false;

    std::uint32_t max_sampled_images = 0;
    std::uint32_t max_image_dimension_2d = 0;
    float timestamp_period_ns = 0.0f;
};

struct DeviceSelectionResult {
    bool compatible = false;
    GpuTier tier = GpuTier::Unsupported;
    std::int64_t score = 0;
    std::vector<std::string> missing_required;
};

[[nodiscard]] DeviceSelectionResult evaluate_gpu(const GpuCapabilities& gpu);

} // namespace thunder
