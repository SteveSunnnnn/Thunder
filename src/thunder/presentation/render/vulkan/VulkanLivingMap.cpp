#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace thunder {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
    }
}

} // namespace

void VulkanDesktopBackend::ensure_living_frame_buffers(std::size_t required_bytes) {
    if (device_ == VK_NULL_HANDLE) return;
    const auto required = static_cast<VkDeviceSize>(std::max<std::size_t>(
        required_bytes, sizeof(std::array<float, 16>)));
    bool resize = false;
    for (const auto& frame : living_frame_buffers_)
        resize = resize || frame.capacity < required || frame.buffer == VK_NULL_HANDLE || frame.mapped == nullptr;
    if (resize) vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(living resize)");
    for (auto& frame : living_frame_buffers_) {
        if (frame.capacity >= required && frame.buffer != VK_NULL_HANDLE && frame.mapped != nullptr) continue;
        destroy_mapped_host_buffer(frame.buffer, frame.memory, frame.mapped);
        frame.capacity = required;
        create_mapped_host_buffer(frame.capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  frame.buffer, frame.memory, frame.mapped);
    }
}

void VulkanDesktopBackend::submit_living_instances(
    std::span<const std::array<float, 16>> transforms) {
    living_instance_count_ = static_cast<std::uint32_t>(std::min<std::size_t>(
        transforms.size(), std::numeric_limits<std::uint32_t>::max()));
    if (device_ == VK_NULL_HANDLE || living_instance_count_ == 0u) return;
    const auto bytes = static_cast<std::size_t>(living_instance_count_) * sizeof(std::array<float, 16>);
    ensure_living_frame_buffers(bytes);
    if (frames_[frame_index_].fence != VK_NULL_HANDLE)
        vkcheck(vkWaitForFences(device_, 1, &frames_[frame_index_].fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences(living upload)");
    std::memcpy(living_frame_buffers_[frame_index_].mapped, transforms.data(), bytes);
}

} // namespace thunder
