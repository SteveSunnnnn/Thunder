#include "thunder/simulation/world/WorldMapLabels.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace thunder {
namespace {

constexpr std::uint32_t label_magic = 0x314c424cu; // LBL1
constexpr std::uint32_t max_labels = 200'000u;
constexpr std::uint16_t max_spine_points = 64u;

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::uint8_t u8() {
        require(1u);
        return std::to_integer<std::uint8_t>(bytes_[position_++]);
    }
    std::uint16_t u16() {
        std::uint16_t value = 0u;
        for (unsigned shift = 0u; shift < 16u; shift += 8u)
            value = static_cast<std::uint16_t>(value | static_cast<std::uint16_t>(u8()) << shift);
        return value;
    }
    std::uint32_t u32() {
        std::uint32_t value = 0u;
        for (unsigned shift = 0u; shift < 32u; shift += 8u)
            value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value = 0u;
        for (unsigned shift = 0u; shift < 64u; shift += 8u)
            value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }
    float f32() { return std::bit_cast<float>(u32()); }
    double f64() { return std::bit_cast<double>(u64()); }
    std::string text() {
        const auto size = u16();
        require(size);
        std::string value(reinterpret_cast<const char*>(bytes_.data() + position_), size);
        position_ += size;
        return value;
    }
    [[nodiscard]] bool done() const noexcept { return position_ == bytes_.size(); }

private:
    void require(std::size_t count) const {
        if (position_ > bytes_.size() || count > bytes_.size() - position_)
            throw std::runtime_error("truncated map-label definition chunk");
    }
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0u;
};

} // namespace

void WorldMapLabels::load_from_worldpack(const WorldPackReader& pack) {
    clear();
    const WorldChunkKey key{WorldChunkType::MapLabelDefinitions, 0u, 0, 0, 0u};
    if (!pack.contains(key)) return;
    const auto bytes = pack.read(key);
    Reader reader{bytes};
    if (reader.u32() != label_magic) throw std::runtime_error("invalid map-label chunk magic");
    const auto count = reader.u32();
    if (count > max_labels) throw std::runtime_error("map-label count exceeds safety cap");
    records_.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        WorldMapLabelRecord record;
        const auto kind = reader.u8();
        record.priority = reader.u8();
        const auto point_count = reader.u16();
        record.minimum_zoom = reader.f32();
        record.maximum_zoom = reader.f32();
        record.geographic_area_km2 = reader.f64();
        record.key = reader.text();
        record.text = reader.text();
        if (kind >= static_cast<std::uint8_t>(WorldMapLabelKind::Count) ||
            point_count < 2u || point_count > max_spine_points)
            throw std::runtime_error("invalid map-label kind or spine count");
        record.kind = static_cast<WorldMapLabelKind>(kind);
        record.spine.reserve(point_count);
        for (std::uint16_t point = 0; point < point_count; ++point)
            record.spine.push_back({reader.f32(), reader.f32()});
        records_.push_back(std::move(record));
    }
    if (!reader.done()) throw std::runtime_error("trailing map-label definition bytes");
    if (!validate()) throw std::runtime_error("map-label definition validation failed");
}

bool WorldMapLabels::validate() const noexcept {
    std::unordered_set<std::string_view> keys;
    keys.reserve(records_.size() * 2u);
    for (const auto& record : records_) {
        if (record.key.empty() || record.text.empty() ||
            record.kind >= WorldMapLabelKind::Count || record.spine.size() < 2u ||
            record.spine.size() > max_spine_points ||
            !std::isfinite(record.minimum_zoom) || !std::isfinite(record.maximum_zoom) ||
            record.minimum_zoom < 0.0f || record.maximum_zoom > 1.0f ||
            record.minimum_zoom > record.maximum_zoom ||
            !std::isfinite(record.geographic_area_km2) || record.geographic_area_km2 < 0.0 ||
            !keys.insert(record.key).second)
            return false;
        for (const auto point : record.spine) {
            if (!std::isfinite(point.u) || !std::isfinite(point.v) ||
                point.u < 0.0f || point.u > 1.0f || point.v < 0.0f || point.v > 1.0f)
                return false;
        }
    }
    return true;
}

std::size_t WorldMapLabels::memory_bytes() const noexcept {
    std::size_t bytes = records_.capacity() * sizeof(WorldMapLabelRecord);
    for (const auto& record : records_)
        bytes += record.key.capacity() + record.text.capacity() +
                 record.spine.capacity() * sizeof(WorldMapLabelPoint);
    return bytes;
}

} // namespace thunder
