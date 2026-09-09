#include "thunder/content/worldpack/WorldPack.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

#if defined(THUNDER_HAS_ZSTD)
#include <zstd.h>
#endif
#if defined(THUNDER_HAS_XXHASH)
#include <xxhash.h>
#endif

namespace thunder {

std::size_t WorldChunkKeyHash::operator()(const WorldChunkKey& key) const noexcept {
    std::uint64_t h = static_cast<std::uint16_t>(key.type);
    h = (h * 0x9E3779B185EBCA87ull) ^ key.level;
    h = (h * 0xC2B2AE3D27D4EB4Full) ^ static_cast<std::uint32_t>(key.x);
    h = (h * 0x165667B19E3779F9ull) ^ static_cast<std::uint32_t>(key.y);
    h = (h * 0x85EBCA77C2B2AE63ull) ^ key.variant;
    h ^= h >> 33u;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33u;
    return static_cast<std::size_t>(h);
}

namespace {

constexpr std::array<char, 8> magic{{'T','H','U','N','D','R','W','P'}};
constexpr std::uint32_t format_version = 2;
constexpr std::uint32_t header_bytes = 64;
constexpr std::uint32_t index_entry_bytes = 48;
constexpr std::uint32_t world_pack_flag_horizontal_wrap = 1u << 0u;

// Largest uncompressed chunk we are willing to allocate. Real chunks are far
// smaller; this only stops a malformed pack from claiming ~4 GiB.
constexpr std::uint32_t kMaxChunkRawBytes = 64u * 1024u * 1024u;

template <typename T, bool IsEnum = std::is_enum_v<T>>
struct WireType { using type = T; };
template <typename T>
struct WireType<T, true> { using type = std::underlying_type_t<T>; };
template <typename T>
using WireTypeT = typename WireType<T>::type;

template <typename T>
void write_le(std::ostream& out, T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using U = WireTypeT<T>;
    U u = static_cast<U>(value);
    if constexpr (std::endian::native == std::endian::big) {
        if constexpr (sizeof(U) == 2) u = static_cast<U>(std::byteswap(static_cast<std::uint16_t>(u)));
        if constexpr (sizeof(U) == 4) u = static_cast<U>(std::byteswap(static_cast<std::uint32_t>(u)));
        if constexpr (sizeof(U) == 8) u = static_cast<U>(std::byteswap(static_cast<std::uint64_t>(u)));
    }
    out.write(reinterpret_cast<const char*>(&u), static_cast<std::streamsize>(sizeof(U)));
    if (!out) throw std::runtime_error("failed writing Thunder world pack");
}

template <typename T>
T read_le(std::istream& in) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using U = WireTypeT<T>;
    U u{};
    in.read(reinterpret_cast<char*>(&u), static_cast<std::streamsize>(sizeof(U)));
    if (!in) throw std::runtime_error("truncated Thunder world pack");
    if constexpr (std::endian::native == std::endian::big) {
        if constexpr (sizeof(U) == 2) u = static_cast<U>(std::byteswap(static_cast<std::uint16_t>(u)));
        if constexpr (sizeof(U) == 4) u = static_cast<U>(std::byteswap(static_cast<std::uint32_t>(u)));
        if constexpr (sizeof(U) == 8) u = static_cast<U>(std::byteswap(static_cast<std::uint64_t>(u)));
    }
    return static_cast<T>(u);
}

WorldChecksumCodec preferred_checksum_codec() noexcept {
#if defined(THUNDER_HAS_XXHASH)
    return WorldChecksumCodec::Xxh3_64;
#else
    return WorldChecksumCodec::Fnv1a64;
#endif
}

std::uint64_t checksum(std::span<const std::byte> payload, WorldChecksumCodec codec) {
    if (codec == WorldChecksumCodec::Xxh3_64) {
#if defined(THUNDER_HAS_XXHASH)
        return XXH3_64bits(payload.data(), payload.size());
#else
        throw std::runtime_error("world pack requires XXH3 checksum support but Thunder was built without xxHash");
#endif
    }
    if (codec == WorldChecksumCodec::Fnv1a64) {
        Fnv1a64 h;
        h.add_bytes(payload);
        return h.value();
    }
    throw std::runtime_error("unknown world checksum codec");
}

std::span<const std::byte> maybe_compress(std::span<const std::byte> input,
                                           const WorldPackWriteOptions& options,
                                           WorldChunkCodec& codec,
                                           std::vector<std::byte>& scratch) {
    codec = WorldChunkCodec::Raw;
#if defined(THUNDER_HAS_ZSTD)
    if (input.size() >= 256u) {
        const std::size_t bound = ZSTD_compressBound(input.size());
        scratch.resize(bound);
        const auto written = ZSTD_compress(scratch.data(), bound, input.data(), input.size(), options.zstd_level);
        if (!ZSTD_isError(written)) {
            const double max_stored = static_cast<double>(input.size()) * (1.0 - options.minimum_savings_fraction);
            if (static_cast<double>(written) <= max_stored) {
                codec = WorldChunkCodec::Zstd;
                return {scratch.data(), written};
            }
        }
    }
#else
    (void)options;
    (void)scratch;
#endif
    return input;
}

void write_key(std::ostream& out, const WorldChunkKey& key) {
    write_le(out, key.type);
    write_le(out, key.level);
    write_le(out, key.x);
    write_le(out, key.y);
    write_le(out, key.variant);
}

WorldChunkKey read_key(std::istream& in) {
    WorldChunkKey key;
    key.type = read_le<WorldChunkType>(in);
    key.level = read_le<std::uint16_t>(in);
    key.x = read_le<std::int32_t>(in);
    key.y = read_le<std::int32_t>(in);
    key.variant = read_le<std::uint32_t>(in);
    return key;
}

template <typename T>
void hash_le(Fnv1a64& hash, T value) noexcept {
    using U = WireTypeT<T>;
    using UU = std::make_unsigned_t<U>;
    UU u = static_cast<UU>(static_cast<U>(value));
    std::array<std::byte, sizeof(U)> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>((u >> (i * 8u)) & static_cast<UU>(0xffu));
    }
    hash.add_bytes(bytes);
}

std::uint64_t compute_build_hash(std::span<const WorldChunkIndexEntry> index,
                                 bool horizontal_wrap = false) noexcept {
    Fnv1a64 hash;
    // Preserve the hash of pre-wrap packs (the default is false), while making
    // the coordinate contract part of the identity for global wrapped packs.
    if (horizontal_wrap) hash.add(world_pack_flag_horizontal_wrap);
    for (const auto& entry : index) {
        hash_le(hash, entry.key.type);
        hash_le(hash, entry.key.level);
        hash_le(hash, entry.key.x);
        hash_le(hash, entry.key.y);
        hash_le(hash, entry.key.variant);
        hash_le(hash, entry.raw_bytes);
        hash_le(hash, entry.checksum_codec);
        hash_le(hash, entry.checksum);
    }
    return hash.value();
}

void write_header(std::ostream& out, std::uint32_t chunk_count, std::uint64_t index_offset,
                  std::uint64_t index_size, const WorldPackStats& stats) {
    out.seekp(0, std::ios::beg);
    out.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    write_le(out, format_version);
    write_le(out, header_bytes);
    write_le(out, chunk_count);
    write_le(out, stats.horizontal_wrap ? world_pack_flag_horizontal_wrap : 0u);
    write_le(out, index_offset);
    write_le(out, index_size);
    write_le(out, stats.raw_bytes);
    write_le(out, stats.stored_bytes);
    write_le(out, stats.build_hash);
    const auto pos = static_cast<std::uint64_t>(out.tellp());
    if (pos > header_bytes) throw std::runtime_error("Thunder world pack header overflow");
    std::array<char, header_bytes> zeros{};
    out.write(zeros.data(), static_cast<std::streamsize>(header_bytes - pos));
}


[[nodiscard]] bool valid_chunk_type(WorldChunkType type) noexcept {
    switch (type) {
        case WorldChunkType::Metadata:
        case WorldChunkType::ProvinceCoastBundle:
        case WorldChunkType::TerrainHeightPage:
        case WorldChunkType::AdjacencyOffsets:
        case WorldChunkType::AdjacencyNeighbors:
        case WorldChunkType::ProvinceDefinitions:
        case WorldChunkType::StringTable:
        case WorldChunkType::CountryDefinitions:
        case WorldChunkType::MarketDefinitions:
        case WorldChunkType::StateDefinitions:
        case WorldChunkType::BuildingDefinitions:
        case WorldChunkType::PopDefinitions:
        case WorldChunkType::SpatialMask:
        case WorldChunkType::PlacementCandidates:
        case WorldChunkType::SettlementAnchors:
        case WorldChunkType::ResourceAnchors:
        case WorldChunkType::TransportPolyline:
        case WorldChunkType::RiverPolyline:
        case WorldChunkType::LakeMask:
        case WorldChunkType::ArchitectureRegion:
        case WorldChunkType::HistoricalSetup:
        case WorldChunkType::ResourceDistribution:
        case WorldChunkType::AreaDefinitions:
        case WorldChunkType::TradeProvinceDefinitions:
        case WorldChunkType::LocationDefinitions:
        case WorldChunkType::MapLabelDefinitions:
        case WorldChunkType::CountryPresentation:
        case WorldChunkType::CartographicCoastPage:
            return true;
    }
    return false;
}

[[nodiscard]] bool valid_chunk_codec(WorldChunkCodec codec) noexcept {
    return codec == WorldChunkCodec::Raw || codec == WorldChunkCodec::Zstd;
}

[[nodiscard]] bool valid_checksum_codec(WorldChecksumCodec codec) noexcept {
    return codec == WorldChecksumCodec::Fnv1a64 || codec == WorldChecksumCodec::Xxh3_64;
}

void read_header(std::istream& in, std::uint32_t& chunk_count, std::uint64_t& index_offset,
                 std::uint64_t& index_size, WorldPackStats& stats) {
    std::array<char, 8> got{};
    in.read(got.data(), static_cast<std::streamsize>(got.size()));
    if (!in || got != magic) throw std::runtime_error("not a Thunder .thunderworld file");
    const auto version = read_le<std::uint32_t>(in);
    const auto hbytes = read_le<std::uint32_t>(in);
    if (version != format_version || hbytes != header_bytes) throw std::runtime_error("unsupported Thunder world pack version");
    chunk_count = read_le<std::uint32_t>(in);
    const auto flags = read_le<std::uint32_t>(in);
    if ((flags & ~world_pack_flag_horizontal_wrap) != 0u)
        throw std::runtime_error("unknown Thunder world pack header flags");
    stats.horizontal_wrap = (flags & world_pack_flag_horizontal_wrap) != 0u;
    index_offset = read_le<std::uint64_t>(in);
    index_size = read_le<std::uint64_t>(in);
    stats.raw_bytes = read_le<std::uint64_t>(in);
    stats.stored_bytes = read_le<std::uint64_t>(in);
    stats.build_hash = read_le<std::uint64_t>(in);
    stats.chunk_count = chunk_count;
    stats.index_bytes = index_size;
}

} // namespace

WorldPackWriter::~WorldPackWriter() {
    if (stream_.is_open()) stream_.close();
}

void WorldPackWriter::open(const std::filesystem::path& path, WorldPackWriteOptions options) {
    if (stream_.is_open()) throw std::logic_error("WorldPackWriter already open");
    if (options.data_alignment == 0u || (options.data_alignment & (options.data_alignment - 1u)) != 0u)
        throw std::invalid_argument("world pack alignment must be power of two");
    path_ = path;
    options_ = options;
    index_.clear();
    keys_.clear();
    keys_.reserve(4096u);
    stats_ = {};
    stats_.horizontal_wrap = options_.horizontal_wrap;
    finalized_ = false;
    stream_.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!stream_) throw std::runtime_error("failed to create world pack: " + path.string());
    write_placeholder_header();
}

void WorldPackWriter::write_placeholder_header() {
    std::array<char, header_bytes> zeros{};
    stream_.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    if (!stream_) throw std::runtime_error("failed reserving world pack header");
}

void WorldPackWriter::align_data() {
    const auto pos = static_cast<std::uint64_t>(stream_.tellp());
    const auto alignment = static_cast<std::uint64_t>(options_.data_alignment);
    const auto aligned = (pos + alignment - 1u) & ~(alignment - 1u);
    const auto padding = aligned - pos;
    if (padding == 0u) return;
    static constexpr std::array<char, 4096> zeros{};
    std::uint64_t left = padding;
    while (left > 0u) {
        const auto n = static_cast<std::streamsize>(std::min<std::uint64_t>(left, zeros.size()));
        stream_.write(zeros.data(), n);
        left -= static_cast<std::uint64_t>(n);
    }
}

void WorldPackWriter::append(WorldChunkKey key, std::span<const std::byte> payload) {
    if (!stream_.is_open() || finalized_) throw std::logic_error("WorldPackWriter is not appendable");
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("world chunk exceeds 4 GiB");

    // Duplicate detection must stay O(1) as global packs can contain hundreds of
    // thousands of pages. An O(N^2) linear scan here would make offline builds scale badly.
    if (!keys_.insert(key).second) throw std::invalid_argument("duplicate world chunk key");

    WorldChunkCodec codec = WorldChunkCodec::Raw;
    const auto stored = maybe_compress(payload, options_, codec, compression_scratch_);
    if (stored.size() > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("stored world chunk exceeds 4 GiB");

    align_data();
    const auto offset = static_cast<std::uint64_t>(stream_.tellp());
    stream_.write(reinterpret_cast<const char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
    if (!stream_) throw std::runtime_error("failed writing world chunk payload");

    const auto checksum_codec = preferred_checksum_codec();
    index_.push_back({key, codec, checksum_codec, offset, static_cast<std::uint32_t>(stored.size()),
                      static_cast<std::uint32_t>(payload.size()), checksum(payload, checksum_codec)});
    ++stats_.chunk_count;
    stats_.raw_bytes += payload.size();
    stats_.stored_bytes += stored.size();
}

WorldPackStats WorldPackWriter::finalize() {
    if (!stream_.is_open() || finalized_) throw std::logic_error("WorldPackWriter cannot finalize");
    std::sort(index_.begin(), index_.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    stats_.build_hash = compute_build_hash(index_, stats_.horizontal_wrap);

    align_data();
    const auto index_offset = static_cast<std::uint64_t>(stream_.tellp());
    for (const auto& entry : index_) {
        write_key(stream_, entry.key);
        write_le(stream_, entry.codec);
        write_le(stream_, entry.checksum_codec);
        write_le(stream_, std::uint16_t{0});
        write_le(stream_, entry.file_offset);
        write_le(stream_, entry.stored_bytes);
        write_le(stream_, entry.raw_bytes);
        write_le(stream_, entry.checksum);
        write_le(stream_, std::uint32_t{0});
    }
    const auto index_end = static_cast<std::uint64_t>(stream_.tellp());
    const auto index_size = index_end - index_offset;
    stats_.index_bytes = index_size;
    if (index_size != static_cast<std::uint64_t>(index_.size()) * index_entry_bytes)
        throw std::runtime_error("world pack index layout mismatch");

    write_header(stream_, static_cast<std::uint32_t>(index_.size()), index_offset, index_size, stats_);
    stream_.flush();
    if (!stream_) throw std::runtime_error("failed finalizing world pack");
    finalized_ = true;
    // A finalized pack is immutable. Release the handle immediately so tools
    // can atomically rename/delete it on Windows without waiting for teardown.
    stream_.close();
    return stats_;
}

void WorldPackReader::open(const std::filesystem::path& path) {
    file_.open(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("failed to open world pack index: " + path.string());
    std::uint32_t chunk_count = 0;
    std::uint64_t index_offset = 0;
    std::uint64_t index_size = 0;
    read_header(in, chunk_count, index_offset, index_size, stats_);
    if (index_size != static_cast<std::uint64_t>(chunk_count) * index_entry_bytes)
        throw std::runtime_error("invalid world pack index size");

    const auto file_size = file_.size();
    if (index_offset < header_bytes || index_offset > file_size || index_size > file_size - index_offset)
        throw std::runtime_error("world pack index outside file");

    in.seekg(static_cast<std::streamoff>(index_offset), std::ios::beg);
    index_.clear();
    index_.reserve(chunk_count);
    std::uint64_t raw_total = 0u;
    std::uint64_t stored_total = 0u;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        WorldChunkIndexEntry entry;
        entry.key = read_key(in);
        entry.codec = read_le<WorldChunkCodec>(in);
        entry.checksum_codec = read_le<WorldChecksumCodec>(in);
        (void)read_le<std::uint16_t>(in);
        entry.file_offset = read_le<std::uint64_t>(in);
        entry.stored_bytes = read_le<std::uint32_t>(in);
        entry.raw_bytes = read_le<std::uint32_t>(in);
        entry.checksum = read_le<std::uint64_t>(in);
        (void)read_le<std::uint32_t>(in);

        if (!valid_chunk_type(entry.key.type)) throw std::runtime_error("invalid world chunk type");
        if (!valid_chunk_codec(entry.codec)) throw std::runtime_error("invalid world chunk codec");
        if (!valid_checksum_codec(entry.checksum_codec)) throw std::runtime_error("invalid world checksum codec");
        if (entry.file_offset < header_bytes || entry.file_offset > index_offset
            || entry.stored_bytes > index_offset - entry.file_offset)
            throw std::runtime_error("world chunk payload overlaps header or index");
        if (entry.codec == WorldChunkCodec::Raw && entry.stored_bytes != entry.raw_bytes)
            throw std::runtime_error("raw world chunk size metadata mismatch");
        if (raw_total > std::numeric_limits<std::uint64_t>::max() - entry.raw_bytes
            || stored_total > std::numeric_limits<std::uint64_t>::max() - entry.stored_bytes)
            throw std::runtime_error("world pack byte statistics overflow");
        raw_total += entry.raw_bytes;
        stored_total += entry.stored_bytes;
        index_.push_back(entry);
    }
    if (!std::is_sorted(index_.begin(), index_.end(), [](const auto& a, const auto& b) { return a.key < b.key; }))
        throw std::runtime_error("world pack index is not sorted");
    for (std::size_t i = 1; i < index_.size(); ++i)
        if (index_[i - 1u].key == index_[i].key)
            throw std::runtime_error("world pack index contains duplicate chunk keys");
    if (raw_total != stats_.raw_bytes || stored_total != stats_.stored_bytes)
        throw std::runtime_error("world pack header byte statistics mismatch");
    if (compute_build_hash(index_, stats_.horizontal_wrap) != stats_.build_hash)
        throw std::runtime_error("world pack content build hash mismatch");
    path_ = path;
}

void WorldPackReader::close() noexcept {
    file_.close();
    path_.clear();
    index_.clear();
    keys_.clear();
    compression_scratch_.clear();
    stats_ = {};
}

std::optional<WorldChunkIndexEntry> WorldPackReader::find(WorldChunkKey key) const noexcept {
    const auto it = std::lower_bound(index_.begin(), index_.end(), key,
        [](const WorldChunkIndexEntry& entry, const WorldChunkKey& k) { return entry.key < k; });
    if (it == index_.end() || !(it->key == key)) return std::nullopt;
    return *it;
}

std::vector<std::byte> WorldPackReader::read(WorldChunkKey key) const {
    WorldPackDecodeScratch scratch{*this};
    const auto bytes = scratch.read(key);
    return {bytes.begin(), bytes.end()};
}

void WorldPackReader::read_stored(const WorldChunkIndexEntry& entry, std::span<std::byte> destination) const {
    if (destination.size() != entry.stored_bytes) throw std::invalid_argument("stored buffer size mismatch");
    file_.read_at(entry.file_offset, destination);
}

#if defined(THUNDER_HAS_ZSTD)
struct WorldPackDecodeScratch::ZstdContext {
    ZSTD_DCtx* value = ZSTD_createDCtx();
    ~ZstdContext() { if (value) ZSTD_freeDCtx(value); }
};
#endif

WorldPackDecodeScratch::WorldPackDecodeScratch(const WorldPackReader& pack) : pack_(&pack) {
#if defined(THUNDER_HAS_ZSTD)
    zstd_ = std::make_unique<ZstdContext>();
    if (!zstd_->value) throw std::bad_alloc{};
#endif
}
WorldPackDecodeScratch::~WorldPackDecodeScratch() = default;

std::span<const std::byte> WorldPackDecodeScratch::read(WorldChunkKey key) {
    if (!pack_) throw std::logic_error("WorldPackDecodeScratch has no pack");
    const auto entry_opt = pack_->find(key);
    if (!entry_opt) throw std::out_of_range("world chunk not found");
    const auto entry = *entry_opt;
    stored_.resize(entry.stored_bytes);
    pack_->read_stored(entry, stored_);

    if (entry.codec == WorldChunkCodec::Raw) {
        if (stored_.size() != entry.raw_bytes) throw std::runtime_error("raw world chunk size mismatch");
        if (checksum(stored_, entry.checksum_codec) != entry.checksum) throw std::runtime_error("world chunk checksum mismatch");
        return stored_;
    }
    if (entry.codec == WorldChunkCodec::Zstd) {
#if defined(THUNDER_HAS_ZSTD)
        // The declared uncompressed size is attacker-controllable (a hostile
        // pack can recompute build_hash to match). Cap it before allocating so
        // a 4 GiB claim cannot be turned into a 4 GiB allocation; the checksum
        // below is only verified after the buffer exists.
        if (entry.raw_bytes > kMaxChunkRawBytes)
            throw std::runtime_error("world chunk raw size exceeds safety cap");
        decoded_.resize(entry.raw_bytes);
        const auto decoded = ZSTD_decompressDCtx(zstd_->value, decoded_.data(), decoded_.size(), stored_.data(), stored_.size());
        if (ZSTD_isError(decoded) || decoded != decoded_.size()) throw std::runtime_error("failed decompressing world chunk");
        if (checksum(decoded_, entry.checksum_codec) != entry.checksum) throw std::runtime_error("world chunk checksum mismatch");
        return decoded_;
#else
        throw std::runtime_error("world pack requires Zstd but Thunder was built without it");
#endif
    }
    throw std::runtime_error("unknown world chunk codec");
}

std::string_view world_chunk_type_name(WorldChunkType type) noexcept {
    switch (type) {
        case WorldChunkType::Metadata: return "metadata";
        case WorldChunkType::ProvinceCoastBundle: return "province_coast";
        case WorldChunkType::TerrainHeightPage: return "height";
        case WorldChunkType::AdjacencyOffsets: return "adjacency_offsets";
        case WorldChunkType::AdjacencyNeighbors: return "adjacency_neighbors";
        case WorldChunkType::ProvinceDefinitions: return "province_definitions";
        case WorldChunkType::StringTable: return "strings";
        case WorldChunkType::CountryDefinitions: return "country_definitions";
        case WorldChunkType::MarketDefinitions: return "market_definitions";
        case WorldChunkType::StateDefinitions: return "state_definitions";
        case WorldChunkType::BuildingDefinitions: return "building_definitions";
        case WorldChunkType::PopDefinitions: return "pop_definitions";
        case WorldChunkType::SpatialMask: return "spatial_mask";
        case WorldChunkType::PlacementCandidates: return "placement_candidates";
        case WorldChunkType::SettlementAnchors: return "settlement_anchors";
        case WorldChunkType::ResourceAnchors: return "resource_anchors";
        case WorldChunkType::TransportPolyline: return "transport_polyline";
        case WorldChunkType::RiverPolyline: return "river_polyline";
        case WorldChunkType::LakeMask: return "lake_mask";
        case WorldChunkType::ArchitectureRegion: return "architecture_region";
        case WorldChunkType::HistoricalSetup: return "historical_setup";
        case WorldChunkType::ResourceDistribution: return "resource_distribution";
        case WorldChunkType::AreaDefinitions: return "area_definitions";
        case WorldChunkType::TradeProvinceDefinitions: return "trade_province_definitions";
        case WorldChunkType::LocationDefinitions: return "location_definitions";
        case WorldChunkType::MapLabelDefinitions: return "map_label_definitions";
        case WorldChunkType::CountryPresentation: return "country_presentation";
        case WorldChunkType::CartographicCoastPage: return "cartographic_coast";
    }
    return "unknown";
}

std::optional<WorldChunkType> parse_world_chunk_type(std::string_view text) noexcept {
    for (const auto type : {WorldChunkType::Metadata, WorldChunkType::ProvinceCoastBundle,
                            WorldChunkType::TerrainHeightPage, WorldChunkType::AdjacencyOffsets,
                            WorldChunkType::AdjacencyNeighbors, WorldChunkType::ProvinceDefinitions,
                            WorldChunkType::StringTable, WorldChunkType::CountryDefinitions,
                            WorldChunkType::MarketDefinitions, WorldChunkType::StateDefinitions,
                            WorldChunkType::BuildingDefinitions, WorldChunkType::PopDefinitions,
                            WorldChunkType::SpatialMask, WorldChunkType::PlacementCandidates,
                            WorldChunkType::SettlementAnchors, WorldChunkType::ResourceAnchors,
                            WorldChunkType::TransportPolyline, WorldChunkType::RiverPolyline,
                            WorldChunkType::LakeMask, WorldChunkType::ArchitectureRegion,
                            WorldChunkType::HistoricalSetup, WorldChunkType::ResourceDistribution,
                            WorldChunkType::AreaDefinitions, WorldChunkType::TradeProvinceDefinitions,
                            WorldChunkType::LocationDefinitions, WorldChunkType::MapLabelDefinitions,
                            WorldChunkType::CountryPresentation, WorldChunkType::CartographicCoastPage}) {
        if (world_chunk_type_name(type) == text ||
            (type == WorldChunkType::TerrainHeightPage && text == "terrain_height")) return type;
    }
    return std::nullopt;
}

} // namespace thunder
