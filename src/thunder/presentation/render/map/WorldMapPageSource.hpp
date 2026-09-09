#pragma once

#include "thunder/presentation/render/map/WorldMapPage.hpp"
#include "thunder/presentation/render/map/WorldMapPageKey.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/content/worldpack/WorldPackMetadata.hpp"

#include <filesystem>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace thunder {

// Backend-neutral source for the streamable map page contract. It owns the
// indexed pack reader and reusable decode scratch; Vulkan only consumes the
// decoded payload and remains responsible for images, staging and barriers.
class WorldMapPageSource {
public:
    WorldMapPageSource() = default;
    ~WorldMapPageSource();
    WorldMapPageSource(const WorldMapPageSource&) = delete;
    WorldMapPageSource& operator=(const WorldMapPageSource&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& path, std::string& diagnostic);
    void close() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const WorldPackMetadata& metadata() const noexcept { return metadata_; }
    [[nodiscard]] const WorldPackStats& stats() const noexcept { return pack_.stats(); }
    [[nodiscard]] const WorldPackReader& reader() const noexcept { return pack_; }

    // Missing/corrupt pages are reported as false after resetting `out` to a
    // safe water/sea-level payload, so callers cannot display stale residency.
    [[nodiscard]] bool decode(WorldMapPageKey key, WorldMapPage& out) noexcept;

private:
    WorldPackReader pack_;
    std::unique_ptr<WorldPackDecodeScratch> scratch_;
    WorldPackMetadata metadata_{};
    bool ready_ = false;
};

} // namespace thunder
