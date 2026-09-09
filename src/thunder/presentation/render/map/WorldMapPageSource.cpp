#include "thunder/presentation/render/map/WorldMapPageSource.hpp"

#include "thunder/presentation/render/map/PoliticalMapPageBundle.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace thunder {
namespace {

constexpr WorldChunkKey static_key(WorldChunkType type) { return {type, 0u, 0, 0, 0u}; }

} // namespace

WorldMapPageSource::~WorldMapPageSource() {
    close();
}

void WorldMapPageSource::close() noexcept {
    scratch_.reset();
    pack_.close();
    metadata_ = {};
    ready_ = false;
}

bool WorldMapPageSource::open(const std::filesystem::path& path, std::string& diagnostic) {
    close();
    diagnostic.clear();
    try {
        pack_.open(path);
        const auto metadata_key = static_key(WorldChunkType::Metadata);
        if (!pack_.contains(metadata_key)) throw std::runtime_error("world pack has no metadata chunk");
        metadata_ = parse_world_pack_metadata(pack_.read(metadata_key));
        if (!metadata_.valid()) throw std::runtime_error("world pack metadata is invalid");
        if (metadata_.horizontal_wrap != pack_.stats().horizontal_wrap)
            throw std::runtime_error("world pack metadata/header horizontal-wrap mismatch");
        if (metadata_.province_count > ProvinceRasterPage::max_province_count)
            throw std::runtime_error("world pack province count exceeds R16 page capacity");
        if (metadata_.page_size != ProvinceRasterPage::samples_per_side)
            throw std::runtime_error("world pack page size does not match the map page contract");
        if (metadata_.base_page_world_size_m <= 0.0)
            throw std::runtime_error("world pack has no positive base page world size");

        const bool has_chart = std::any_of(pack_.index().begin(), pack_.index().end(), [](const auto& entry) {
            return entry.key.type == WorldChunkType::CartographicCoastPage;
        });
        for (std::uint32_t level = 0u; level < metadata_.clip_levels; ++level) {
            const auto count_x = metadata_.page_count_x(level);
            const auto count_y = metadata_.page_count_y(level);
            for (std::uint32_t y = 0u; y < count_y; ++y) {
                for (std::uint32_t x = 0u; x < count_x; ++x) {
                    const auto page_key = WorldChunkKey{WorldChunkType::ProvinceCoastBundle,
                                                        static_cast<std::uint16_t>(level),
                                                        static_cast<std::int32_t>(x),
                                                        static_cast<std::int32_t>(y), 0u};
                    const auto height_key = WorldChunkKey{WorldChunkType::TerrainHeightPage,
                                                          static_cast<std::uint16_t>(level),
                                                          static_cast<std::int32_t>(x),
                                                          static_cast<std::int32_t>(y), 0u};
                    const auto lake_key = WorldChunkKey{WorldChunkType::LakeMask,
                                                        static_cast<std::uint16_t>(level),
                                                        static_cast<std::int32_t>(x),
                                                        static_cast<std::int32_t>(y), 0u};
                    const auto spatial_key = WorldChunkKey{WorldChunkType::SpatialMask,
                                                            static_cast<std::uint16_t>(level),
                                                            static_cast<std::int32_t>(x),
                                                            static_cast<std::int32_t>(y), 0u};
                    if (!pack_.contains(page_key) || !pack_.contains(height_key) ||
                        !pack_.contains(lake_key) || !pack_.contains(spatial_key))
                        throw std::runtime_error("world pack clipmap has an incomplete page family");
                    if (has_chart && !pack_.contains({WorldChunkType::CartographicCoastPage,
                        static_cast<std::uint16_t>(level), static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), 0u}))
                        throw std::runtime_error("world pack cartographic coast family is incomplete");
                }
            }
        }
        scratch_ = std::make_unique<WorldPackDecodeScratch>(pack_);
        ready_ = true;
        return true;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        close();
        return false;
    }
}

bool WorldMapPageSource::decode(WorldMapPageKey key, WorldMapPage& out) noexcept {
    out.province.fill(ProvinceRasterPage::water);
    out.coast.fill(static_cast<std::int16_t>(-32767));
    out.cartographic_coast.fill(static_cast<std::int16_t>(-32767));
    out.height.fill(static_cast<std::uint16_t>(24'000u));
    out.lake_mask.fill(0u);
    out.spatial_mask.fill(0u);
    out.has_height = false;
    out.has_lake_mask = false;
    out.has_spatial_mask = false;
    if (!ready_ || !scratch_ || key.level >= metadata_.clip_levels) return false;
    const auto page_count_x = static_cast<std::int32_t>(metadata_.page_count_x(key.level));
    const auto page_count_y = static_cast<std::int32_t>(metadata_.page_count_y(key.level));
    if (page_count_x <= 0 || page_count_y <= 0 || key.y < 0 || key.y >= page_count_y) return false;
    if (metadata_.horizontal_wrap) {
        key.x %= page_count_x;
        if (key.x < 0) key.x += page_count_x;
    } else if (key.x < 0 || key.x >= page_count_x) {
        return false;
    }

    try {
        const auto bundle_key = WorldChunkKey{WorldChunkType::ProvinceCoastBundle,
                                              key.level, key.x, key.y, 0u};
        const auto bundle_bytes = scratch_->read(bundle_key);
        PoliticalMapPageBundleView bundle{bundle_bytes};
        ProvinceRasterPage province;
        CoastDistancePage coast;
        bundle.decode_province(province);
        bundle.decode_coast(coast);
        std::copy(province.samples().begin(), province.samples().end(), out.province.begin());
        std::copy(coast.samples().begin(), coast.samples().end(), out.coast.begin());
        const auto chart_key = WorldChunkKey{WorldChunkType::CartographicCoastPage,
                                             key.level, key.x, key.y, 0u};
        if (pack_.contains(chart_key)) {
            const auto chart_bytes = scratch_->read(chart_key);
            if (chart_bytes.size() != sizeof(out.cartographic_coast)) return false;
            std::memcpy(out.cartographic_coast.data(), chart_bytes.data(), chart_bytes.size());
        } else {
            // Legacy packs retain a conservative narrow chart effect. Mark
            // saturated physical samples as far away, never as a false coast.
            for (std::size_t i = 0; i < out.coast.size(); ++i) {
                const auto raw = out.coast[i];
                out.cartographic_coast[i] = raw >= 31990 ? 32767 :
                    raw <= -31990 ? -32767 : static_cast<std::int16_t>(raw / 64);
            }
        }

        const auto height_key = WorldChunkKey{WorldChunkType::TerrainHeightPage,
                                              key.level, key.x, key.y, 0u};
        const auto height_bytes = scratch_->read(height_key);
        if (height_bytes.size() == TerrainHeightPage::sample_count * sizeof(std::uint16_t)) {
            std::memcpy(out.height.data(), height_bytes.data(), height_bytes.size());
            out.has_height = true;
        }
        const auto lake_key = WorldChunkKey{WorldChunkType::LakeMask,
                                            key.level, key.x, key.y, 0u};
        const auto lake_bytes = scratch_->read(lake_key);
        if (lake_bytes.size() == ProvinceRasterPage::sample_count * sizeof(std::uint8_t)) {
            std::memcpy(out.lake_mask.data(), lake_bytes.data(), lake_bytes.size());
            if (std::any_of(out.lake_mask.begin(), out.lake_mask.end(),
                            [](std::uint8_t value) { return value > 1u; }))
                return false;
            out.has_lake_mask = true;
        }
        const auto spatial_key = WorldChunkKey{WorldChunkType::SpatialMask,
                                               key.level, key.x, key.y, 0u};
        const auto spatial_bytes = scratch_->read(spatial_key);
        if (spatial_bytes.size() == ProvinceRasterPage::sample_count * sizeof(std::uint8_t)) {
            std::memcpy(out.spatial_mask.data(), spatial_bytes.data(), spatial_bytes.size());
            if (std::any_of(out.spatial_mask.begin(), out.spatial_mask.end(),
                            [](std::uint8_t value) { return value > 1u; }))
                return false;
            out.has_spatial_mask = true;
        }
        return out.has_height && out.has_lake_mask && out.has_spatial_mask;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace thunder
