#include "thunder/presentation/render/vulkan/VulkanProbe.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace thunder {
namespace {

using VkFlags = std::uint32_t;
using VkResult = std::int32_t;
using VkStructureType = std::int32_t;
struct VkInstance_T;
struct VkPhysicalDevice_T;
using VkInstance = VkInstance_T*;
using VkPhysicalDevice = VkPhysicalDevice_T*;

constexpr VkResult VK_SUCCESS = 0;
constexpr VkStructureType VK_STRUCTURE_TYPE_APPLICATION_INFO = 0;
constexpr VkStructureType VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1;
constexpr std::uint32_t make_api_version(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) noexcept {
    return (major << 22u) | (minor << 12u) | patch;
}

struct VkApplicationInfo {
    VkStructureType sType;
    const void* pNext;
    const char* pApplicationName;
    std::uint32_t applicationVersion;
    const char* pEngineName;
    std::uint32_t engineVersion;
    std::uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    std::uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};

using PFN_vkVoidFunction = void (*)();
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(VkInstance, const char*);
using PFN_vkEnumerateInstanceVersion = VkResult (*)(std::uint32_t*);
using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
using PFN_vkDestroyInstance = void (*)(VkInstance, const void*);
using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, std::uint32_t*, VkPhysicalDevice*);

class DynamicLibrary {
public:
    DynamicLibrary() noexcept {
#if defined(_WIN32)
        handle_ = LoadLibraryA("vulkan-1.dll");
#else
        handle_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
    }
    ~DynamicLibrary() {
#if defined(_WIN32)
        if (handle_) FreeLibrary(static_cast<HMODULE>(handle_));
#else
        if (handle_) dlclose(handle_);
#endif
    }
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] void* symbol(const char* name) const noexcept {
#if defined(_WIN32)
        return handle_ ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name)) : nullptr;
#else
        return handle_ ? dlsym(handle_, name) : nullptr;
#endif
    }
private:
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

template <typename Fn>
Fn cast_function(void* symbol) noexcept {
    Fn out{};
    static_assert(sizeof(out) == sizeof(symbol));
    std::memcpy(&out, &symbol, sizeof(out));
    return out;
}

template <typename Fn>
Fn cast_function(PFN_vkVoidFunction symbol) noexcept {
    Fn out{};
    static_assert(sizeof(out) == sizeof(symbol));
    std::memcpy(&out, &symbol, sizeof(out));
    return out;
}

} // namespace

VulkanProbeResult probe_vulkan_loader() noexcept {
    VulkanProbeResult result;
#if defined(_WIN32)
    result.loader_name = "vulkan-1.dll";
#else
    result.loader_name = "libvulkan.so.1";
#endif

    DynamicLibrary library;
    if (!library.valid()) {
        result.message = "Vulkan loader not found";
        return result;
    }
    result.loader_found = true;

    const auto get_proc = cast_function<PFN_vkGetInstanceProcAddr>(library.symbol("vkGetInstanceProcAddr"));
    if (!get_proc) {
        result.message = "vkGetInstanceProcAddr missing from loader";
        return result;
    }

    result.loader_api_version = make_api_version(1, 0, 0);
    if (const auto enumerate_version = cast_function<PFN_vkEnumerateInstanceVersion>(get_proc(nullptr, "vkEnumerateInstanceVersion"))) {
        std::uint32_t version = result.loader_api_version;
        if (enumerate_version(&version) == VK_SUCCESS) result.loader_api_version = version;
    }

    const auto create_instance = cast_function<PFN_vkCreateInstance>(get_proc(nullptr, "vkCreateInstance"));
    if (!create_instance) {
        result.message = "vkCreateInstance missing from loader";
        return result;
    }

    const VkApplicationInfo app{
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "Thunder Vulkan Probe",
        1u,
        "Thunder",
        1u,
        std::min(result.loader_api_version, make_api_version(1, 3, 0))
    };
    const VkInstanceCreateInfo create_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0u,
        &app,
        0u,
        nullptr,
        0u,
        nullptr
    };

    VkInstance instance = nullptr;
    result.create_instance_result = create_instance(&create_info, nullptr, &instance);
    if (result.create_instance_result != VK_SUCCESS || !instance) {
        result.message = "Vulkan loader reachable, but no instance could be created (usually no ICD/GPU in headless CI)";
        return result;
    }
    result.instance_created = true;

    const auto enumerate_devices = cast_function<PFN_vkEnumeratePhysicalDevices>(get_proc(instance, "vkEnumeratePhysicalDevices"));
    const auto destroy_instance = cast_function<PFN_vkDestroyInstance>(get_proc(instance, "vkDestroyInstance"));
    if (enumerate_devices) {
        std::uint32_t count = 0;
        if (enumerate_devices(instance, &count, nullptr) == VK_SUCCESS) result.physical_device_count = count;
    }
    if (destroy_instance) destroy_instance(instance, nullptr);

    result.message = result.physical_device_count > 0u
        ? "Vulkan instance and physical-device enumeration succeeded"
        : "Vulkan instance succeeded; no physical devices exposed";
    return result;
}

} // namespace thunder
