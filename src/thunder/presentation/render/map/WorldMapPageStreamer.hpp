#pragma once

#include "thunder/presentation/render/map/WorldMapPage.hpp"
#include "thunder/presentation/render/map/WorldMapPageKey.hpp"
#include "thunder/presentation/render/map/WorldResidentLayout.hpp"
#include "thunder/presentation/render/map/WorldMapPageSource.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/content/worldpack/WorldPackMetadata.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace thunder {

// One instanced world-map patch, mirrored byte-for-byte by the
// WorldMapPatchGpu std430 block in world_map.vert.
struct WorldMapPatchGpu {
    // u0, v0, du, dv in continuous (unwrapped-x) map UV. The vertex stage
    // wraps u for sampling and takes the shortest arc for projection, so the
    // rect itself may hang off either side of [0, 1).
    std::array<float, 4> rect{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::uint32_t level_a = 0;  // fine level actually sampled
    std::uint32_t level_b = 0;  // coarse morph target (== level_a when m == 1)
    float morph = 1.0f;         // 1 = fine only; 0 = coarse only
    float pad = 0.0f;
};

static_assert(sizeof(WorldMapPatchGpu) == 32u);

// A2-R residency owner for the world map. Every page of every clip level is
// decoded once at startup and uploaded into the resident pyramids; after that
// the streamer only produces instanced patches for the current camera window.
//
// The decode is bounded and asynchronous: a fixed worker pool owns one pack
// reader each, the frame thread pops finished pages (applying GPU residency
// bookkeeping), and the worker queue applies backpressure so the decoded
// payload cannot accumulate. No filesystem or decompression work ever runs on
// the frame thread.
class WorldMapPageStreamer {
public:
    // worker_count pack readers decode in parallel; 1490 pages at ~0.25 ms
    // each saturate one worker for ~0.4 s, three finish inside the upload
    // window (~12 frames).
    explicit WorldMapPageStreamer(std::size_t worker_count = 3u);
    ~WorldMapPageStreamer();

    WorldMapPageStreamer(const WorldMapPageStreamer&) = delete;
    WorldMapPageStreamer& operator=(const WorldMapPageStreamer&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& path, std::string& diagnostic);
    void close() noexcept;

    [[nodiscard]] bool ready() const noexcept { return source_.ready(); }
    [[nodiscard]] const WorldPackMetadata& metadata() const noexcept { return source_.metadata(); }
    [[nodiscard]] const WorldPackStats& stats() const noexcept { return source_.stats(); }

    // Queues every page of every level once (coarse levels first, then by
    // distance to the view centre). Safe to call every frame; only the first
    // call after open() does work.
    void enqueue_all_pages(std::array<float, 4> map_view);

    // Pops up to `max_pages` decoded pages into the frame-thread upload
    // window and marks them resident (resident bit, arrival frame, remaining
    // count). The payloads stay valid until clear_upload_window(); the
    // renderer copies them into its staging ring inside that window. Pages
    // decoded this frame become patch-eligible on the next frame, which
    // matches when their pixels actually reach the samplers.
    std::uint32_t pop_decoded(std::uint32_t frame, std::uint32_t max_pages);
    [[nodiscard]] const std::deque<std::pair<WorldMapPageKey, WorldMapPage>>&
        upload_window() const noexcept { return upload_window_; }
    void clear_upload_window() noexcept;

    [[nodiscard]] bool resident(std::uint32_t level, std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] std::uint32_t resident_count() const noexcept;
    [[nodiscard]] std::uint32_t remaining() const noexcept { return remaining_; }
    [[nodiscard]] bool all_resident() const noexcept { return remaining_ == 0u; }

    // Fractional LOD: how many pixels one finest-level quad covers, in
    // [0, 3]. The integer part picks the fine level, the fraction blends the
    // coarser one, so slow zooms morph continuously instead of popping.
    [[nodiscard]] static float lod_fraction(std::array<float, 4> map_view, float screen_width_px);

    // Builds the instanced patch list for the current camera. Pages whose
    // fine level has not landed yet fall back to the nearest coarser resident
    // ancestor (startup only; the whole pyramid is resident afterwards), and
    // freshly-arrived pages fade in over `fade_seconds` through the morph.
    void build_world_patches(std::array<float, 4> map_view,
                             float screen_width_px,
                             std::uint64_t frame,
                             std::vector<WorldMapPatchGpu>& out) const;

    // Wall-clock seconds per frame, used by the arrival fade. Clamped.
    void set_frame_dt(float dt) noexcept { frame_dt_ = dt; }

private:
    void start_workers();
    void stop_workers() noexcept;
    void worker_main();

    WorldMapPageSource source_;
    std::filesystem::path source_path_;

    std::size_t worker_count_;
    std::vector<std::thread> workers_;
    mutable std::mutex decode_mutex_;
    std::condition_variable decode_cv_;
    std::deque<WorldMapPageKey> decode_requests_;
    std::deque<std::pair<WorldMapPageKey, WorldMapPage>> decode_results_;
    bool decode_stop_ = false;
    static constexpr std::size_t kMaxQueuedResults = 1024u;

    // Residency bookkeeping, written by the frame thread only.
    std::array<std::vector<std::uint8_t>, 4> resident_{};
    std::array<std::vector<std::uint64_t>, 4> arrived_frame_{};
    std::deque<std::pair<WorldMapPageKey, WorldMapPage>> upload_window_;
    std::uint32_t remaining_ = 0u;
    bool enqueued_ = false;
    float frame_dt_ = 1.0f / 60.0f;
    static constexpr float kFadeSeconds = 0.15f;
};

} // namespace thunder
