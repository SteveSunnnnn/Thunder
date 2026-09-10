#include "thunder/presentation/render/map/WorldMapPageStreamer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace thunder {

WorldMapPageStreamer::WorldMapPageStreamer(std::size_t worker_count)
    : worker_count_(worker_count == 0u ? 1u : worker_count) {}

WorldMapPageStreamer::~WorldMapPageStreamer() {
    close();
}

void WorldMapPageStreamer::start_workers() {
    decode_stop_ = false;
    workers_.reserve(worker_count_);
    for (std::size_t index = 0; index < worker_count_; ++index)
        workers_.emplace_back([this] { worker_main(); });
}

void WorldMapPageStreamer::stop_workers() noexcept {
    {
        std::lock_guard lock(decode_mutex_);
        decode_stop_ = true;
    }
    decode_cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
    std::lock_guard lock(decode_mutex_);
    decode_requests_.clear();
    decode_results_.clear();
    decode_stop_ = false;
}

void WorldMapPageStreamer::worker_main() {
    // Each worker owns a second pack reader and decode scratch, so frame-thread
    // residency bookkeeping never races with file I/O.
    WorldMapPageSource worker_source;
    std::string diagnostic;
    if (!worker_source.open(source_path_, diagnostic)) return;

    for (;;) {
        WorldMapPageKey key{};
        {
            std::unique_lock lock(decode_mutex_);
            decode_cv_.wait(lock, [&] { return decode_stop_ || !decode_requests_.empty(); });
            if (decode_stop_) return;
            key = decode_requests_.front();
            decode_requests_.pop_front();
        }

        WorldMapPage page;
        const bool decoded = worker_source.decode(key, page);
        {
            std::unique_lock lock(decode_mutex_);
            if (decode_stop_) return;
            if (decoded) {
                // Backpressure: never let decoded pages pile up unbounded while
                // the frame thread uploads at its per-frame budget.
                decode_cv_.wait(lock, [&] {
                    return decode_stop_ || decode_results_.size() < kMaxQueuedResults;
                });
                if (decode_stop_) return;
                decode_results_.emplace_back(key, std::move(page));
            }
        }
        decode_cv_.notify_all();
    }
}

bool WorldMapPageStreamer::open(const std::filesystem::path& path, std::string& diagnostic) {
    close();
    try {
        if (!source_.open(path, diagnostic)) return false;
        const auto& metadata = source_.metadata();
        // The pyramid textures are baked for one specific clip geometry; refuse
        // any pack whose metadata disagrees instead of rendering garbage.
        if (!metadata.horizontal_wrap)
            throw std::runtime_error("world-resident pyramid requires a horizontally wrapping pack");
        if (metadata.clip_levels < kWorldLevelCount)
            throw std::runtime_error("world pack has fewer clip levels than the resident layout");
        // Packs may carry extra coarser levels for legacy deep-zoom streaming;
        // the resident pyramid stops at level 3, which already covers the
        // whole world, so only the first four levels have to agree.
        for (std::uint32_t level = 0u; level < kWorldLevelCount; ++level) {
            if (metadata.page_count_x(level) != kWorldLevels[level].cols ||
                metadata.page_count_y(level) != kWorldLevels[level].rows)
                throw std::runtime_error("world pack page grid does not match the resident layout");
        }

        for (std::uint32_t level = 0u; level < kWorldLevelCount; ++level) {
            resident_[level].assign(world_page_count(level), 0u);
            arrived_frame_[level].assign(world_page_count(level), 0u);
        }
        remaining_ = world_total_page_count();
        enqueued_ = false;
        source_path_ = std::filesystem::absolute(path);
        start_workers();
        return true;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        close();
        return false;
    }
}

void WorldMapPageStreamer::close() noexcept {
    stop_workers();
    upload_window_.clear();
    for (auto& bitmap : resident_) bitmap.clear();
    for (auto& arrivals : arrived_frame_) arrivals.clear();
    remaining_ = 0u;
    enqueued_ = false;
    source_.close();
    source_path_.clear();
}

void WorldMapPageStreamer::enqueue_all_pages(std::array<float, 4> map_view) {
    {
        std::lock_guard lock(decode_mutex_);
        if (enqueued_ || !source_.ready()) return;
        enqueued_ = true;
    }

    struct Job {
        WorldMapPageKey key;
        float dist2;
    };
    std::vector<Job> jobs;
    jobs.reserve(world_total_page_count());
    const float center_u = map_view[0];
    const float center_v = map_view[1];
    for (int level = kWorldLevelCount - 1; level >= 0; --level) {
        const auto& layout = kWorldLevels[level];
        const float page_u = static_cast<float>(1u << level) / 40.0f;
        const float page_v = static_cast<float>(1u << level) / 28.0f;
        for (std::uint32_t y = 0u; y < layout.rows; ++y) {
            for (std::uint32_t x = 0u; x < layout.cols; ++x) {
                float du = (static_cast<float>(x) + 0.5f) * page_u - center_u;
                du -= std::round(du);   // shortest arc across the dateline
                const float dv = (static_cast<float>(y) + 0.5f) * page_v - center_v;
                const auto pack_y = static_cast<std::int32_t>((layout.rows - 1u) - y);
                jobs.push_back({{static_cast<std::int32_t>(x), pack_y,
                                 static_cast<std::uint16_t>(level)},
                                du * du + dv * dv});
            }
        }
    }
    std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        if (a.key.level != b.key.level) return a.key.level > b.key.level;
        return a.dist2 < b.dist2;
    });

    {
        std::lock_guard lock(decode_mutex_);
        for (const auto& job : jobs) decode_requests_.push_back(job.key);
    }
    decode_cv_.notify_all();
}

std::uint32_t WorldMapPageStreamer::pop_decoded(std::uint32_t frame, std::uint32_t max_pages) {
    {
        std::lock_guard lock(decode_mutex_);
        while (!decode_results_.empty() && max_pages != 0u) {
            upload_window_.emplace_back(std::move(decode_results_.front()));
            decode_results_.pop_front();
            --max_pages;
        }
    }
    decode_cv_.notify_all();
    if (upload_window_.empty()) return 0u;

    // Frame-thread only state; the workers never touch the residency bitmaps.
    std::uint32_t taken = 0u;
    for (auto& result : upload_window_) {
        ++taken;
        const auto& key = result.first;
        if (key.level >= kWorldLevelCount) continue;
        const auto& layout = kWorldLevels[key.level];
        if (static_cast<std::uint32_t>(key.x) >= layout.cols ||
            static_cast<std::uint32_t>(key.y) >= layout.rows) continue;
        const auto index = world_page_index(key.level,
                                            static_cast<std::uint32_t>(key.x),
                                            static_cast<std::uint32_t>(key.y));
        if (resident_[key.level][index] != 0u) continue;
        resident_[key.level][index] = 1u;
        arrived_frame_[key.level][index] = frame;
        --remaining_;
    }
    return taken;
}

void WorldMapPageStreamer::clear_upload_window() noexcept {
    upload_window_.clear();
}

bool WorldMapPageStreamer::resident(std::uint32_t level, std::uint32_t x, std::uint32_t y) const noexcept {
    if (level >= kWorldLevelCount) return false;
    const auto& layout = kWorldLevels[level];
    if (x >= layout.cols || y >= layout.rows) return false;
    const auto chunk_y = (layout.rows - 1u) - y;
    return resident_[level][world_page_index(level, x, chunk_y)] != 0u;
}

std::uint32_t WorldMapPageStreamer::resident_count() const noexcept {
    std::uint32_t count = 0u;
    for (const auto& bitmap : resident_) {
        for (const auto value : bitmap) count += value;
    }
    return count;
}

float WorldMapPageStreamer::lod_fraction(std::array<float, 4> map_view, float screen_width_px) {
    if (screen_width_px <= 0.0f) return 0.0f;
    const float half_u = std::clamp(map_view[2], 1.0e-6f, 1.0f);
    const float page0_px = (1.0f / 40.0f) * screen_width_px / (2.0f * half_u);
    const float quad0_px = std::max(1.0e-6f, page0_px / 64.0f);
    const float raw_lod = -std::log2(quad0_px / 2.5f);
    return std::isfinite(raw_lod) ? std::clamp(raw_lod, 0.0f, 3.0f) : 0.0f;
}

void WorldMapPageStreamer::build_world_patches(std::array<float, 4> map_view,
                                               float screen_width_px,
                                               std::uint64_t frame,
                                               std::vector<WorldMapPatchGpu>& out) const {
    out.clear();
    const float center_u = map_view[0];
    const float center_v = map_view[1];
    const float half_u = std::clamp(map_view[2], 1.0e-6f, 1.0f);
    const float half_v = std::clamp(map_view[3], 1.0e-6f, 1.0f);

    const float lod = lod_fraction(map_view, screen_width_px);
    const int fine = static_cast<int>(std::floor(lod));
    const int coarse = std::min(fine + 1, static_cast<int>(kWorldLevelCount) - 1);
    const float blend = lod - static_cast<float>(fine);

    const auto& layout = kWorldLevels[fine];
    const auto& metadata = source_.metadata();
    const double span_x = metadata.bounds_world_m[2] - metadata.bounds_world_m[0];
    const double span_y = metadata.bounds_world_m[3] - metadata.bounds_world_m[1];
    if (span_x <= 0.0 || span_y <= 0.0) return;
    const double page_m = metadata.base_page_world_size_m * static_cast<double>(1u << fine);
    const float page_u = static_cast<float>(page_m / span_x);
    const float page_v = static_cast<float>(page_m / span_y);
    if (page_u <= 1.0e-6f || page_v <= 1.0e-6f) return;
    const float north_padding = static_cast<float>(layout.rows) * page_v - 1.0f;
    // x deliberately over-covers by a page in both directions so the wrap seam
    // always has a patch on each side; keys normalise back into the level.
    const int first_x = static_cast<int>(std::floor(center_u - half_u)) * static_cast<int>(layout.cols);
    const int last_x = (static_cast<int>(std::floor(center_u + half_u)) + 1) * static_cast<int>(layout.cols);
    const int first_y = std::max(0, static_cast<int>(std::floor((center_v - half_v + north_padding) / page_v)));
    const int last_y = std::min(static_cast<int>(layout.rows),
                                static_cast<int>(std::ceil((center_v + half_v + north_padding) / page_v)));

    for (int y = first_y; y < last_y; ++y) {
        for (int x = first_x; x < last_x; ++x) {
            const auto cols = static_cast<int>(layout.cols);
            const auto key_x = static_cast<std::uint32_t>(((x % cols) + cols) % cols);
            const float cycle = std::floor(static_cast<float>(x) / static_cast<float>(cols));
            const float u0 = cycle + static_cast<float>(key_x) * page_u;
            const float du = std::min(page_u, 1.0f - static_cast<float>(key_x) * page_u);
            if (u0 + du < center_u - half_u || u0 > center_u + half_u) continue;
            const auto chunk_y = static_cast<std::uint32_t>((layout.rows - 1) - y);

            // Fine level: if the page has not landed yet, fall back along the
            // mip chain to the nearest coarser resident ancestor. Because the
            // pyramid is one address space, the fallback needs no uv surgery:
            // sample_height01 is defined for every uv at every level.
            int level_a = fine;
            auto ancestor_x = key_x;
            auto ancestor_y = chunk_y;
            while (level_a < static_cast<int>(kWorldLevelCount) &&
                   resident_[level_a][world_page_index(level_a, ancestor_x, ancestor_y)] == 0u) {
                ++level_a;
                ancestor_x >>= 1;
                ancestor_y >>= 1;
            }
            if (level_a >= static_cast<int>(kWorldLevelCount)) continue;

            int level_b = level_a;
            float morph = 1.0f;
            if (level_a == fine && coarse != fine &&
                resident_[coarse][world_page_index(coarse, key_x >> 1, chunk_y >> 1)] != 0u) {
                level_b = coarse;
                morph = 1.0f - blend;
            }

            // Newly arrived pages fade in through the morph so slow zooms
            // never pop; once everything is resident this never triggers.
            const auto index = world_page_index(level_a, ancestor_x, ancestor_y);
            const float age = static_cast<float>(
                static_cast<double>(frame) - static_cast<double>(arrived_frame_[level_a][index])) *
                frame_dt_;
            if (age < kFadeSeconds && level_b != level_a)
                morph = std::min(morph, age / kFadeSeconds);

            const float raw_v0 = static_cast<float>(y) * page_v - north_padding;
            const float v0 = std::max(0.0f, raw_v0);
            const float dv = std::min(1.0f, raw_v0 + page_v) - v0;
            const auto patch = [&](float start, float width) {
                out.push_back({{start, v0, width, dv},
                               static_cast<std::uint32_t>(level_a),
                               static_cast<std::uint32_t>(level_b),
                               morph, 0.0f});
            };

            // Patches are shifted uniformly per-patch in world_map.vert via
            // patch_center_u, preventing intra-patch tearing across shortest-arc
            // boundaries. Emitting whole page patches preserves exact vertex grid
            // alignment across neighboring patches without seam cracks.
            patch(u0, du);
        }
    }
}

} // namespace thunder
