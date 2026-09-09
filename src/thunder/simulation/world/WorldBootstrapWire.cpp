#include "thunder/simulation/world/WorldBootstrapWire.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {

constexpr std::uint32_t state_wire_magic = 0x32535453u; // STS2
constexpr std::uint32_t province_wire_magic = 0x32565250u; // PRV2
constexpr std::uint32_t history_wire_magic = 0x31534948u; // HIS1
constexpr std::uint32_t country_wire_magic = 0x32544e43u; // CNT2, tag-only

class Writer {
public:
    void u8(std::uint8_t value) { bytes.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) { for (unsigned shift = 0; shift < 16; shift += 8) u8(static_cast<std::uint8_t>(value >> shift)); }
    void u32(std::uint32_t value) { for (unsigned shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>(value >> shift)); }
    void u64(std::uint64_t value) { for (unsigned shift = 0; shift < 64; shift += 8) u8(static_cast<std::uint8_t>(value >> shift)); }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void str(std::string_view value) {
        if (value.size() > std::numeric_limits<std::uint16_t>::max())
            throw std::invalid_argument("world definition key too large");
        u16(static_cast<std::uint16_t>(value.size()));
        const auto* begin = reinterpret_cast<const std::byte*>(value.data());
        bytes.insert(bytes.end(), begin, begin + value.size());
    }
    std::vector<std::byte> bytes;
};

template <class Id>
void write_id(Writer& writer, Id value) { writer.u32(value.value()); }

} // namespace

std::vector<std::byte> WorldBootstrapWire::countries(std::span<const CountryInit> records) {
    Writer writer;
    writer.u32(country_wire_magic);
    writer.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) writer.str(record.tag);
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldBootstrapWire::markets(std::span<const MarketBootstrapRecord> records) {
    Writer writer;
    writer.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) write_id(writer, record.owner);
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldBootstrapWire::states(std::span<const StateInit> records) {
    Writer writer;
    writer.u32(state_wire_magic);
    writer.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) {
        writer.str(record.key);
        write_id(writer, record.owner);
        write_id(writer, record.market);
        write_id(writer, record.capital);
        writer.u32(record.resistance_ppm);
        writer.u8(0u);
        writer.u8(0u);
        writer.u16(0u);
    }
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldBootstrapWire::provinces(std::span<const ProvinceInit> records) {
    Writer writer;
    writer.u32(province_wire_magic);
    writer.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) {
        writer.str(record.key);
        write_id(writer, record.state);
        write_id(writer, record.owner);
        write_id(writer, record.market);
        writer.f64(record.center_x_m);
        writer.f64(record.center_y_m);
        writer.u32(record.area_km2);
        writer.u8(static_cast<std::uint8_t>(record.kind));
        writer.u8(static_cast<std::uint8_t>((record.coastal ? 1u : 0u) |
                                             (record.impassable ? 2u : 0u)));
        writer.u16(0u);
    }
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldBootstrapWire::historical_setup(
    std::span<const HistoricalStateInit> states,
    std::span<const HistoricalProvinceInit> provinces,
    std::span<const ProvinceId> sea_starts) {
    Writer writer;
    writer.u32(history_wire_magic);
    writer.u32(static_cast<std::uint32_t>(states.size()));
    for (const auto& state : states) {
        write_id(writer, state.state);
        write_id(writer, state.owner);
        write_id(writer, state.market);
        write_id(writer, state.capital);
    }
    writer.u32(static_cast<std::uint32_t>(provinces.size()));
    for (const auto& province : provinces) {
        write_id(writer, province.province);
        write_id(writer, province.owner);
        write_id(writer, province.market);
    }
    writer.u32(static_cast<std::uint32_t>(sea_starts.size()));
    for (const auto province : sea_starts) write_id(writer, province);
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldBootstrapWire::adjacency_offsets(std::span<const std::uint32_t> offsets) {
    Writer writer;
    writer.u32(static_cast<std::uint32_t>(offsets.size()));
    for (const auto offset : offsets) writer.u32(offset);
    return std::move(writer.bytes);
}

std::vector<std::byte> WorldBootstrapWire::adjacency_neighbors(std::span<const ProvinceNeighbor> neighbors) {
    Writer writer;
    writer.u32(static_cast<std::uint32_t>(neighbors.size()));
    for (const auto& neighbor : neighbors) {
        writer.u32(neighbor.province);
        writer.u16(neighbor.flags);
        writer.u16(neighbor.base_cost_q8);
    }
    return std::move(writer.bytes);
}

} // namespace thunder
