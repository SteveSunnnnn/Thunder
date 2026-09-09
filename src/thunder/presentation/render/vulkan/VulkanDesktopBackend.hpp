#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#include "thunder/presentation/ui/FontAtlas.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include "thunder/presentation/render/flag/DynamicFlag3D.hpp"
#include "thunder/presentation/render/map/PoliticalMapState.hpp"
#include "thunder/presentation/render/map/WorldMapPageStreamer.hpp"
#include "thunder/presentation/render/RenderQuality.hpp"
#include "thunder/presentation/render/terrain/TerrainClipmap.hpp"
#include "thunder/presentation/render/terrain/TerrainMaterialShading.hpp"
#include "thunder/presentation/render/BindlessMaterialSystem.hpp"
#include "thunder/presentation/render/environment/VolumetricAtmosphereAndClouds.hpp"
#include "thunder/presentation/render/water/PhysicalWaterPass.hpp"
#include "thunder/presentation/render/vegetation/ForestCanopyInstancer.hpp"
#include "thunder/presentation/render/water/RiverSplineFlowPass.hpp"
#include "thunder/presentation/render/vfx/LivingMapVfx3D.hpp"
#include "thunder/presentation/render/map/BorderMesh3D.hpp"

#include <chrono>

struct SDL_Window;

namespace thunder {

// Per-frame timing sample. GPU time comes from timestamp queries written
// around the whole command buffer; CPU time measures the host-side work in
// draw_frame() including command recording.
struct FrameTiming {
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
};

// Aggregated statistics over the frames since the last reset. Kept separate
// from the capability report so the benchmark harness can slice it freely.
struct RenderStats {
    std::uint64_t sampled_frames = 0;
    double avg_frame_ms = 0.0;
    double min_frame_ms = 0.0;
    double max_frame_ms = 0.0;
    double p95_frame_ms = 0.0;
    double avg_gpu_ms = 0.0;
    double avg_cpu_ms = 0.0;
    double avg_cpu_submission_ms = 0.0; // record + submit, excluding fence/acquire/present waits
    double fps = 0.0;
    std::uint64_t draw_calls_last_frame = 0;
};

// Expanded UI vertex used only at the renderer boundary.  UiDrawList keeps a
// compact AARRGGBB color; the Vulkan path uploads normalized float channels so
// the shader layout stays explicit and portable across backends.
struct UiGpuVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

class VulkanDesktopBackend {
public:
    VulkanDesktopBackend();
    ~VulkanDesktopBackend();
    VulkanDesktopBackend(const VulkanDesktopBackend&) = delete;
    VulkanDesktopBackend& operator=(const VulkanDesktopBackend&) = delete;

    void initialize(SDL_Window* window, bool enable_validation);
    // Optional client-owned UI resources.  Paths are read before initialize()
    // and are never part of authoritative simulation state.
    void set_ui_font_atlas(std::filesystem::path image_path,
                           std::filesystem::path metrics_path = {}) {
        ui_font_atlas_path_ = std::move(image_path);
        ui_font_metrics_path_ = std::move(metrics_path);
    }
    // The world map is a streamable .thunderworld page family. It is opened once
    // during renderer setup; the backend never accepts a second legacy atlas.
    void set_world_pack(std::filesystem::path path) { world_pack_path_ = std::move(path); }
    void set_world_political_state(std::span<const ProvincePoliticalRecord> records,
                                   std::span<const Rgba8> country_colors);
    void submit_living_instances(std::span<const std::array<float, 16>> transforms);
    void set_shader_dir(std::filesystem::path path) { shader_dir_ = std::move(path); }
    void set_dynamic_flag(DynamicFlag3D flag) { dynamic_flag_ = std::move(flag); }
    void draw_frame();
    // Capture the next fully composited swapchain image. This is an explicit
    // one-frame GPU readback intended for visual QA and automated screenshots;
    // ordinary gameplay frames never pay its synchronization cost.
    void request_screenshot(std::filesystem::path path) {
        pending_screenshot_path_ = std::move(path);
    }
    // Stage a UI draw list for the next frame. Solid and polyline batches are
    // rendered through the zero-descriptor UI pipeline; client-owned textured
    // batches use stable keys and are sampled when their optional resources
    // have been installed.
    void submit_ui(const UiDrawList& ui);

    // Stage the static map overlay (vector borders) for persistent GPU reuse.
    // Unlike submit_ui(), the geometry is uploaded only when the caller reports
    // that it changed (camera moved / resize) and is redrawn every frame from
    // the resident buffers, so stationary frames avoid re-converting and
    // re-uploading hundreds of thousands of border vertices.
    void submit_map_overlay(std::span<const UiVertex> vertices,
                            std::span<const std::uint32_t> indices);

    // Map viewport for the live validation renderer, in uv space:
    // (cx, cy) center and (hx, hy) half extents. Defaults to the full map.
    void set_map_view(float cx, float cy, float hx, float hy,
                      float altitude_m = 250'000.0f,
                      float pitch_deg = 52.0f) noexcept {
        map_view_[0] = cx;
        map_view_[1] = cy;
        map_view_[2] = hx;
        map_view_[3] = hy;
        map_camera_[0] = altitude_m;
        map_camera_[1] = pitch_deg;
    }
    // Static screen-space map overlays must use the same view as the last
    // fully resident page set. During a drag the requested camera may be one
    // or more page uploads ahead of that committed view.
    [[nodiscard]] std::array<float, 4> committed_map_view() const noexcept {
        return {{world_render_view_[0], world_render_view_[1],
                 world_render_view_[2], world_render_view_[3]}};
    }
    [[nodiscard]] bool committed_map_ready() const noexcept {
        return world_patch_count_ != 0u;
    }
    void wait_idle();
    void write_report(const std::filesystem::path& path) const;

    [[nodiscard]] std::uint64_t frames_presented() const noexcept { return frames_presented_; }
    [[nodiscard]] std::uint32_t validation_errors() const noexcept {
        return validation_errors_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t validation_warnings() const noexcept {
        return validation_warnings_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool validation_enabled() const noexcept { return validation_enabled_; }

    // Render quality control. The tier must be selected before initialize()
    // so the swapchain-dependent targets are created with the right format and
    // sample count. `Auto` resolves from device capabilities.
    void set_quality_tier(RenderQuality tier) noexcept { requested_quality_ = tier; }
    [[nodiscard]] RenderQuality quality_tier() const noexcept { return active_quality_; }
    [[nodiscard]] const RenderQualitySettings& quality_settings() const noexcept { return settings_; }

    // Internal render scale, adjustable at runtime. The 3D passes render into
    // a scene target of `extent_ * render_scale` and the tonemap pass upscales
    // into the swapchain, which makes it the escape hatch for weak hardware at
    // high display resolutions without a pipeline rebuild.
    //
    // The scale is only honoured when the tier uses an offscreen HDR target.
    // On the direct-to-swapchain legacy path the scene pass writes the
    // swapchain image itself, so its viewport must stay at full resolution.
    // set_render_scale() may be called before initialize() to choose the
    // startup scale, or at any time afterwards to resize the scene target.
    void set_render_scale(float scale);
    // Visual-debug bitmask forwarded to the world-map push constants.
    void set_shading_debug(std::uint32_t flags);
    // TEMP DIAG: dump stream params, occupied span and resident page keys.
    void debug_dump_stream_state() const;
    [[nodiscard]] float render_scale() const noexcept { return settings_.render_scale; }
    [[nodiscard]] VkExtent2D scene_extent() const noexcept { return scene_extent_; }

    // Timing. reset_stats() clears the accumulator; stats() reports over the
    // frames sampled since the last reset.
    void reset_stats() noexcept;
    [[nodiscard]] const RenderStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::uint32_t sample_count() const noexcept { return msaa_samples_; }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user);

    void create_instance();
    void create_debug_messenger();
    void pick_device();
    void create_device();
    void create_swapchain();
    void destroy_swapchain();
    void create_sync();
    void destroy_sync();
    void recreate_swapchain();

    void create_live_validation_renderer();
    void destroy_live_validation_renderer();
    void create_ui_image(const std::filesystem::path& path,
                         VkImage& image,
                         VkDeviceMemory& memory,
                         VkImageView& view,
                         VkSampler& sampler,
                         std::uint32_t& width,
                         std::uint32_t& height,
                         bool repeat_horizontal = false,
                         bool nearest_filter = false,
                         bool generate_mipmaps = false);
    void destroy_ui_image(VkImage& image, VkDeviceMemory& memory,
                          VkImageView& view, VkSampler& sampler) noexcept;
    void load_font_slots(const std::filesystem::path& path);
    void load_ui_font_metrics();
    void create_hdr_targets();
    void destroy_hdr_targets();
    void create_depth_target();
    void destroy_depth_target();
    void create_scene_targets();
    void destroy_scene_targets();
    // Small helper for the offscreen targets. Dedicated allocations are fine
    // here: these images are long-lived and recreated only on resize.
    void create_image_2d(std::uint32_t width, std::uint32_t height, VkFormat format,
                         VkImageUsageFlags usage, VkImageAspectFlags aspect,
                         std::uint32_t samples, VkImage& image,
                         VkDeviceMemory& memory, VkImageView& view);
    void destroy_image_2d(VkImage& image, VkDeviceMemory& memory, VkImageView& view) noexcept;

    // Quality resolution: turns the requested tier into concrete settings
    // once device capabilities are known.
    void resolve_quality_tier();
    // Recomputes scene_extent_ from extent_ and the active render scale.
    void update_scene_extent() noexcept;
    VkFormat pick_depth_format() const;

    // Pipeline cache: SPIR-V compilation is the dominant cost when pipelines
    // are rebuilt on swapchain recreation, so the cache is persisted to disk.
    void create_pipeline_cache();
    void destroy_pipeline_cache() noexcept;

    // GPU timing.
    void create_query_pools();
    void destroy_query_pools() noexcept;
    void reset_query_pool(VkCommandBuffer command, std::uint32_t frame) const;
    void write_gpu_timestamp(VkCommandBuffer command, std::uint32_t frame, bool start) const;
    void collect_gpu_timing(std::uint32_t frame);
    void update_stats(double frame_ms, double gpu_ms, double cpu_ms, double submission_ms);
    void record_runtime_draws(VkCommandBuffer command) const;
    void record_map_label_draws(VkCommandBuffer command) const;
    void record_ui_fallback(VkCommandBuffer command) const;
    void record_tonemap(VkCommandBuffer command) const;
    void record_fxaa(VkCommandBuffer command) const;
    void record_dynamic_flag_draw(VkCommandBuffer command, const UiModuleSlot& slot) const;
    void record_ui_draws(VkCommandBuffer command) const;
    void ensure_ui_frame_buffers();
    [[nodiscard]] VkShaderModule load_shader_module(const std::filesystem::path& path) const;
    [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t bits, VkMemoryPropertyFlags required) const;
    void create_host_buffer(VkDeviceSize size,
                            VkBufferUsageFlags usage,
                            const void* data,
                            VkBuffer& buffer,
                            VkDeviceMemory& memory);
    void create_mapped_host_buffer(VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   VkBuffer& buffer,
                                   VkDeviceMemory& memory,
                                   void*& mapped);
    void destroy_mapped_host_buffer(VkBuffer& buffer,
                                    VkDeviceMemory& memory,
                                    void*& mapped) noexcept;
    void create_runtime_renderer();
    void destroy_runtime_renderer();
    void create_world_page_resources();
    void destroy_world_page_resources() noexcept;
    void open_world_pack();
    void stream_world_pages();
    void record_world_page_uploads(VkCommandBuffer command);
    void write_screenshot_bmp(const std::filesystem::path& path) const;
    void upload_map_overlay_frame();
    void ensure_living_frame_buffers(std::size_t required_bytes);

    SDL_Window* window_ = nullptr;
    bool validation_enabled_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    std::uint32_t graphics_family_ = 0;
    std::uint32_t present_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    // True when the swapchain is an sRGB format, in which case the hardware
    // performs the transfer and the tonemap shader must not gamma-encode.
    bool srgb_swapchain_ = false;
    VkExtent2D extent_{};
    // Resolution of the offscreen 3D passes. Equals extent_ at render_scale
    // 1.0 and on the direct-to-swapchain legacy path.
    VkExtent2D scene_extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> image_rendered_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    static constexpr std::uint32_t frames_in_flight = 3;
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };
    Frame frames_[frames_in_flight]{};
    std::uint32_t frame_index_ = 0;
    std::uint64_t frames_presented_ = 0;

    std::filesystem::path shader_dir_;
    bool runtime_renderer_enabled_ = false;
    VkPipelineLayout fullscreen_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout world_map_layout_ = VK_NULL_HANDLE;
    float map_view_[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    // The requested camera can move ahead of page residency. The map pass
    // consumes this last complete view until the new page set is uploaded,
    // preventing half-updated atlases from appearing as misplaced geography.
    float world_render_view_[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    float map_camera_[4] = {250'000.0f, 52.0f, 0.0f, 0.0f};
    // world_render_camera_[2] and [3] carry the *occupied* atlas span for the
    // active window (pages_x/16, pages_y/16). Slots are top-left anchored, so
    // only that top-left block is uploaded; the shader clamps to it instead of
    // the whole atlas so out-of-range samples never reach the stale columns
    // beyond the block. Defaulted to 1.0 (whole atlas) so the first frame
    // behaves exactly as before the span was exposed.
    float world_render_camera_[4] = {250'000.0f, 52.0f, 1.0f, 1.0f};
    VkPipelineLayout political_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout flag_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout tonemap_layout_ = VK_NULL_HANDLE;
    VkPipeline terrain_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ocean_pipeline_ = VK_NULL_HANDLE;
    VkPipeline political_pipeline_ = VK_NULL_HANDLE;
    VkPipeline world_map_pipeline_ = VK_NULL_HANDLE;
    VkPipeline living_pipeline_ = VK_NULL_HANDLE;
    VkPipeline flag_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_textured_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_msdf_pipeline_ = VK_NULL_HANDLE;
    VkPipeline map_label_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_textured_layout_ = VK_NULL_HANDLE;
    VkPipeline tonemap_pipeline_ = VK_NULL_HANDLE;
    VkBuffer living_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory living_vertices_memory_ = VK_NULL_HANDLE;
    VkBuffer living_instance_ = VK_NULL_HANDLE;
    VkDeviceMemory living_instance_memory_ = VK_NULL_HANDLE;
    VkBuffer flag_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory flag_vertices_memory_ = VK_NULL_HANDLE;
    VkBuffer flag_indices_ = VK_NULL_HANDLE;
    VkDeviceMemory flag_indices_memory_ = VK_NULL_HANDLE;
    std::uint32_t flag_index_count_ = 0;
    std::optional<DynamicFlag3D> dynamic_flag_{};
    VkBuffer ui_vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_vertices_memory_ = VK_NULL_HANDLE;

    // Optional atlas-backed UI resources. Fonts prefer Thunder's MSDF metrics;
    // the fixed-cell branch is retained only for diagnostics.
    std::filesystem::path ui_font_atlas_path_;
    std::filesystem::path ui_font_metrics_path_;
    VkImage ui_font_image_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_font_image_memory_ = VK_NULL_HANDLE;
    VkImageView ui_font_view_ = VK_NULL_HANDLE;
    VkSampler ui_font_sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ui_font_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool ui_font_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet ui_font_descriptor_set_ = VK_NULL_HANDLE;
    std::uint32_t ui_font_width_ = 0;
    std::uint32_t ui_font_height_ = 0;
    std::uint32_t ui_font_cell_ = 0;
    std::uint32_t ui_font_columns_ = 0;
    std::unordered_map<std::uint32_t, std::uint32_t> ui_font_slots_;
    std::unique_ptr<FontAtlas> ui_font_metrics_;
    float ui_logical_width_ = 1.0f;
    float ui_logical_height_ = 1.0f;
    float ui_scale_x_ = 1.0f;
    float ui_scale_y_ = 1.0f;

    // HDR scene target with MSAA and its resolved 1x copy consumed by the
    // tonemap pass. Recreated with the swapchain extent.
    VkFormat hdr_format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkSampleCountFlagBits msaa_samples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage hdr_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hdr_image_memory_ = VK_NULL_HANDLE;
    VkImageView hdr_view_ = VK_NULL_HANDLE;
    VkImage hdr_resolve_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hdr_resolve_memory_ = VK_NULL_HANDLE;
    VkImageView hdr_resolve_view_ = VK_NULL_HANDLE;
    // Single-sample resolve target for the MSAA colour buffer. Distinct from
    // hdr_resolve_image_ above so the legacy members keep their meaning.
    VkImage hdr_msaa_resolve_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hdr_msaa_resolve_memory_ = VK_NULL_HANDLE;
    VkImageView hdr_msaa_resolve_view_ = VK_NULL_HANDLE;
    // Intermediate target for the FXAA pass when MSAA is unavailable.
    VkImage fxaa_image_ = VK_NULL_HANDLE;
    VkDeviceMemory fxaa_memory_ = VK_NULL_HANDLE;
    VkImageView fxaa_view_ = VK_NULL_HANDLE;
    VkSampler fxaa_sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool fxaa_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet fxaa_descriptor_set_ = VK_NULL_HANDLE;
    VkSampler hdr_sampler_ = VK_NULL_HANDLE;
    bool hdr_targets_created_ = false;
    VkDescriptorPool hdr_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout hdr_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet hdr_descriptor_set_ = VK_NULL_HANDLE;

    // UI draw-list staging: CPU copies filled by submit_ui, and one
    // persistently mapped vertex/index buffer pair per frame in flight.
    struct UiGpuBatch {
        std::uint32_t first_index;
        std::uint32_t index_count;
        VkRect2D scissor;
        UiBatchKind kind = UiBatchKind::Solid;
        std::uint64_t texture = 0;
        std::uint64_t order = 0;
    };
    struct UiFrameBuffer {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        void* vertex_mapped = nullptr;
        VkDeviceSize vertex_capacity = 0;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory index_memory = VK_NULL_HANDLE;
        void* index_mapped = nullptr;
        VkDeviceSize index_capacity = 0;
    };
    std::vector<UiGpuVertex> ui_staging_vertices_;
    std::vector<std::uint32_t> ui_staging_indices_;
    std::vector<UiGpuBatch> ui_staging_batches_;
    std::vector<UiModuleSlot> ui_staging_modules_;
    UiFrameBuffer ui_frame_buffers_[frames_in_flight]{};
    bool ui_submitted_ = false;

    // Persistent static map-overlay geometry (vector borders). Uploaded on
    // camera movement only; redrawn ahead of the dynamic UI every frame.
    struct MapOverlayFrameBuffer {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        void* vertex_mapped = nullptr;
        VkDeviceSize vertex_capacity = 0;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory index_memory = VK_NULL_HANDLE;
        void* index_mapped = nullptr;
        VkDeviceSize index_capacity = 0;
        std::uint32_t index_count = 0;
        std::uint64_t generation = 0;
    };
    MapOverlayFrameBuffer map_overlay_frames_[frames_in_flight]{};
    std::vector<UiGpuVertex> map_overlay_staging_vertices_;
    std::vector<std::uint32_t> map_overlay_staging_indices_;
    std::uint64_t map_overlay_generation_ = 0;

    // ---- Resident world-pyramid resources (A2-R) -------------------------
    struct MappedHostBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;
    };

    // The three resident pyramids (see WorldResidentLayout.hpp) are allocated
    // once and never re-created; pages stream into fixed texel rectangles.
    static constexpr std::uint32_t world_palette_bytes_ = 256u * 256u * 4u;
    // Staging ring page block: [palette | height planes | id planes | sdf planes].
    static constexpr std::size_t world_upload_buffer_bytes_ =
        static_cast<std::size_t>(world_palette_bytes_) +
        static_cast<std::size_t>(kWorldUploadPageBudget) * kWorldUploadPageStride;
    // Worst-case instanced patches: a whole-world window at the finest level
    // is 40x28 pages plus one wrap margin each side; 2048 leaves headroom.
    static constexpr std::uint32_t kWorldPatchCapacity = 2048u;
    static constexpr std::size_t world_patch_buffer_bytes_ =
        static_cast<std::size_t>(kWorldPatchCapacity) * sizeof(WorldMapPatchGpu);

    std::filesystem::path world_pack_path_;
    std::unique_ptr<WorldMapPageStreamer> world_page_streamer_;
    bool world_pack_ready_ = false;
    VkImage world_height_pyramid_image_ = VK_NULL_HANDLE;
    VkDeviceMemory world_height_pyramid_memory_ = VK_NULL_HANDLE;
    VkImageView world_height_pyramid_view_ = VK_NULL_HANDLE;
    VkImage world_province_image_ = VK_NULL_HANDLE;
    VkDeviceMemory world_province_memory_ = VK_NULL_HANDLE;
    VkImageView world_province_view_ = VK_NULL_HANDLE;
    VkImage world_sdf_image_ = VK_NULL_HANDLE;
    VkDeviceMemory world_sdf_memory_ = VK_NULL_HANDLE;
    VkImageView world_sdf_view_ = VK_NULL_HANDLE;
    VkImage world_chart_sdf_image_ = VK_NULL_HANDLE;
    VkDeviceMemory world_chart_sdf_memory_ = VK_NULL_HANDLE;
    VkImageView world_chart_sdf_view_ = VK_NULL_HANDLE;
    VkImage world_palette_image_ = VK_NULL_HANDLE;
    VkDeviceMemory world_palette_memory_ = VK_NULL_HANDLE;
    VkImageView world_palette_view_ = VK_NULL_HANDLE;
    VkSampler world_linear_clamp_sampler_ = VK_NULL_HANDLE;
    VkSampler world_nearest_sampler_ = VK_NULL_HANDLE;
    VkSampler world_palette_sampler_ = VK_NULL_HANDLE;
    MappedHostBuffer world_upload_frames_[frames_in_flight]{};
    MappedHostBuffer world_patch_frames_[frames_in_flight]{};
    MappedHostBuffer world_political_frames_[frames_in_flight]{};
    std::vector<ProvincePoliticalRecord> world_political_cpu_;
    std::uint64_t world_political_revision_ = 1u;
    std::array<std::uint64_t, frames_in_flight> world_political_frame_revision_{};
    static constexpr std::size_t world_political_bytes_ = 65'536u * sizeof(ProvincePoliticalRecord);
    MappedHostBuffer world_levels_ubo_{};
    std::vector<WorldMapPatchGpu> world_patch_staging_;
    std::uint32_t world_patch_count_ = 0u;
    // Per-image "has completed its first UNDEFINED -> SHADER_READ transition".
    // Untouched regions of a not-yet-uploaded pyramid are never sampled: the
    // residency bitmaps gate every patch to uploaded pages only.
    std::array<bool, 4> world_pyramid_layout_initialized_{};
    std::vector<std::byte> world_palette_cpu_;
    bool world_palette_dirty_ = true;
    bool world_palette_layout_initialized_ = false;
    // Diagnostics for the acceptance CSV.
    float world_lod_ = 0.0f;
    std::uint32_t world_last_uploaded_ = 0u;
    float world_frame_dt_ = 1.0f / 60.0f;
    std::chrono::steady_clock::time_point world_last_stream_time_{};
    bool world_have_stream_time_ = false;
    // TEMP DIAG: one-shot height pyramid readback (THUNDER_DUMP_PYRAMID=1).
    bool world_pyramid_dumped_ = false;
    bool world_dump_pending_ = false;
    VkBuffer world_dump_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory world_dump_memory_ = VK_NULL_HANDLE;
    void* world_dump_mapped_ = nullptr;
    VkBuffer world_dump2_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory world_dump2_memory_ = VK_NULL_HANDLE;
    void* world_dump2_mapped_ = nullptr;
    void dump_height_pyramid(VkCommandBuffer command);
    void dump_page_pyramids(VkCommandBuffer command);
    VkDescriptorSetLayout world_map_scene_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool world_map_scene_descriptor_pool_ = VK_NULL_HANDLE;
    // Set 1: resident pyramids + WorldLevels UBO + per-frame patch SSBO.
    VkDescriptorSetLayout world_levels_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool world_levels_descriptor_pool_ = VK_NULL_HANDLE;
    // Set 0: the political palette. Set 1 (per frame-in-flight) is created in
    // create_world_page_resources() from world_levels_descriptor_layout_.
    VkDescriptorSet world_map_scene_descriptor_set_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, frames_in_flight> world_map_levels_descriptor_sets_{};

    // ---- Dynamic living-map uploads -------------------------------------
    MappedHostBuffer living_frame_buffers_[frames_in_flight]{};
    std::uint32_t living_instance_count_ = 0u;

    MappedHostBuffer screenshot_buffer_{};
    std::optional<std::filesystem::path> pending_screenshot_path_;
    bool swapchain_transfer_source_ = false;


    std::atomic<std::uint32_t> validation_errors_{0};
    std::atomic<std::uint32_t> validation_warnings_{0};
    VkPhysicalDeviceProperties properties_{};
    std::uint64_t device_local_bytes_ = 0;
    std::uint32_t max_sampled_images_ = 0;

    // ---- Render quality -------------------------------------------------
    // `requested_quality_` is what the caller asked for; `active_quality_` is
    // what was resolved against actual device capabilities once the physical
    // device was picked.
    RenderQuality requested_quality_ = RenderQuality::High;
    RenderQuality active_quality_ = RenderQuality::High;
    RenderQualitySettings settings_{};
    // Survives resolve_quality_tier() so a runtime scale change is not wiped
    // out the next time the tier is resolved.
    float requested_render_scale_ = 1.0f;
    // Bit patterns below 2^24 round-trip through float exactly.
    float shading_debug_flags_ = 0.0f;
    bool discrete_gpu_ = false;

    // ---- Wired orphaned pipelines (global parity) ------------------------
    // Previously TerrainClipmap / TerrainMaterialShading / BindlessMaterial
    // / VolumetricAtmosphere / PhysicalWater / ForestCanopy / RiverSpline
    // existed as standalone modules but were never referenced from the Vulkan
    // path, so the ubershader reimplemented ad-hoc copies (sin-striped dunes,
    // far-only farm lod, etc.) and regions outside the Alpine belt lost 3D
    // quality. These members prove the pipeline is now wired; the shader parity
    // fix in world_map.vert/frag is the visual side of the same wiring.
    BindlessMaterialSystem terrain_bindless_;
    TerrainClipmap terrain_clipmap_{TerrainClipmapConfig{}};
    CloudLayerUniforms wired_cloud_uniforms_{};
    SkyAtmosphereOutput wired_sky_output_{};
    LivingMapVfx3D wired_living_vfx_{};
    bool wired_terrain_initialized_ = false;
    void ensure_wired_terrain_pipelines();

    // ---- Pipeline cache -------------------------------------------------
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::filesystem::path pipeline_cache_path_;

    // ---- Depth attachment ----------------------------------------------
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    // ---- FXAA ------------------------------------------------------------
    VkPipeline fxaa_pipeline_ = VK_NULL_HANDLE;

    // ---- GPU timing ------------------------------------------------------
    // One pool per frame in flight; each holds a start/end timestamp pair.
    VkQueryPool query_pools_[frames_in_flight]{};
    // Tracks which pools have had a timestamp pair written, so results are
    // never read back before the pool has been used. Written from a const
    // recording helper.
    mutable bool query_written_[frames_in_flight]{};
    bool timing_supported_ = false;
    double timestamp_period_ns_ = 1.0;
    double last_gpu_ms_ = 0.0;
    std::chrono::steady_clock::time_point frame_start_{};
    // Wall-clock interval between consecutive draw_frame() calls. This is what
    // determines the observed frame rate; the time spent inside draw_frame()
    // alone only measures host-side cost.
    std::chrono::steady_clock::time_point last_frame_time_{};
    bool have_last_frame_time_ = false;

    // ---- Statistics ------------------------------------------------------
    RenderStats stats_{};
    std::vector<double> frame_samples_;
    // Draw-call counter is incremented from const draw-recording helpers.
    mutable std::uint64_t draw_calls_ = 0;
    bool supports_dynamic_rendering_ = false;
    bool supports_synchronization2_ = false;
    bool supports_timeline_ = false;
    bool supports_bda_ = false;
    bool supports_descriptor_indexing_ = false;
    bool supports_draw_indirect_count_ = false;
    bool supports_shader_draw_parameters_ = false;
};

} // namespace thunder
