#include "thunder/presentation/render/map/WorldStaticLayerSource.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace thunder {
namespace {

constexpr std::uint32_t river_magic = 0x31564952u;     // RIV1
constexpr std::uint32_t transport_magic = 0x31525450u; // PTR1
constexpr std::uint32_t max_polyline_count = 1'000'000u;
constexpr std::uint32_t max_polyline_points = 4'000'000u;

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    void need(std::size_t count) const {
        if (position_ > bytes_.size() || count > bytes_.size() - position_)
            throw std::runtime_error("truncated world polyline chunk");
    }

    std::uint32_t u32() {
        need(4u);
        std::uint32_t value = 0u;
        for (unsigned shift = 0u; shift < 32u; shift += 8u)
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[position_++])) << shift;
        return value;
    }

    std::uint64_t u64() {
        need(8u);
        std::uint64_t value = 0u;
        for (unsigned shift = 0u; shift < 64u; shift += 8u)
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[position_++])) << shift;
        return value;
    }

    double f64() { return std::bit_cast<double>(u64()); }
    [[nodiscard]] bool done() const noexcept { return position_ == bytes_.size(); }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0u;
};

} // namespace

WorldStaticLayerSource::WorldStaticLayerSource(const WorldPackReader& pack)
    : pack_(&pack), scratch_(std::make_unique<WorldPackDecodeScratch>(pack)) {}

WorldStaticLayerSource::~WorldStaticLayerSource() = default;

bool WorldStaticLayerSource::decode(WorldChunkKey key, WorldPolylineChunk& out) const noexcept {
    out.key = key;
    out.lines.clear();
    if (pack_ == nullptr || scratch_ == nullptr ||
        key.level != 0u ||
        (key.type != WorldChunkType::RiverPolyline &&
         key.type != WorldChunkType::TransportPolyline)) return false;
    try {
        const auto payload = scratch_->read(key);
        Reader reader(payload);
        const auto expected_magic = key.type == WorldChunkType::RiverPolyline
            ? river_magic : transport_magic;
        if (reader.u32() != expected_magic) return false;
        const auto line_count = reader.u32();
        if (line_count > max_polyline_count) return false;
        out.lines.reserve(line_count);
        std::size_t point_count = 0u;
        for (std::uint32_t line_index = 0u; line_index < line_count; ++line_index) {
            const auto count = reader.u32();
            if (count < 2u || count > max_polyline_points ||
                point_count > max_polyline_points - count) return false;
            point_count += count;
            WorldPolyline line;
            line.points.reserve(count);
            for (std::uint32_t point_index = 0u; point_index < count; ++point_index) {
                const double x = reader.f64();
                const double y = reader.f64();
                if (!std::isfinite(x) || !std::isfinite(y)) return false;
                line.points.push_back({x, y});
            }
            out.lines.push_back(std::move(line));
        }
        return reader.done();
    } catch (...) {
        out.key = key;
        out.lines.clear();
        return false;
    }
}

} // namespace thunder
