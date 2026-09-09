#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/io/RandomAccessFile.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

namespace thunder {

enum class WorldChunkType : std::uint16_t {
    Metadata = 1,
    ProvinceCoastBundle = 2,
    TerrainHeightPage = 3,
    AdjacencyOffsets = 4,
    AdjacencyNeighbors = 5,
    ProvinceDefinitions = 6,
    StringTable = 7,
    CountryDefinitions = 8,
    MarketDefinitions = 9,
    StateDefinitions = 10,
    BuildingDefinitions = 11,
    PopDefinitions = 12,
    SpatialMask = 13,
    PlacementCandidates = 14,
    SettlementAnchors = 15,
    ResourceAnchors = 16,
    TransportPolyline = 17,
    RiverPolyline = 18,
    LakeMask = 19,
    ArchitectureRegion = 20,
    HistoricalSetup = 21,
    ResourceDistribution = 22,
    AreaDefinitions = 23,
    TradeProvinceDefinitions = 24,
    LocationDefinitions = 25,
    MapLabelDefinitions = 26,
    CountryPresentation = 27,
    CartographicCoastPage = 28,
};

enum class WorldChunkCodec : std::uint8_t {
    Raw = 0,
    Zstd = 1,
};

enum class WorldChecksumCodec : std::uint8_t {
    Fnv1a64 = 0,
    Xxh3_64 = 1,
};

struct WorldChunkKey {
    WorldChunkType type = WorldChunkType::Metadata;
    std::uint16_t level = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t variant = 0;

    friend constexpr bool operator==(const WorldChunkKey&, const WorldChunkKey&) = default;
    friend constexpr auto operator<=>(const WorldChunkKey&, const WorldChunkKey&) = default;
};

struct WorldChunkKeyHash {
    [[nodiscard]] std::size_t operator()(const WorldChunkKey& key) const noexcept;
};

struct WorldChunkIndexEntry {
    WorldChunkKey key{};
    WorldChunkCodec codec = WorldChunkCodec::Raw;
    WorldChecksumCodec checksum_codec = WorldChecksumCodec::Fnv1a64;
    std::uint64_t file_offset = 0;
    std::uint32_t stored_bytes = 0;
    std::uint32_t raw_bytes = 0;
    std::uint64_t checksum = 0;
};

struct WorldPackStats {
    std::uint32_t chunk_count = 0;
    std::uint64_t raw_bytes = 0;
    std::uint64_t stored_bytes = 0;
    std::uint64_t index_bytes = 0;
    std::uint64_t build_hash = 0;
    // The x axis of page keys wraps when this flag is set. It is stored in
    // the reserved header word so existing 64-byte packs remain readable.
    bool horizontal_wrap = false;

    [[nodiscard]] double compression_ratio() const noexcept {
        return raw_bytes == 0 ? 1.0 : static_cast<double>(stored_bytes) / static_cast<double>(raw_bytes);
    }
};

struct WorldPackWriteOptions {
    int zstd_level = 3;
    // Small wins are not worth decode cost and codec metadata. Keep raw unless
    // compression saves at least this fraction of payload bytes.
    double minimum_savings_fraction = 0.05;
    std::uint32_t data_alignment = 64;
    bool horizontal_wrap = false;
};

class WorldPackWriter {
public:
    WorldPackWriter() = default;
    ~WorldPackWriter();

    WorldPackWriter(const WorldPackWriter&) = delete;
    WorldPackWriter& operator=(const WorldPackWriter&) = delete;

    void open(const std::filesystem::path& path, WorldPackWriteOptions options = {});
    void append(WorldChunkKey key, std::span<const std::byte> payload);
    WorldPackStats finalize();

    [[nodiscard]] bool is_open() const noexcept { return stream_.is_open(); }
    [[nodiscard]] const WorldPackStats& stats() const noexcept { return stats_; }

private:
    void write_placeholder_header();
    void align_data();

    std::fstream stream_;
    std::filesystem::path path_;
    WorldPackWriteOptions options_{};
    std::vector<WorldChunkIndexEntry> index_;
    std::unordered_set<WorldChunkKey, WorldChunkKeyHash> keys_;
    std::vector<std::byte> compression_scratch_;
    WorldPackStats stats_{};
    bool finalized_ = false;
};

class WorldPackDecodeScratch;

class WorldPackReader {
public:
    void open(const std::filesystem::path& path);
    void close() noexcept;

    [[nodiscard]] std::span<const WorldChunkIndexEntry> index() const noexcept { return index_; }
    [[nodiscard]] std::optional<WorldChunkIndexEntry> find(WorldChunkKey key) const noexcept;
    [[nodiscard]] bool contains(WorldChunkKey key) const noexcept { return find(key).has_value(); }
    [[nodiscard]] std::vector<std::byte> read(WorldChunkKey key) const;
    void read_stored(const WorldChunkIndexEntry& entry, std::span<std::byte> destination) const;
    [[nodiscard]] const WorldPackStats& stats() const noexcept { return stats_; }

private:
    std::filesystem::path path_;
    RandomAccessFile file_;
    std::vector<WorldChunkIndexEntry> index_;
    std::unordered_set<WorldChunkKey, WorldChunkKeyHash> keys_;
    std::vector<std::byte> compression_scratch_;
    WorldPackStats stats_{};
};


// One scratch object per streaming worker. Capacity is reused across page reads,
// removing allocator churn from camera-driven world streaming.
class WorldPackDecodeScratch {
public:
    explicit WorldPackDecodeScratch(const WorldPackReader& pack);
    ~WorldPackDecodeScratch();
    WorldPackDecodeScratch(const WorldPackDecodeScratch&) = delete;
    WorldPackDecodeScratch& operator=(const WorldPackDecodeScratch&) = delete;
    [[nodiscard]] std::span<const std::byte> read(WorldChunkKey key);
    [[nodiscard]] std::size_t stored_capacity() const noexcept { return stored_.capacity(); }
    [[nodiscard]] std::size_t decoded_capacity() const noexcept { return decoded_.capacity(); }

private:
    const WorldPackReader* pack_ = nullptr;
    std::vector<std::byte> stored_;
    std::vector<std::byte> decoded_;
#if defined(THUNDER_HAS_ZSTD)
    struct ZstdContext;
    std::unique_ptr<ZstdContext> zstd_;
#endif
};

[[nodiscard]] std::string_view world_chunk_type_name(WorldChunkType type) noexcept;
[[nodiscard]] std::optional<WorldChunkType> parse_world_chunk_type(std::string_view text) noexcept;

} // namespace thunder
