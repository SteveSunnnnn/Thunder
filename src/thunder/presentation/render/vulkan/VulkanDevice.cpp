#include "thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp"
#include "thunder/presentation/render/GpuCapabilities.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {
namespace {

void vkcheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
    }
}

bool has_device_extension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0;
    vkcheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
            "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> properties(count);
    vkcheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data()),
            "vkEnumerateDeviceExtensionProperties(data)");
    return std::any_of(properties.begin(), properties.end(), [&](const auto& property) {
        return std::string_view{property.extensionName} == name;
    });
}

} // namespace

void VulkanDesktopBackend::create_instance() {
    std::uint32_t count = 0;
    const char* const* required = SDL_Vulkan_GetInstanceExtensions(&count);
    if (required == nullptr || count == 0) {
        throw std::runtime_error("SDL returned no Vulkan instance extensions");
    }
    std::vector<const char*> extensions(required, required + count);
    if (validation_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Thunder Engine";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app.pEngineName = "Thunder";
    app.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    const char* layer = "VK_LAYER_KHRONOS_validation";
    if (validation_enabled_) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = &layer;
    }
    vkcheck(vkCreateInstance(&create_info, nullptr, &instance_), "vkCreateInstance");
}

void VulkanDesktopBackend::create_debug_messenger() {
    if (!validation_enabled_) {
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT create_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = &VulkanDesktopBackend::debug_callback;
    create_info.pUserData = this;
    const auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (create_debug == nullptr) {
        throw std::runtime_error("vkCreateDebugUtilsMessengerEXT unavailable");
    }
    vkcheck(create_debug(instance_, &create_info, nullptr, &debug_), "vkCreateDebugUtilsMessengerEXT");
}

void VulkanDesktopBackend::pick_device() {
    std::uint32_t count = 0;
    vkcheck(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (count == 0) {
        throw std::runtime_error("no Vulkan physical device");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkcheck(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices(data)");

    int best_score = -1;
    for (const auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3 ||
            !has_device_extension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        std::uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());
        std::optional<std::uint32_t> graphics;
        std::optional<std::uint32_t> present;
        for (std::uint32_t index = 0; index < queue_count; ++index) {
            if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && !graphics.has_value()) {
                graphics = index;
            }
            VkBool32 supported = VK_FALSE;
            vkcheck(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &supported),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (supported == VK_TRUE && !present.has_value()) {
                present = index;
            }
        }
        if (!graphics.has_value() || !present.has_value()) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        features11.pNext = &features12;
        features12.pNext = &features13;
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features2.pNext = &features11;
        vkGetPhysicalDeviceFeatures2(device, &features2);
        if (features13.dynamicRendering != VK_TRUE ||
            features13.synchronization2 != VK_TRUE ||
            features12.timelineSemaphore != VK_TRUE ||
            features12.bufferDeviceAddress != VK_TRUE ||
            features11.shaderDrawParameters != VK_TRUE) {
            continue;
        }

        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        std::uint64_t device_local = 0;
        for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
            if ((memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u) {
                device_local += memory.memoryHeaps[heap].size;
            }
        }

        GpuCapabilities caps{};
        caps.name = properties.deviceName;
        caps.type = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? GpuType::Discrete :
                    (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? GpuType::Integrated : GpuType::Other);
        caps.api_major = VK_API_VERSION_MAJOR(properties.apiVersion);
        caps.api_minor = VK_API_VERSION_MINOR(properties.apiVersion);
        caps.device_local_bytes = device_local;
        caps.dynamic_rendering = features13.dynamicRendering == VK_TRUE;
        caps.synchronization2 = features13.synchronization2 == VK_TRUE;
        caps.timeline_semaphore = features12.timelineSemaphore == VK_TRUE;
        caps.buffer_device_address = features12.bufferDeviceAddress == VK_TRUE;
        caps.descriptor_indexing = features12.descriptorIndexing == VK_TRUE;
        caps.draw_indirect_count = features12.drawIndirectCount == VK_TRUE;
        caps.shader_draw_parameters = features11.shaderDrawParameters == VK_TRUE;
        caps.sampler_anisotropy = features2.features.samplerAnisotropy == VK_TRUE;
        caps.max_sampled_images = properties.limits.maxDescriptorSetSampledImages;
        caps.max_image_dimension_2d = properties.limits.maxImageDimension2D;
        caps.timestamp_period_ns = properties.limits.timestampPeriod;

        const auto eval = evaluate_gpu(caps);
        if (!eval.compatible) {
            continue;
        }

        const int score = static_cast<int>(eval.score) + (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 10000 : 0);
        if (score > best_score) {
            best_score = score;
            physical_ = device;
            properties_ = properties;
            graphics_family_ = *graphics;
            present_family_ = *present;
            supports_dynamic_rendering_ = true;
            supports_synchronization2_ = true;
            supports_timeline_ = true;
            supports_bda_ = true;
            supports_descriptor_indexing_ = features12.descriptorIndexing == VK_TRUE;
            supports_draw_indirect_count_ = features12.drawIndirectCount == VK_TRUE;
            supports_shader_draw_parameters_ = true;
            device_local_bytes_ = device_local;
            max_sampled_images_ = properties.limits.maxDescriptorSetSampledImages;
            discrete_gpu_ = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            timestamp_period_ns_ = static_cast<double>(properties.limits.timestampPeriod);
        }
    }
    if (physical_ == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "no Thunder-compatible Vulkan 1.3 device "
            "(dynamicRendering+synchronization2+timelineSemaphore+bufferDeviceAddress+shaderDrawParameters+swapchain)");
    }
}

void VulkanDesktopBackend::create_device() {
    const float priority = 1.0f;
    const std::set<std::uint32_t> families{graphics_family_, present_family_};
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    for (const auto family : families) {
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        queue_infos.push_back(queue);
    }

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.pNext = &features13;
    features12.timelineSemaphore = supports_timeline_ ? VK_TRUE : VK_FALSE;
    features12.bufferDeviceAddress = supports_bda_ ? VK_TRUE : VK_FALSE;
    features12.descriptorIndexing = supports_descriptor_indexing_ ? VK_TRUE : VK_FALSE;
    features12.drawIndirectCount = supports_draw_indirect_count_ ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features11.pNext = &features12;
    features11.shaderDrawParameters = supports_shader_draw_parameters_ ? VK_TRUE : VK_FALSE;

    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create_info.pNext = &features11;
    create_info.queueCreateInfoCount = static_cast<std::uint32_t>(queue_infos.size());
    create_info.pQueueCreateInfos = queue_infos.data();
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = &extension;
    vkcheck(vkCreateDevice(physical_, &create_info, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
}

void VulkanDesktopBackend::create_swapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    vkcheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    // A newly-created/resized SDL window can report a zero surface extent for
    // a few event-loop turns (especially on high-DPI Windows desktops). Never
    // pass that transient state to Vulkan; wait briefly for a real drawable
    // surface instead of creating invalid zero-sized images.
    for (int attempt = 0; attempt < 120 &&
         (capabilities.maxImageExtent.width == 0u || capabilities.maxImageExtent.height == 0u); ++attempt) {
        SDL_PumpEvents();
        SDL_Delay(8);
        vkcheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR(wait)");
    }
    if (capabilities.maxImageExtent.width == 0u || capabilities.maxImageExtent.height == 0u)
        throw std::runtime_error("surface has no non-zero drawable extent");
    std::uint32_t format_count = 0;
    vkcheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    if (format_count == 0) {
        throw std::runtime_error("surface has no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkcheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(data)");
    auto format = formats.front();
    for (const auto& candidate : formats) {
        // Any SRGB swapchain format keeps shaders writing linear light correct;
        // accept both channel orders before falling back to a UNORM surface.
        if ((candidate.format == VK_FORMAT_B8G8R8A8_SRGB || candidate.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }
    swapchain_format_ = format.format;
    srgb_swapchain_ = swapchain_format_ == VK_FORMAT_B8G8R8A8_SRGB ||
                      swapchain_format_ == VK_FORMAT_R8G8B8A8_SRGB;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max() &&
        capabilities.currentExtent.width > 0 && capabilities.currentExtent.height > 0) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_.width = std::clamp(static_cast<std::uint32_t>(std::max(width, 1)),
                                   capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<std::uint32_t>(std::max(height, 1)),
                                    capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);
    }
    // The scene target follows the swapchain; a non-unit render scale is
    // re-applied here so a resize does not silently reset it to native.
    update_scene_extent();

    std::uint32_t image_count = std::max(capabilities.minImageCount + 1u, 3u);
    if (capabilities.maxImageCount > 0) {
        image_count = std::min(image_count, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = format.format;
    create_info.imageColorSpace = format.colorSpace;
    create_info.imageExtent = extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_transfer_source_ =
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    if (swapchain_transfer_source_) create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const std::uint32_t queue_families[]{graphics_family_, present_family_};
    if (graphics_family_ != present_family_) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_families;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Prefer MAILBOX (low latency, no tearing) when offered; FIFO is the
    // guaranteed fallback.
    create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::uint32_t present_mode_count = 0;
    vkcheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_mode_count, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkcheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_mode_count, present_modes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(data)");
    for (const auto candidate : present_modes) {
        if (candidate == VK_PRESENT_MODE_MAILBOX_KHR) {
            create_info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    create_info.clipped = VK_TRUE;
    vkcheck(vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_), "vkCreateSwapchainKHR");

    vkcheck(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr), "vkGetSwapchainImagesKHR(count)");
    images_.resize(image_count);
    vkcheck(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, images_.data()), "vkGetSwapchainImagesKHR(data)");
    views_.resize(images_.size());
    image_rendered_.resize(images_.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < images_.size(); ++index) {
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = images_[index];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        vkcheck(vkCreateImageView(device_, &view_info, nullptr, &views_[index]), "vkCreateImageView");

        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkcheck(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_rendered_[index]),
                "vkCreateSemaphore(image_rendered)");
    }
}

void VulkanDesktopBackend::destroy_swapchain() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (const auto semaphore : image_rendered_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    image_rendered_.clear();
    for (const auto view : views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    views_.clear();
    images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanDesktopBackend::create_sync() {
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = graphics_family_;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkcheck(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");

    std::array<VkCommandBuffer, frames_in_flight> commands{};
    VkCommandBufferAllocateInfo allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate_info.commandPool = command_pool_;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = frames_in_flight;
    vkcheck(vkAllocateCommandBuffers(device_, &allocate_info, commands.data()), "vkAllocateCommandBuffers");

    for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
        frames_[index].command = commands[index];
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkcheck(vkCreateSemaphore(device_, &semaphore_info, nullptr, &frames_[index].acquired),
                "vkCreateSemaphore(acquired)");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkcheck(vkCreateFence(device_, &fence_info, nullptr, &frames_[index].fence), "vkCreateFence");
    }
}

void VulkanDesktopBackend::destroy_sync() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (auto& frame : frames_) {
        if (frame.fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frame.fence, nullptr);
            frame.fence = VK_NULL_HANDLE;
        }
        if (frame.acquired != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, frame.acquired, nullptr);
            frame.acquired = VK_NULL_HANDLE;
        }
        frame.command = VK_NULL_HANDLE;
    }
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}



} // namespace thunder
