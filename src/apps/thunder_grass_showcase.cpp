// Thunder near-view material showcase: P0 temperate grass set under a movable
// sun, split against the current procedural paint. Controls:
//   Space  cycle view (split / textured / procedural)
//   Arrow keys / WASD  move the sun direction
//   +/- (or wheel)  change tiling density (world metres)
//   Esc  quit
// Expects three raw RGB8 files (1024x1024) next to the executable:
//   grass_albedo.raw / grass_normal.raw / grass_orm.raw
// (converted from the P0 pack PNGs by tools/convert_grass_textures.py).
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t tex_size = 1024u;

#define VK_CHECK(expr)                                                        \
    do {                                                                      \
        if ((expr) != VK_SUCCESS)                                             \
            throw std::runtime_error(std::string{"Vulkan call failed: "} + #expr); \
    } while (0)

std::vector<char> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    const auto size = static_cast<std::size_t>(in.tellg());
    std::vector<char> bytes(size);
    in.seekg(0);
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    return bytes;
}

std::vector<std::byte> read_raw_rgb(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (bytes.size() != static_cast<std::size_t>(tex_size) * tex_size * 3u)
        throw std::runtime_error("raw texture must be 1024x1024 RGB8: " + path.string());
    // Expand RGB8 -> RGBA8 for a well-aligned image format.
    std::vector<std::byte> rgba(static_cast<std::size_t>(tex_size) * tex_size * 4u);
    for (std::size_t p = 0; p < static_cast<std::size_t>(tex_size) * tex_size; ++p) {
        rgba[p * 4u + 0u] = static_cast<std::byte>(bytes[p * 3u + 0u]);
        rgba[p * 4u + 1u] = static_cast<std::byte>(bytes[p * 3u + 1u]);
        rgba[p * 4u + 2u] = static_cast<std::byte>(bytes[p * 3u + 2u]);
        rgba[p * 4u + 3u] = static_cast<std::byte>(255u);
    }
    return rgba;
}

VK_DEFINE_HANDLE(VkInstance) // keep clangd quiet; real decl comes from vulkan.h

namespace {
VKAPI_ATTR VkBool32 VKAPI_CALL showcase_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[vulkan validation] " << data->pMessage << "\n";
    return VK_FALSE;
}
} // namespace

class Showcase {
public:
    void run() {
        create_window();
        create_vulkan();
        create_swapchain();
        create_render_pass();
        create_pipeline();
        create_sync(); // command buffers first: texture uploads record into them
        create_textures();
        create_descriptors();
        create_framebuffers();
        main_loop();
    }

private:
    // ---- window / context -------------------------------------------------
    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent_{1280, 720};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;

    // ---- pipeline ----------------------------------------------------------
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    // ---- textures ----------------------------------------------------------
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    std::array<Texture, 3> textures_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    // ---- sync --------------------------------------------------------------
    static constexpr std::uint32_t frames_in_flight = 2u;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkSemaphore rendered = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };
    Frame frames_[frames_in_flight]{};
    std::vector<VkSemaphore> image_rendered_; // per swapchain image
    std::uint32_t frame_index_ = 0u;

    // ---- controls ----------------------------------------------------------
    int mode_ = 0;            // 0 split, 1 textured, 2 procedural
    float light_azimuth_ = 2.2f;
    float light_elevation_ = 0.9f;
    float tiles_ = 8.0f;
    float anti_tiling_ = 1.0f;  // T toggles: rotation-blend + macro tint
    float stylize_ = 1.0f;      // G toggles: V3 painterly mute vs raw photo PBR

    void create_window() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        window_ = SDL_CreateWindow("Thunder Grass Material Showcase", 1280, 720,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window_) throw std::runtime_error(SDL_GetError());
    }

    void create_vulkan() {
        std::uint32_t extension_count = 0u;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (extensions == nullptr) throw std::runtime_error(SDL_GetError());
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "ThunderGrassShowcase";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app;
        instance_info.enabledExtensionCount = extension_count;
        instance_info.ppEnabledExtensionNames = extensions;
        const bool validation = std::getenv("THUNDER_VALIDATION") != nullptr;
        const char* validation_layer = "VK_LAYER_KHRONOS_validation";
        VkDebugUtilsMessengerCreateInfoEXT debug_info{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        std::vector<const char*> all_extensions(extensions, extensions + extension_count);
        if (validation) {
            all_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            instance_info.enabledLayerCount = 1u;
            instance_info.ppEnabledLayerNames = &validation_layer;
            debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_info.pfnUserCallback = showcase_debug_callback;
            instance_info.pNext = &debug_info;
            instance_info.enabledExtensionCount =
                static_cast<std::uint32_t>(all_extensions.size());
            instance_info.ppEnabledExtensionNames = all_extensions.data();
            std::cerr << "[showcase] validation layers ON\n";
        }
        VK_CHECK(vkCreateInstance(&instance_info, nullptr, &instance_));

        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
            throw std::runtime_error(SDL_GetError());

        std::uint32_t device_count = 0u;
        vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
        if (device_count == 0u) throw std::runtime_error("no Vulkan physical device");
        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
        physical_ = devices.front();
        for (auto device : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                physical_ = device;
                break;
            }
        }

        std::uint32_t family_count = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &family_count, families.data());
        for (std::uint32_t i = 0; i < family_count; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_, i, surface_, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && present) {
                queue_family_ = i;
                break;
            }
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family_;
        queue_info.queueCount = 1u;
        queue_info.pQueuePriorities = &priority;
        const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1u;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = 1u;
        device_info.ppEnabledExtensionNames = device_extensions;
        VK_CHECK(vkCreateDevice(physical_, &device_info, nullptr, &device_));
        vkGetDeviceQueue(device_, queue_family_, 0u, &queue_);
    }

    void create_swapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps));
        extent_ = caps.currentExtent;
        std::uint32_t format_count = 0u;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, formats.data());
        swapchain_format_ = formats.front().format;
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB ||
                format.format == VK_FORMAT_R8G8B8A8_SRGB) {
                swapchain_format_ = format.format;
                break;
            }
        }
        std::uint32_t image_count = caps.minImageCount + 1u;
        if (caps.maxImageCount > 0u && image_count > caps.maxImageCount)
            image_count = caps.maxImageCount;
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface_;
        info.minImageCount = image_count;
        info.imageFormat = swapchain_format_;
        info.imageColorSpace = formats.front().colorSpace;
        info.imageExtent = extent_;
        info.imageArrayLayers = 1u;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_));

        std::uint32_t count = 0u;
        vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
        swapchain_images_.resize(count);
        vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchain_images_.data());
        swapchain_views_.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view_info.image = swapchain_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = swapchain_format_;
            view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &swapchain_views_[i]));
        }
    }

    void create_render_pass() {
        VkAttachmentDescription color{};
        color.format = swapchain_format_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1u;
        subpass.pColorAttachments = &color_ref;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 1u;
        info.pAttachments = &color;
        info.subpassCount = 1u;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1u;
        info.pDependencies = &dependency;
        VK_CHECK(vkCreateRenderPass(device_, &info, nullptr, &render_pass_));
    }

    void create_pipeline() {
        const auto base = std::filesystem::path{"shaders"};
        const auto vert_code = read_file(base / "grass_showcase.vert.spv");
        const auto frag_code = read_file(base / "grass_showcase.frag.spv");
        VkShaderModule vert = create_module(vert_code);
        VkShaderModule frag = create_module(frag_code);

        VkDescriptorSetLayoutBinding bindings[3]{};
        for (std::uint32_t i = 0; i < 3u; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1u;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = 3u;
        layout_info.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr,
                                             &descriptor_layout_));

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0u;
        push.size = 64u; // vec4 * 4
        VkPipelineLayoutCreateInfo pipeline_info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_info.setLayoutCount = 1u;
        pipeline_info.pSetLayouts = &descriptor_layout_;
        pipeline_info.pushConstantRangeCount = 1u;
        pipeline_info.pPushConstantRanges = &push;
        VK_CHECK(vkCreatePipelineLayout(device_, &pipeline_info, nullptr,
                                        &pipeline_layout_));

        VkPipelineShaderStageCreateInfo stages[2]{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr}};
        VkPipelineVertexInputStateCreateInfo vertex_input{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{};
        VkRect2D scissor{};
        VkPipelineViewportStateCreateInfo viewport_state{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.viewportCount = 1u;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1u;
        viewport_state.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend_state{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend_state.attachmentCount = 1u;
        blend_state.pAttachments = &blend;
        // Viewport/scissor are dynamic: the swapchain extent is only known at
        // draw time, and a static 0x0 viewport is a spec violation.
        const std::array<VkDynamicState, 2> dynamic_states{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic_state.dynamicStateCount = 2u;
        dynamic_state.pDynamicStates = dynamic_states.data();
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2u;
        info.pStages = stages;
        info.pVertexInputState = &vertex_input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport_state;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend_state;
        info.pDynamicState = &dynamic_state;
        info.layout = pipeline_layout_;
        info.renderPass = render_pass_;
        info.subpass = 0u;
        VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1u, &info, nullptr,
                                           &pipeline_));
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
    }

    [[nodiscard]] VkShaderModule create_module(const std::vector<char>& code) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device_, &info, nullptr, &module));
        return module;
    }

    void create_textures() {
        const char* files[3] = {"grass_albedo.raw", "grass_normal.raw", "grass_orm.raw"};
        for (std::uint32_t i = 0; i < 3u; ++i) {
            upload_texture(read_raw_rgb(files[i]), textures_[i]);
        }
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = static_cast<float>(
            static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>(tex_size)))) + 1u);
        VK_CHECK(vkCreateSampler(device_, &sampler_info, nullptr, &sampler_));

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 3u;
        VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1u;
        pool_info.poolSizeCount = 1u;
        pool_info.pPoolSizes = &pool_size;
        VK_CHECK(vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_));

        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = descriptor_pool_;
        alloc.descriptorSetCount = 1u;
        alloc.pSetLayouts = &descriptor_layout_;
        VK_CHECK(vkAllocateDescriptorSets(device_, &alloc, &descriptor_set_));
        for (std::uint32_t i = 0; i < 3u; ++i) {
            VkDescriptorImageInfo image_info{};
            image_info.sampler = sampler_;
            image_info.imageView = textures_[i].view;
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptor_set_;
            write.dstBinding = i;
            write.descriptorCount = 1u;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image_info;
            vkUpdateDescriptorSets(device_, 1u, &write, 0u, nullptr);
        }
    }

    void upload_texture(const std::vector<std::byte>& rgba, Texture& texture) {
        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = rgba.size();
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        VK_CHECK(vkCreateBuffer(device_, &buffer_info, nullptr, &staging));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, staging, &requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = find_memory_type(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK(vkAllocateMemory(device_, &alloc, nullptr, &staging_memory));
        vkBindBufferMemory(device_, staging, staging_memory, 0u);
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(device_, staging_memory, 0u, rgba.size(), 0u, &mapped));
        std::memcpy(mapped, rgba.data(), rgba.size());
        vkUnmapMemory(device_, staging_memory);

        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {tex_size, tex_size, 1u};
        image_info.mipLevels =
            (std::getenv("THUNDER_NO_MIPS") != nullptr)
                ? 1u
                : static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>(tex_size)))) + 1u;
        image_info.arrayLayers = 1u;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(device_, &image_info, nullptr, &texture.image));
        vkGetImageMemoryRequirements(device_, texture.image, &requirements);
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(device_, &alloc, nullptr, &texture.memory));
        vkBindImageMemory(device_, texture.image, texture.memory, 0u);

        VK_CHECK(vkResetCommandPool(device_, command_pool_, 0u));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(frames_[0].command, &begin));
        VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = texture.image;
        to_transfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        to_transfer.srcAccessMask = 0u;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(frames_[0].command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u,
                             nullptr, 1u, &to_transfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {tex_size, tex_size, 1u};
        vkCmdCopyBufferToImage(frames_[0].command, staging, texture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);

        // Mipmap chain (blit-based), recorded into the SAME submission as the
        // copy: far-view minification without mips aliases into shimmering
        // noise. With mips disabled the level-0 fallback transition runs
        // instead.
        const std::uint32_t mip_levels = image_info.mipLevels;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = texture.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        if (mip_levels > 1u) {
            std::int32_t mip_width = static_cast<std::int32_t>(tex_size);
            std::int32_t mip_height = static_cast<std::int32_t>(tex_size);
            for (std::uint32_t level = 1u; level < mip_levels; ++level) {
                // Source level: every level sits in TRANSFER_DST after the
                // copy (level 0) or the previous blit (level > 0).
                barrier.subresourceRange.baseMipLevel = level - 1u;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(frames_[0].command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u,
                                     nullptr, 1u, &barrier);
                barrier.subresourceRange.baseMipLevel = level;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcAccessMask = 0u;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(frames_[0].command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u,
                                     nullptr, 1u, &barrier);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1u, 0, 1};
                blit.srcOffsets[1] = {mip_width, mip_height, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                blit.dstOffsets[1] = {mip_width > 1 ? mip_width / 2 : 1,
                                      mip_height > 1 ? mip_height / 2 : 1, 1};
                vkCmdBlitImage(frames_[0].command, texture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &blit,
                               VK_FILTER_LINEAR);
                if (mip_width > 1) mip_width /= 2;
                if (mip_height > 1) mip_height /= 2;
            }
            // Release to shader read: levels 0..N-2 sit in TRANSFER_SRC (they
            // were blit sources), level N-1 sits in TRANSFER_DST (it was the
            // final blit destination).
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels - 1u, 0, 1};
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(frames_[0].command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, nullptr,
                                 0u, nullptr, 1u, &barrier);
            if (mip_levels > 1u) {
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip_levels - 1u, 1u, 0, 1};
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                vkCmdPipelineBarrier(frames_[0].command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, nullptr,
                                     0u, nullptr, 1u, &barrier);
            }
        } else {
            VkImageMemoryBarrier to_shader = to_transfer;
            to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(frames_[0].command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, nullptr,
                                 0u, nullptr, 1u, &to_shader);
        }
        VK_CHECK(vkEndCommandBuffer(frames_[0].command));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1u;
        submit.pCommandBuffers = &frames_[0].command;
        VK_CHECK(vkQueueSubmit(queue_, 1u, &submit, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(queue_));

        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = texture.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1};
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &texture.view));

        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_memory, nullptr);
    }

    void create_descriptors() {}

    void create_framebuffers() {
        framebuffers_.resize(swapchain_views_.size());
        for (std::size_t i = 0; i < swapchain_views_.size(); ++i) {
            VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            info.renderPass = render_pass_;
            info.attachmentCount = 1u;
            info.pAttachments = &swapchain_views_[i];
            info.width = extent_.width;
            info.height = extent_.height;
            info.layers = 1u;
            VK_CHECK(vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]));
        }
    }

    void create_sync() {
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_;
        VK_CHECK(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_));
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = frames_in_flight;
        for (std::uint32_t i = 0; i < frames_in_flight; ++i) {
            VK_CHECK(vkAllocateCommandBuffers(device_, &alloc, &frames_[i].command));
            VkSemaphoreCreateInfo semaphore_info{
                VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                                       &frames_[i].acquired));
            VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                                       &frames_[i].rendered));
            VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &frames_[i].fence));
        }
        // One "render finished" semaphore per swapchain image: a semaphore can
        // only be re-signaled once the present engine has consumed it, and
        // present completion is not fenced — key it to the image instead.
        image_rendered_.resize(swapchain_images_.size());
        for (auto& semaphore : image_rendered_) {
            VkSemaphoreCreateInfo semaphore_info{
                VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr, &semaphore));
        }
    }

    void main_loop() {
        for (std::uint32_t i = 0; i < frames_in_flight; ++i)
            VK_CHECK(vkWaitForFences(device_, 1u, &frames_[i].fence, VK_TRUE,
                                     std::numeric_limits<std::uint64_t>::max()));
        bool quit = false;
        while (!quit) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN &&
                     event.key.key == SDLK_ESCAPE)) {
                    quit = true;
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    switch (event.key.key) {
                    case SDLK_SPACE: mode_ = (mode_ + 1) % 3; break;
                    case SDLK_T: anti_tiling_ = anti_tiling_ > 0.5f ? 0.0f : 1.0f; break;
                    case SDLK_G: stylize_ = stylize_ > 0.5f ? 0.0f : 1.0f; break;
                    case SDLK_LEFT: case SDLK_A: light_azimuth_ -= 0.15f; break;
                    case SDLK_RIGHT: case SDLK_D: light_azimuth_ += 0.15f; break;
                    case SDLK_UP: case SDLK_W:
                        light_elevation_ = std::min(1.45f, light_elevation_ + 0.12f);
                        break;
                    case SDLK_DOWN: case SDLK_S:
                        light_elevation_ = std::max(0.05f, light_elevation_ - 0.12f);
                        break;
                    case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS:
                        tiles_ = std::min(96.0f, tiles_ * 1.25f);
                        break;
                    case SDLK_MINUS: case SDLK_KP_MINUS:
                        tiles_ = std::max(1.0f, tiles_ / 1.25f);
                        break;
                    default: break;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    tiles_ = event.wheel.y > 0 ? std::min(96.0f, tiles_ * 1.2f)
                                               : std::max(1.0f, tiles_ / 1.2f);
                } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                    // FIFO + capped extent: swapchain rebuild omitted for the
                    // showcase; the window is expected at 1280x720.
                }
            }
            draw_frame();
        }
        VK_CHECK(vkDeviceWaitIdle(device_));
    }

    void draw_frame() {
        VK_CHECK(vkWaitForFences(device_, 1u, &frames_[frame_index_].fence, VK_TRUE,
                                 std::numeric_limits<std::uint64_t>::max()));
        std::uint32_t image_index = 0u;
        const auto acquire = vkAcquireNextImageKHR(
            device_, swapchain_, std::numeric_limits<std::uint64_t>::max(),
            frames_[frame_index_].acquired, VK_NULL_HANDLE, &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            VK_CHECK(vkResetFences(device_, 1u, &frames_[frame_index_].fence));
            return;
        }
        VK_CHECK(vkResetFences(device_, 1u, &frames_[frame_index_].fence));
        VK_CHECK(vkResetCommandBuffer(frames_[frame_index_].command, 0u));

        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(frames_[frame_index_].command, &begin));
        VkClearValue clear{{{0.05f, 0.06f, 0.07f, 1.0f}}};
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = render_pass_;
        pass.framebuffer = framebuffers_[image_index];
        pass.renderArea.extent = extent_;
        pass.clearValueCount = 1u;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(frames_[frame_index_].command, &pass,
                             VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(frames_[frame_index_].command,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent_.width),
                            static_cast<float>(extent_.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, extent_};
        vkCmdSetViewport(frames_[frame_index_].command, 0u, 1u, &viewport);
        vkCmdSetScissor(frames_[frame_index_].command, 0u, 1u, &scissor);
        vkCmdBindDescriptorSets(frames_[frame_index_].command,
                                VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                                0u, 1u, &descriptor_set_, 0u, nullptr);
        struct Push {
            float light[4];
            float params[4];
            float resolution[4];
            float style[4];
        } push{};
        const float cl = std::cos(light_azimuth_) * std::cos(light_elevation_);
        const float sl = std::sin(light_azimuth_) * std::cos(light_elevation_);
        push.light[0] = cl;
        push.light[1] = sl;
        push.light[2] = std::sin(light_elevation_);
        push.params[0] = static_cast<float>(mode_);
        push.params[1] = tiles_;
        push.params[2] = static_cast<float>(extent_.width) /
                         static_cast<float>(extent_.height);
        push.params[3] = anti_tiling_;
        push.style[0] = stylize_;
        push.resolution[0] = static_cast<float>(extent_.width);
        vkCmdPushConstants(frames_[frame_index_].command, pipeline_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(push), &push);
        vkCmdDraw(frames_[frame_index_].command, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(frames_[frame_index_].command);
        VK_CHECK(vkEndCommandBuffer(frames_[frame_index_].command));

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1u;
        submit.pWaitSemaphores = &frames_[frame_index_].acquired;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1u;
        submit.pCommandBuffers = &frames_[frame_index_].command;
        submit.signalSemaphoreCount = 1u;
        submit.pSignalSemaphores = &image_rendered_[image_index];
        VK_CHECK(vkQueueSubmit(queue_, 1u, &submit, frames_[frame_index_].fence));
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1u;
        present.pWaitSemaphores = &image_rendered_[image_index];
        present.swapchainCount = 1u;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index;
        (void)vkQueuePresentKHR(queue_, &present);
        frame_index_ = (frame_index_ + 1u) % frames_in_flight;
    }

    [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t bits,
                                                 VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) != 0u &&
                (properties.memoryTypes[i].propertyFlags & required) == required)
                return i;
        }
        throw std::runtime_error("no suitable Vulkan memory type");
    }
};

} // namespace

int main() {
    try {
        Showcase showcase;
        showcase.run();
        std::cout << "Grass showcase closed cleanly\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "grass showcase: " << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
