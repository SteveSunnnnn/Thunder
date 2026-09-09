#include "thunder/simulation/world/WorldMapHierarchyWire.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {

constexpr std::uint32_t area_magic = 0x32415241u;       // ARA2
constexpr std::uint32_t province_magic = 0x3250564du;   // MVP2
constexpr std::uint32_t location_magic = 0x32434f4cu;   // LOC2

class Writer {
public:
    void u8(std::uint8_t value) { bytes.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) { for (unsigned shift = 0u; shift < 16u; shift += 8u) u8(static_cast<std::uint8_t>(value >> shift)); }
    void u32(std::uint32_t value) { for (unsigned shift = 0u; shift < 32u; shift += 8u) u8(static_cast<std::uint8_t>(value >> shift)); }
    void u64(std::uint64_t value) { for (unsigned shift = 0u; shift < 64u; shift += 8u) u8(static_cast<std::uint8_t>(value >> shift)); }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void str(std::string_view value) {
        if (value.size() > std::numeric_limits<std::uint16_t>::max()) throw std::invalid_argument("map hierarchy key too long");
        u16(static_cast<std::uint16_t>(value.size()));
        const auto* begin = reinterpret_cast<const std::byte*>(value.data());
        bytes.insert(bytes.end(), begin, begin + value.size());
    }
    std::vector<std::byte> bytes;
};

template <class Id>
void write_id(Writer& writer, Id id) { writer.u32(id.value()); }

} // namespace

std::vector<std::byte> WorldMapHierarchyWire::areas(std::span<const MapAreaRecord> records) {
    Writer writer;
    writer.u32(area_magic);
    writer.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) writer.str(record.key);
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldMapHierarchyWire::provinces(std::span<const MapProvinceRecord> records) {
    Writer writer;
    writer.u32(province_magic);
    writer.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) { writer.str(record.key); write_id(writer, record.area); }
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldMapHierarchyWire::locations(std::span<const MapLocationRecord> records) {
    Writer writer;
    writer.u32(location_magic);
    writer.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) {
        writer.str(record.key);
        write_id(writer, record.province);
        write_id(writer, record.area);
        write_id(writer, record.raster_province);
        writer.f64(record.center_x_m);
        writer.f64(record.center_y_m);
        writer.u32(record.area_km2);
        writer.u8(static_cast<std::uint8_t>(record.terrain));
        writer.u8(static_cast<std::uint8_t>(record.coastal));
        writer.u16(record.constraint_flags);
    }
    return std::move(writer.bytes);
}

} // namespace thunder
