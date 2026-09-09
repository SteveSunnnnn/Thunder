#pragma once

#include <cstdint>
#include <string>

namespace thunder {

struct VulkanProbeResult {
    bool loader_found = false;
    bool instance_created = false;
    std::uint32_t loader_api_version = 0;
    std::uint32_t physical_device_count = 0;
    std::int32_t create_instance_result = 0;
    std::string loader_name;
    std::string message;

    [[nodiscard]] std::uint32_t api_major() const noexcept { return loader_api_version >> 22u; }
    [[nodiscard]] std::uint32_t api_minor() const noexcept { return (loader_api_version >> 12u) & 0x3ffu; }
    [[nodiscard]] std::uint32_t api_patch() const noexcept { return loader_api_version & 0xfffu; }
};

// Loader-only smoke probe with no SDK/header dependency. It verifies that Thunder can
// dynamically reach Vulkan and create an instance in CI/headless environments.
[[nodiscard]] VulkanProbeResult probe_vulkan_loader() noexcept;

} // namespace thunder
