#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"

#include "thunder/presentation/ui/FontAtlas.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace thunder {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " + std::to_string(static_cast<int>(result)));
    }
}

bool has_layer(const char* name) {
    std::uint32_t count = 0;
    vkcheck(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties(count)");
    std::vector<VkLayerProperties> properties(count);
    vkcheck(vkEnumerateInstanceLayerProperties(&count, properties.data()), "vkEnumerateInstanceLayerProperties(data)");
    return std::any_of(properties.begin(), properties.end(), [&](const auto& property) {
        return std::string_view{property.layerName} == name;
    });
}

} // namespace

VulkanDesktopBackend::VulkanDesktopBackend() = default;

VulkanDesktopBackend::~VulkanDesktopBackend() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        destroy_runtime_renderer();
        destroy_mapped_host_buffer(screenshot_buffer_.buffer,
                                   screenshot_buffer_.memory,
                                   screenshot_buffer_.mapped);
        screenshot_buffer_.capacity = 0;
        destroy_sync();
        destroy_swapchain();
        // Persisted here so the next run starts with warm pipelines.
        destroy_pipeline_cache();
        destroy_query_pools();
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (debug_ != VK_NULL_HANDLE) {
        const auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_debug != nullptr) {
            destroy_debug(instance_, debug_, nullptr);
        }
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDesktopBackend::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* user) {
    auto* self = static_cast<VulkanDesktopBackend*>(user);
    const char* message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u) {
        self->validation_errors_.fetch_add(1u, std::memory_order_relaxed);
        std::cerr << "VULKAN_VALIDATION_ERROR: " << message << '\n';
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0u) {
        self->validation_warnings_.fetch_add(1u, std::memory_order_relaxed);
        std::cerr << "VULKAN_VALIDATION_WARNING: " << message << '\n';
    }
    return VK_FALSE;
}

void VulkanDesktopBackend::initialize(SDL_Window* window, bool enable_validation) {
    window_ = window;
    validation_enabled_ = enable_validation;
    if (validation_enabled_ && !has_layer("VK_LAYER_KHRONOS_validation")) {
        throw std::runtime_error("VK_LAYER_KHRONOS_validation requested but unavailable");
    }
    if (const char* shader_dir = std::getenv("THUNDER_SHADER_DIR")) {
        shader_dir_ = std::filesystem::path{shader_dir};
    }
    create_instance();
    create_debug_messenger();
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    pick_device();
    // Must follow pick_device(): the tier is resolved against the limits of
    // the device that was actually chosen.
    resolve_quality_tier();
    create_device();
    create_pipeline_cache();
    create_swapchain();
    create_sync();
    create_query_pools();
    if (!shader_dir_.empty()) {
        create_runtime_renderer();
    }
}

std::uint32_t VulkanDesktopBackend::find_memory_type(std::uint32_t bits, VkMemoryPropertyFlags required) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        const bool supported = (bits & (1u << index)) != 0u;
        const bool has_flags = (memory.memoryTypes[index].propertyFlags & required) == required;
        if (supported && has_flags) {
            return index;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type");
}

void VulkanDesktopBackend::create_host_buffer(VkDeviceSize size,
                                               VkBufferUsageFlags usage,
                                               const void* data,
                                               VkBuffer& buffer,
                                               VkDeviceMemory& memory) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkcheck(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = find_memory_type(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkcheck(vkAllocateMemory(device_, &allocate_info, nullptr, &memory), "vkAllocateMemory");
    vkcheck(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory");

    void* mapped = nullptr;
    vkcheck(vkMapMemory(device_, memory, 0, size, 0, &mapped), "vkMapMemory");
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(device_, memory);
}

VkShaderModule VulkanDesktopBackend::load_shader_module(const std::filesystem::path& path) const {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open SPIR-V shader: " + path.string());
    }
    const std::streamoff end = stream.tellg();
    if (end <= 0 || (static_cast<std::uint64_t>(end) % sizeof(std::uint32_t)) != 0u) {
        throw std::runtime_error("invalid SPIR-V byte size: " + path.string());
    }
    const auto byte_size = static_cast<std::size_t>(end);
    std::vector<std::uint32_t> words(byte_size / sizeof(std::uint32_t));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(byte_size));
    if (!stream) {
        throw std::runtime_error("failed reading SPIR-V shader: " + path.string());
    }
    VkShaderModuleCreateInfo create_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create_info.codeSize = byte_size;
    create_info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkcheck(vkCreateShaderModule(device_, &create_info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

// ---------------------------------------------------------------------------
// Quality tiers
// ---------------------------------------------------------------------------

void VulkanDesktopBackend::resolve_quality_tier() {
    GpuTierInputs gpu{};
    gpu.device_local_bytes = device_local_bytes_;
    gpu.discrete_gpu = discrete_gpu_;
    gpu.color_sample_counts = properties_.limits.framebufferColorSampleCounts;
    gpu.supports_msaa4 = (properties_.limits.framebufferColorSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0u;

    active_quality_ = auto_quality_tier(gpu);
    settings_ = make_quality_settings(active_quality_, properties_.limits.framebufferColorSampleCounts);

    // An explicit request always wins over the auto-detected default.
    if (requested_quality_ != active_quality_) {
        active_quality_ = requested_quality_;
        settings_ = make_quality_settings(active_quality_, properties_.limits.framebufferColorSampleCounts);
    }

    msaa_samples_ = static_cast<VkSampleCountFlagBits>(settings_.msaa_samples);

    // The tier table always yields a scale of 1.0; the caller's request is the
    // override, so re-apply it and let update_scene_extent() clamp.
    settings_.render_scale = requested_render_scale_;
}

void VulkanDesktopBackend::update_scene_extent() noexcept {
    // Without an offscreen target the scene pass writes the swapchain image
    // directly, so a sub-native scene extent would shrink the viewport into a
    // corner of the screen instead of upscaling. Force parity on that path.
    if (!settings_.hdr_target || settings_.render_scale >= 1.0f) {
        scene_extent_ = extent_;
        return;
    }
    const float scale = std::clamp(settings_.render_scale, 0.25f, 1.0f);
    scene_extent_.width = std::max(1u, static_cast<std::uint32_t>(static_cast<float>(extent_.width) * scale));
    scene_extent_.height = std::max(1u, static_cast<std::uint32_t>(static_cast<float>(extent_.height) * scale));
}

void VulkanDesktopBackend::set_shading_debug(std::uint32_t flags) {
    // Full u32: the float push constant represents any single power-of-two bit
    // exactly (only sums above 2^24 lose their low mantissa bits).
    shading_debug_flags_ = static_cast<float>(flags);
}

void VulkanDesktopBackend::set_render_scale(float scale) {
    const float clamped = std::clamp(scale, 0.25f, 1.0f);
    requested_render_scale_ = clamped;
    settings_.render_scale = clamped;

    // Before initialize() there is nothing to rebuild; the value is picked up
    // when the swapchain and scene targets are first created.
    if (device_ == VK_NULL_HANDLE) return;

    update_scene_extent();
    if (scene_extent_.width == extent_.width && scene_extent_.height == extent_.height &&
        !settings_.hdr_target) {
        return;
    }
    // Only the size-dependent targets are rebuilt. Every pipeline stays valid
    // because no format or sample count changed, so this is a cheap realloc
    // rather than a SPIR-V recompilation.
    vkcheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(set_render_scale)");
    destroy_scene_targets();
    create_scene_targets();
}

VkFormat VulkanDesktopBackend::pick_depth_format() const {
    // Prefer 32-bit float depth: the strategic camera spans a very large world,
    // and 24-bit integer depth visibly z-fights at far zoom levels.
    for (const VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                     VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(physical_, candidate, &format_properties);
        if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u) {
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

// ---------------------------------------------------------------------------
// Pipeline cache
// ---------------------------------------------------------------------------

void VulkanDesktopBackend::create_pipeline_cache() {
    // Reusing a persisted cache turns swapchain resizes from "recompile every
    // SPIR-V module" into a near-no-op. The cache is keyed on the driver, so a
    // stale file is simply ignored rather than corrupting pipelines.
    std::vector<char> initial;
    const std::filesystem::path cache_path = shader_dir_ / "thunder_pipeline.cache";
    if (std::ifstream stream{cache_path, std::ios::binary}) {
        stream.seekg(0, std::ios::end);
        const auto size = static_cast<std::size_t>(stream.tellg());
        if (size > 0) {
            stream.seekg(0, std::ios::beg);
            initial.resize(size);
            stream.read(initial.data(), static_cast<std::streamsize>(size));
        }
    }
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = initial.size();
    info.pInitialData = initial.empty() ? nullptr : initial.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &pipeline_cache_) != VK_SUCCESS) {
        // A failed cache must never prevent startup; fall back to no cache.
        pipeline_cache_ = VK_NULL_HANDLE;
        return;
    }
    pipeline_cache_path_ = cache_path;
}

void VulkanDesktopBackend::destroy_pipeline_cache() noexcept {
    if (pipeline_cache_ == VK_NULL_HANDLE) {
        return;
    }
    // Persist so the next run starts warm.
    std::size_t size = 0;
    if (vkGetPipelineCacheData(device_, pipeline_cache_, &size, nullptr) == VK_SUCCESS && size > 0) {
        std::vector<char> data(size);
        if (vkGetPipelineCacheData(device_, pipeline_cache_, &size, data.data()) == VK_SUCCESS) {
            if (std::ofstream stream{pipeline_cache_path_, std::ios::binary | std::ios::trunc}) {
                stream.write(data.data(), static_cast<std::streamsize>(size));
            }
        }
    }
    vkDestroyPipelineCache(device_, pipeline_cache_, nullptr);
    pipeline_cache_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// GPU timing
// ---------------------------------------------------------------------------

void VulkanDesktopBackend::create_query_pools() {
    // Timestamp queries require the queue to actually support them. Without
    // this the engine still runs; it just reports no GPU-side numbers.
    timing_supported_ = properties_.limits.timestampComputeAndGraphics == VK_TRUE;
    if (!timing_supported_) {
        return;
    }
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = 2; // start + end
    for (auto& pool : query_pools_) {
        if (vkCreateQueryPool(device_, &info, nullptr, &pool) != VK_SUCCESS) {
            timing_supported_ = false;
            return;
        }
    }

    // Queries start out uninitialised and resetting them from the host needs
    // the hostQueryReset feature, which would narrow device support for no
    // benefit. Instead each pool is reset inside the command buffer that writes
    // it, and results are only read back once that pool has actually been used.
}

void VulkanDesktopBackend::destroy_query_pools() noexcept {
    for (auto& pool : query_pools_) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_, pool, nullptr);
            pool = VK_NULL_HANDLE;
        }
    }
}

void VulkanDesktopBackend::reset_query_pool(VkCommandBuffer command, std::uint32_t frame) const {
    if (!timing_supported_ || query_pools_[frame] == VK_NULL_HANDLE) {
        return;
    }
    vkCmdResetQueryPool(command, query_pools_[frame], 0, 2);
}

void VulkanDesktopBackend::write_gpu_timestamp(VkCommandBuffer command,
                                               std::uint32_t frame,
                                               bool start) const {
    if (!timing_supported_ || query_pools_[frame] == VK_NULL_HANDLE) {
        return;
    }
    // vkCmdWriteTimestamp takes the pre-synchronization2 stage enum.
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        query_pools_[frame], start ? 0u : 1u);
    if (!start) {
        query_written_[frame] = true;
    }
}

void VulkanDesktopBackend::collect_gpu_timing(std::uint32_t frame) {
    // Reading a pool that has never been written returns uninitialised data.
    if (!timing_supported_ || query_pools_[frame] == VK_NULL_HANDLE || !query_written_[frame]) {
        return;
    }
    std::uint64_t timestamps[2] = {0, 0};
    const VkResult result = vkGetQueryPoolResults(
        device_, query_pools_[frame], 0, 2, sizeof(timestamps), timestamps, sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS) {
        return;
    }
    if (timestamps[1] <= timestamps[0]) {
        return;
    }
    const double delta_ns = static_cast<double>(timestamps[1] - timestamps[0]) * timestamp_period_ns_;
    last_gpu_ms_ = delta_ns / 1'000'000.0;
}

void VulkanDesktopBackend::reset_stats() noexcept {
    stats_ = RenderStats{};
    frame_samples_.clear();
}

void VulkanDesktopBackend::update_stats(double frame_ms, double gpu_ms, double cpu_ms, double submission_ms) {
    // Skip the first few frames: they include pipeline warm-up and shader
    // specialisation that would otherwise skew the average badly.
    constexpr std::uint64_t warmup_frames = 30;
    if (stats_.sampled_frames < warmup_frames) {
        ++stats_.sampled_frames;
        return;
    }
    frame_samples_.push_back(frame_ms);
    stats_.sampled_frames += 1;
    stats_.avg_gpu_ms += (gpu_ms - stats_.avg_gpu_ms) /
                         static_cast<double>(frame_samples_.size());
    stats_.avg_cpu_ms += (cpu_ms - stats_.avg_cpu_ms) /
                         static_cast<double>(frame_samples_.size());
    stats_.avg_cpu_submission_ms += (submission_ms - stats_.avg_cpu_submission_ms) /
                                    static_cast<double>(frame_samples_.size());

    double total = 0.0;
    double minimum = frame_samples_.front();
    double maximum = frame_samples_.front();
    for (const double sample : frame_samples_) {
        total += sample;
        minimum = std::min(minimum, sample);
        maximum = std::max(maximum, sample);
    }
    stats_.avg_frame_ms = total / static_cast<double>(frame_samples_.size());
    stats_.min_frame_ms = minimum;
    stats_.max_frame_ms = maximum;
    stats_.avg_frame_ms = std::max(stats_.avg_frame_ms, 1e-6);
    stats_.fps = 1000.0 / stats_.avg_frame_ms;

    // p95 is the number that actually matters for perceived smoothness.
    if (frame_samples_.size() >= 2) {
        std::vector<double> sorted(frame_samples_.begin(), frame_samples_.end());
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() * 95 / 100),
                         sorted.end());
        stats_.p95_frame_ms = sorted[sorted.size() * 95 / 100];
    }
}

// ---------------------------------------------------------------------------
// Offscreen targets
// ---------------------------------------------------------------------------

void VulkanDesktopBackend::create_image_2d(std::uint32_t width, std::uint32_t height, VkFormat format,
                                           VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                           std::uint32_t samples, VkImage& image,
                                           VkDeviceMemory& memory, VkImageView& view) {
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = static_cast<VkSampleCountFlagBits>(samples);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkcheck(vkCreateImage(device_, &image_info, nullptr, &image), "vkCreateImage(offscreen)");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkcheck(vkAllocateMemory(device_, &allocate, nullptr, &memory), "vkAllocateMemory(offscreen)");
    vkcheck(vkBindImageMemory(device_, image, memory, 0), "vkBindImageMemory(offscreen)");

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    vkcheck(vkCreateImageView(device_, &view_info, nullptr, &view), "vkCreateImageView(offscreen)");
}

void VulkanDesktopBackend::destroy_image_2d(VkImage& image, VkDeviceMemory& memory, VkImageView& view) noexcept {
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void VulkanDesktopBackend::create_hdr_targets() {
    const std::uint32_t width = scene_extent_.width;
    const std::uint32_t height = scene_extent_.height;
    const std::uint32_t samples = settings_.msaa_samples;

    hdr_format_ = VK_FORMAT_R16G16B16A16_SFLOAT;

    if (samples > 1u) {
        // Multisampled colour plus a single-sample resolve the tonemap pass
        // samples. TRANSIENT_ATTACHMENT tells the driver the MSAA buffer never
        // needs to round-trip to memory on a tiler.
        create_image_2d(width, height, hdr_format_,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, samples,
                        hdr_image_, hdr_image_memory_, hdr_view_);
        create_image_2d(width, height, hdr_format_,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                        hdr_msaa_resolve_image_, hdr_msaa_resolve_memory_, hdr_msaa_resolve_view_);
    } else {
        // Single-sampled path: one image serves as both attachment and source.
        create_image_2d(width, height, hdr_format_,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                        hdr_image_, hdr_image_memory_, hdr_view_);
    }

    // hdr_sampler_ is created once in create_runtime_renderer and
    // outlives the targets, which are recreated on every resize.
    const VkImageView sampled_view = (samples > 1u) ? hdr_msaa_resolve_view_ : hdr_view_;
    VkDescriptorImageInfo image_info{};
    image_info.sampler = hdr_sampler_;
    image_info.imageView = sampled_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = hdr_descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    // FXAA runs on tonemapped LDR pixels, so it needs its own intermediate
    // target: it cannot sample the swapchain image it is writing to.
    if (settings_.fxaa) {
        // FXAA runs on tonemapped LDR pixels at display resolution, so this
        // target tracks the swapchain rather than the (possibly upscaled)
        // scene resolution.
        create_image_2d(extent_.width, extent_.height, swapchain_format_,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1u,
                        fxaa_image_, fxaa_memory_, fxaa_view_);

        VkSamplerCreateInfo fxaa_sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        fxaa_sampler_info.magFilter = VK_FILTER_LINEAR;
        fxaa_sampler_info.minFilter = VK_FILTER_LINEAR;
        fxaa_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        fxaa_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        fxaa_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkcheck(vkCreateSampler(device_, &fxaa_sampler_info, nullptr, &fxaa_sampler_),
                "vkCreateSampler(fxaa)");

        // Same single-binding layout as the HDR set; only the image differs.
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        vkcheck(vkCreateDescriptorPool(device_, &pool_info, nullptr, &fxaa_descriptor_pool_),
                "vkCreateDescriptorPool(fxaa)");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = fxaa_descriptor_pool_;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &hdr_descriptor_layout_;
        vkcheck(vkAllocateDescriptorSets(device_, &allocate, &fxaa_descriptor_set_),
                "vkAllocateDescriptorSets(fxaa)");

        VkDescriptorImageInfo fxaa_image_info{};
        fxaa_image_info.sampler = fxaa_sampler_;
        fxaa_image_info.imageView = fxaa_view_;
        fxaa_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet fxaa_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        fxaa_write.dstSet = fxaa_descriptor_set_;
        fxaa_write.dstBinding = 0;
        fxaa_write.descriptorCount = 1;
        fxaa_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        fxaa_write.pImageInfo = &fxaa_image_info;
        vkUpdateDescriptorSets(device_, 1, &fxaa_write, 0, nullptr);
    }

    hdr_targets_created_ = true;
}

void VulkanDesktopBackend::destroy_hdr_targets() {
    if (fxaa_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, fxaa_descriptor_pool_, nullptr);
        fxaa_descriptor_pool_ = VK_NULL_HANDLE;
        fxaa_descriptor_set_ = VK_NULL_HANDLE;
    }
    if (fxaa_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, fxaa_sampler_, nullptr);
        fxaa_sampler_ = VK_NULL_HANDLE;
    }
    destroy_image_2d(hdr_image_, hdr_image_memory_, hdr_view_);
    destroy_image_2d(hdr_msaa_resolve_image_, hdr_msaa_resolve_memory_, hdr_msaa_resolve_view_);
    destroy_image_2d(fxaa_image_, fxaa_memory_, fxaa_view_);
    hdr_targets_created_ = false;
}

void VulkanDesktopBackend::create_depth_target() {
    depth_format_ = pick_depth_format();
    if (depth_format_ == VK_FORMAT_UNDEFINED) {
        return;
    }
    create_image_2d(scene_extent_.width, scene_extent_.height, depth_format_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, settings_.msaa_samples,
                    depth_image_, depth_memory_, depth_view_);
}

void VulkanDesktopBackend::destroy_depth_target() {
    destroy_image_2d(depth_image_, depth_memory_, depth_view_);
    depth_format_ = VK_FORMAT_UNDEFINED;
}

void VulkanDesktopBackend::create_scene_targets() {
    if (settings_.hdr_target) {
        create_hdr_targets();
    }
    if (settings_.depth_buffer) {
        create_depth_target();
    }
}

void VulkanDesktopBackend::destroy_scene_targets() {
    if (hdr_targets_created_) {
        destroy_hdr_targets();
    }
    destroy_depth_target();
}

void VulkanDesktopBackend::create_mapped_host_buffer(VkDeviceSize size,
                                                      VkBufferUsageFlags usage,
                                                      VkBuffer& buffer,
                                                      VkDeviceMemory& memory,
                                                      void*& mapped) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkcheck(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer), "vkCreateBuffer(world upload)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = find_memory_type(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    try {
        vkcheck(vkAllocateMemory(device_, &allocate_info, nullptr, &memory),
                "vkAllocateMemory(world upload)");
        vkcheck(vkBindBufferMemory(device_, buffer, memory, 0),
                "vkBindBufferMemory(world upload)");
        vkcheck(vkMapMemory(device_, memory, 0, requirements.size, 0, &mapped),
                "vkMapMemory(world upload)");
    } catch (...) {
        if (mapped != nullptr && memory != VK_NULL_HANDLE) vkUnmapMemory(device_, memory);
        mapped = nullptr;
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        throw;
    }
}

void VulkanDesktopBackend::destroy_mapped_host_buffer(VkBuffer& buffer,
                                                       VkDeviceMemory& memory,
                                                       void*& mapped) noexcept {
    if (device_ == VK_NULL_HANDLE) {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        mapped = nullptr;
        return;
    }
    if (mapped != nullptr && memory != VK_NULL_HANDLE) vkUnmapMemory(device_, memory);
    mapped = nullptr;
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device_, memory, nullptr);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}

void VulkanDesktopBackend::record_ui_fallback(VkCommandBuffer command) const {
    if (ui_vertices_ == VK_NULL_HANDLE || ui_pipeline_ == VK_NULL_HANDLE) {
        return;
    }
    const VkDeviceSize ui_offset = 0;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
    vkCmdBindVertexBuffers(command, 0, 1, &ui_vertices_, &ui_offset);
    const std::array<float, 2> inv_viewport{{
        1.0f / static_cast<float>(std::max(extent_.width, 1u)),
        1.0f / static_cast<float>(std::max(extent_.height, 1u))}};
    vkCmdPushConstants(command, ui_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(inv_viewport), inv_viewport.data());
    vkCmdDraw(command, 6, 1, 0, 0);
    ++draw_calls_;
}

void VulkanDesktopBackend::write_report(const std::filesystem::path& path) const {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot create GPU report");
    }
    output << "thunder_gpu_validation=1\n"
           << "device=" << properties_.deviceName << '\n'
           << "vendor_id=" << properties_.vendorID << '\n'
           << "device_id=" << properties_.deviceID << '\n'
           << "api_version=" << VK_API_VERSION_MAJOR(properties_.apiVersion) << '.'
           << VK_API_VERSION_MINOR(properties_.apiVersion) << '.'
           << VK_API_VERSION_PATCH(properties_.apiVersion) << '\n'
           << "driver_version=" << properties_.driverVersion << '\n'
           << "frames_presented=" << frames_presented_ << '\n'
           << "validation_enabled=" << (validation_enabled_ ? 1 : 0) << '\n'
           << "validation_errors=" << validation_errors() << '\n'
           << "validation_warnings=" << validation_warnings() << '\n'
           << "dynamic_rendering=" << (supports_dynamic_rendering_ ? 1 : 0) << '\n'
           << "synchronization2=" << (supports_synchronization2_ ? 1 : 0) << '\n'
           << "timeline_semaphore=" << (supports_timeline_ ? 1 : 0) << '\n'
           << "buffer_device_address=" << (supports_bda_ ? 1 : 0) << '\n'
           << "descriptor_indexing=" << (supports_descriptor_indexing_ ? 1 : 0) << '\n'
           << "draw_indirect_count=" << (supports_draw_indirect_count_ ? 1 : 0) << '\n'
           << "shader_draw_parameters=" << (supports_shader_draw_parameters_ ? 1 : 0) << '\n'
           << "device_local_bytes=" << device_local_bytes_ << '\n'
           << "max_sampled_images=" << max_sampled_images_ << '\n'
           << "swapchain_format=" << static_cast<std::uint32_t>(swapchain_format_) << '\n'
           << "swapchain_width=" << extent_.width << '\n'
           << "swapchain_height=" << extent_.height << '\n'
           << "runtime_renderer_enabled=" << (runtime_renderer_enabled_ ? 1 : 0) << '\n'
           << "quality_tier=" << quality_tier_name(active_quality_) << '\n'
           << "msaa_samples=" << settings_.msaa_samples << '\n'
           << "fxaa=" << (settings_.fxaa ? 1 : 0) << '\n'
           << "dither=" << (settings_.dither ? 1 : 0) << '\n'
           << "depth_buffer=" << (settings_.depth_buffer ? 1 : 0) << '\n'
           << "hdr_target=" << (settings_.hdr_target ? 1 : 0) << '\n'
           << "terrain_octaves=" << settings_.terrain_octaves << '\n'
           << "terrain_detail_octaves=" << settings_.terrain_detail_octaves << '\n'
           << "pipeline_cache=" << (pipeline_cache_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
           << "world_pack_ready=" << (world_pack_ready_ ? 1 : 0) << '\n'
           << "world_pack_hash=0x" << std::hex
           << (world_page_streamer_ ? world_page_streamer_->stats().build_hash : 0u) << std::dec << '\n'
           << "world_horizontal_wrap=" << (world_page_streamer_ && world_page_streamer_->metadata().horizontal_wrap ? 1 : 0) << '\n'
           << "world_clip_levels=" << (world_page_streamer_ ? world_page_streamer_->metadata().clip_levels : 0u) << '\n'
           << "world_resident_pages=" << (world_page_streamer_ ? world_page_streamer_->resident_count() : 0u) << '\n'
           << "world_remaining_pages=" << (world_page_streamer_ ? world_page_streamer_->remaining() : 0u) << '\n'
           << "timing_supported=" << (timing_supported_ ? 1 : 0) << '\n'
           << "sampled_frames=" << stats_.sampled_frames << '\n'
           << "avg_frame_ms=" << stats_.avg_frame_ms << '\n'
           << "min_frame_ms=" << stats_.min_frame_ms << '\n'
           << "max_frame_ms=" << stats_.max_frame_ms << '\n'
           << "p95_frame_ms=" << stats_.p95_frame_ms << '\n'
           << "avg_gpu_ms=" << stats_.avg_gpu_ms << '\n'
           << "avg_cpu_ms=" << stats_.avg_cpu_ms << '\n'
           << "fps=" << stats_.fps << '\n'
            << "draw_calls_last_frame=" << stats_.draw_calls_last_frame << '\n'
            << "world_page_pipeline=" << (world_map_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
            << "living_pipeline=" << (living_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
            << "ui_pipeline=" << (ui_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\n'
            << "wired_terrain_clipmap=" << (wired_terrain_initialized_ ? 1 : 0) << '\n'
            << "wired_terrain_clipmap_patches=" << terrain_clipmap_.max_patch_count() << '\n'
            << "wired_bindless_materials=" << terrain_bindless_.material_count() << '\n'
            << "wired_bindless_textures=" << terrain_bindless_.texture_slot_count() << '\n'
            << "wired_sky_luma=" << (wired_sky_output_.sky_horizon_color.x + wired_sky_output_.sky_horizon_color.y + wired_sky_output_.sky_horizon_color.z) / 3.0f << '\n'
            << "wired_cloud_shadow_sample=" << wired_cloud_uniforms_.shadow_intensity << '\n';
}

} // namespace thunder
