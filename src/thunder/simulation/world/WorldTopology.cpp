#include "thunder/simulation/world/WorldTopology.hpp"
#include "thunder/simulation/world/WorldMapHierarchyWire.hpp"
#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"

#include <bit>
#include <stdexcept>
#include <string>
#include <utility>

namespace thunder {
namespace {

constexpr std::uint32_t state_wire_magic = 0x32535453u;   // STS2
constexpr std::uint32_t country_wire_magic = 0x32544e43u; // CNT2, tag-only
constexpr std::uint32_t province_wire_magic = 0x32565250u; // PRV2
constexpr std::uint32_t history_wire_magic = 0x31534948u; // HIS1
constexpr std::uint32_t area_wire_magic = 0x32415241u; // ARA2
constexpr std::uint32_t trade_province_wire_magic = 0x3250564du; // MVP2
constexpr std::uint32_t location_wire_magic = 0x32434f4cu; // LOC2

class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : bytes(data) {}

    void need(std::size_t size) const {
        if (position > bytes.size() || size > bytes.size() - position)
            throw std::runtime_error("truncated world definition chunk at offset " +
                                     std::to_string(position) + " of " +
                                     std::to_string(bytes.size()));
    }
    std::uint8_t u8() {
        need(1u);
        return std::to_integer<std::uint8_t>(bytes[position++]);
    }
    std::uint16_t u16() {
        std::uint16_t value = 0u;
        for (unsigned shift = 0u; shift < 16u; shift += 8u)
            value = static_cast<std::uint16_t>(value |
                                               (static_cast<std::uint16_t>(u8()) << shift));
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
    double f64() { return std::bit_cast<double>(u64()); }
    std::string str() {
        const auto size = u16();
        need(size);
        std::string value(reinterpret_cast<const char*>(bytes.data() + position), size);
        position += size;
        return value;
    }
    [[nodiscard]] bool done() const noexcept { return position == bytes.size(); }

private:
    std::span<const std::byte> bytes;
    std::size_t position = 0u;
};

template <class Id>
Id read_id(Reader& reader) {
    return Id{static_cast<typename Id::rep_type>(reader.u32())};
}

constexpr WorldChunkKey static_key(WorldChunkType type) { return {type, 0u, 0, 0, 0u}; }

std::vector<std::byte> required_chunk(const WorldPackReader& pack, WorldChunkType type) {
    const auto key = static_key(type);
    if (!pack.contains(key)) {
        throw std::runtime_error("required world definition chunk missing: " +
                                 std::string{world_chunk_type_name(type)});
    }
    return pack.read(key);
}

} // namespace

WorldTopology WorldTopology::load(const WorldPackReader& pack) {
    WorldTopology result;
    result.world_pack_hash = pack.stats().build_hash;

    if (const auto metadata_key = static_key(WorldChunkType::Metadata);
        pack.contains(metadata_key)) {
        result.metadata = parse_world_pack_metadata(pack.read(metadata_key));
        if (result.metadata.horizontal_wrap != pack.stats().horizontal_wrap)
            throw std::runtime_error("world metadata/header horizontal-wrap mismatch");
    }

    {
        const auto bytes = required_chunk(pack, WorldChunkType::CountryDefinitions);
        Reader reader(bytes);
        const auto first = reader.u32();
        const bool tag_only = first == country_wire_magic;
        const auto count = tag_only ? reader.u32() : first;
        if (count > 65'534u) throw std::runtime_error("too many countries");
        result.countries.reserve(count);
        for (std::uint32_t index = 0u; index < count; ++index) {
            WorldPackCountryRecord country;
            country.tag = reader.str();
            if (!tag_only) {
                country.population = reader.f64();
                country.gdp = reader.f64();
                country.treasury = reader.f64();
                country.tax_rate = reader.f64();
            }
            result.countries.push_back(std::move(country));
        }
        if (!reader.done()) throw std::runtime_error("trailing country definition bytes");
    }

    {
        const auto bytes = required_chunk(pack, WorldChunkType::MarketDefinitions);
        Reader reader(bytes);
        const auto count = reader.u32();
        if (count > 65'534u) throw std::runtime_error("too many markets");
        result.market_owners.reserve(count);
        for (std::uint32_t index = 0u; index < count; ++index)
            result.market_owners.push_back(read_id<CountryId>(reader));
        if (!reader.done()) throw std::runtime_error("trailing market definition bytes");
    }

    {
        const auto bytes = required_chunk(pack, WorldChunkType::StateDefinitions);
        Reader reader(bytes);
        const auto first = reader.u32();
        const bool versioned = first == state_wire_magic;
        const auto count = versioned ? reader.u32() : first;
        if (count > 65'534u) throw std::runtime_error("too many states");
        result.geography.reserve_states(count);
        for (std::uint32_t index = 0u; index < count; ++index) {
            StateInit state{reader.str(), read_id<CountryId>(reader),
                            read_id<MarketId>(reader), read_id<ProvinceId>(reader)};
            if (versioned) {
                state.resistance_ppm = reader.u32();
                if (reader.u8() != 0u || reader.u8() != 0u || reader.u16() != 0u)
                    throw std::runtime_error("state definition reserved bits are non-zero");
            }
            result.geography.create_state(std::move(state));
        }
        if (!reader.done()) throw std::runtime_error("trailing state definition bytes");
    }

    {
        const auto bytes = required_chunk(pack, WorldChunkType::ProvinceDefinitions);
        Reader reader(bytes);
        const auto first = reader.u32();
        const bool versioned = first == province_wire_magic;
        const auto count = versioned ? reader.u32() : first;
        if (count > 65'534u) throw std::runtime_error("too many provinces");
        result.geography.reserve_provinces(count);
        for (std::uint32_t index = 0u; index < count; ++index) {
            ProvinceInit province{reader.str(), read_id<StateId>(reader),
                                  read_id<CountryId>(reader), read_id<MarketId>(reader),
                                  reader.f64(), reader.f64(), reader.u32()};
            if (versioned) {
                const auto kind = reader.u8();
                const auto flags = reader.u8();
                if (kind > static_cast<std::uint8_t>(ProvinceKind::Lake) ||
                    (flags & 0xfcu) != 0u || reader.u16() != 0u)
                    throw std::runtime_error("invalid province definition flags");
                province.kind = static_cast<ProvinceKind>(kind);
                province.coastal = (flags & 1u) != 0u;
                province.impassable = (flags & 2u) != 0u;
            }
            result.geography.create_province(std::move(province));
        }
        if (!reader.done()) throw std::runtime_error("trailing province definition bytes");
    }

    if (result.metadata.valid()) {
        if (result.metadata.country_count != result.countries.size() ||
            result.metadata.state_count != result.geography.state_count() ||
            result.metadata.province_count != result.geography.province_count())
            throw std::runtime_error("world metadata definition counts do not match bootstrap");
        std::uint32_t sea_count = 0u;
        std::uint32_t lake_count = 0u;
        for (std::size_t index = 0u; index < result.geography.province_count(); ++index) {
            const auto province = ProvinceId{static_cast<ProvinceId::rep_type>(index)};
            if (result.geography.province_kind(province) == ProvinceKind::Sea) ++sea_count;
            if (result.geography.province_kind(province) == ProvinceKind::Lake) ++lake_count;
        }
        if (result.metadata.sea_count != sea_count || result.metadata.lake_count != lake_count)
            throw std::runtime_error("world metadata water counts do not match bootstrap");
    }

    if (const auto history_key = static_key(WorldChunkType::HistoricalSetup);
        pack.contains(history_key)) {
        const auto history_bytes = pack.read(history_key);
        Reader reader(history_bytes);
        if (reader.u32() != history_wire_magic)
            throw std::runtime_error("invalid historical setup chunk magic");
        const auto state_count = reader.u32();
        if (state_count > result.geography.state_count())
            throw std::runtime_error("historical state count exceeds definitions");
        for (std::uint32_t index = 0u; index < state_count; ++index) {
            const auto state = read_id<StateId>(reader);
            if (!state.valid() || state.value() >= result.geography.state_count())
                throw std::runtime_error("historical state reference invalid");
            result.geography.set_state_owner(state, read_id<CountryId>(reader));
            result.geography.set_state_market(state, read_id<MarketId>(reader));
            result.geography.set_state_capital(state, read_id<ProvinceId>(reader));
        }
        const auto province_count = reader.u32();
        if (province_count > result.geography.province_count())
            throw std::runtime_error("historical province count exceeds definitions");
        for (std::uint32_t index = 0u; index < province_count; ++index) {
            const auto province = read_id<ProvinceId>(reader);
            if (!province.valid() || province.value() >= result.geography.province_count())
                throw std::runtime_error("historical province reference invalid");
            result.geography.set_province_owner(province, read_id<CountryId>(reader));
            result.geography.set_province_market(province, read_id<MarketId>(reader));
        }
        const auto sea_start_count = reader.u32();
        if (sea_start_count > result.geography.province_count())
            throw std::runtime_error("historical sea start count exceeds provinces");
        result.sea_starts.reserve(sea_start_count);
        for (std::uint32_t index = 0u; index < sea_start_count; ++index) {
            const auto province = read_id<ProvinceId>(reader);
            if (!province.valid() || province.value() >= result.geography.province_count() ||
                result.geography.province_kind(province) != ProvinceKind::Sea)
                throw std::runtime_error("historical sea start is not a sea province");
            result.sea_starts.push_back(province);
        }
        if (!reader.done()) throw std::runtime_error("trailing historical setup bytes");
    }

    // Market definitions carry identity only; historical ownership provides a
    // deterministic primary owner for the existing market API.
    for (std::size_t index = 0u; index < result.geography.province_count(); ++index) {
        const auto province = ProvinceId{static_cast<ProvinceId::rep_type>(index)};
        const auto market = result.geography.province_market(province);
        const auto owner = result.geography.province_owner(province);
        if (market.valid() && owner.valid() && market.value() < result.market_owners.size() &&
            !result.market_owners[market.value()].valid())
            result.market_owners[market.value()] = owner;
    }

    if (!result.geography.validate(result.countries.size(), result.market_owners.size()))
        throw std::runtime_error("invalid geography in world pack");
    {
        const auto area_key = static_key(WorldChunkType::AreaDefinitions);
        const auto province_key = static_key(WorldChunkType::TradeProvinceDefinitions);
        const auto location_key = static_key(WorldChunkType::LocationDefinitions);
        const bool any_hierarchy = pack.contains(area_key) || pack.contains(province_key) || pack.contains(location_key);
        if (any_hierarchy) {
            if (!pack.contains(area_key) || !pack.contains(province_key) || !pack.contains(location_key))
                throw std::runtime_error("incomplete Area/Province/Location hierarchy chunks");
            std::vector<MapAreaRecord> areas;
            {
                const auto bytes = pack.read(area_key);
                Reader reader(bytes);
                if (reader.u32() != area_wire_magic) throw std::runtime_error("invalid area definition chunk magic");
                const auto count = reader.u32();
                if (count == 0u || count > 65'534u) throw std::runtime_error("invalid area definition count");
                areas.reserve(count);
                for (std::uint32_t index = 0u; index < count; ++index) areas.push_back({reader.str()});
                if (!reader.done()) throw std::runtime_error("trailing area definition bytes");
            }
            std::vector<MapProvinceRecord> provinces;
            {
                const auto bytes = pack.read(province_key);
                Reader reader(bytes);
                if (reader.u32() != trade_province_wire_magic) throw std::runtime_error("invalid trade-province definition chunk magic");
                const auto count = reader.u32();
                if (count == 0u || count > 65'534u) throw std::runtime_error("invalid trade-province definition count");
                provinces.reserve(count);
                for (std::uint32_t index = 0u; index < count; ++index)
                    provinces.push_back({reader.str(), read_id<AreaId>(reader)});
                if (!reader.done()) throw std::runtime_error("trailing trade-province definition bytes");
            }
            std::vector<MapLocationRecord> locations;
            {
                const auto bytes = pack.read(location_key);
                Reader reader(bytes);
                if (reader.u32() != location_wire_magic) throw std::runtime_error("invalid location definition chunk magic");
                const auto count = reader.u32();
                if (count == 0u || count > ProvinceRasterPage::max_province_count) throw std::runtime_error("invalid location definition count");
                locations.reserve(count);
                for (std::uint32_t index = 0u; index < count; ++index) {
                    MapLocationRecord location;
                    location.key = reader.str();
                    location.province = read_id<TradeProvinceId>(reader);
                    location.area = read_id<AreaId>(reader);
                    location.raster_province = read_id<ProvinceId>(reader);
                    location.center_x_m = reader.f64();
                    location.center_y_m = reader.f64();
                    location.area_km2 = reader.u32();
                    const auto terrain = reader.u8();
                    const auto flags = reader.u8();
                    const auto constraint_flags = reader.u16();
                    if (terrain >= static_cast<std::uint8_t>(TerrainType::Count) || (flags & 0xfeu) != 0u)
                        throw std::runtime_error("invalid location definition flags");
                    location.terrain = static_cast<TerrainType>(terrain);
                    location.compound_terrain = to_compound_terrain(location.terrain);
                    location.coastal = (flags & 1u) != 0u;
                    location.constraint_flags = constraint_flags;
                    locations.push_back(std::move(location));
                }
                if (!reader.done()) throw std::runtime_error("trailing location definition bytes");
            }
            result.map_hierarchy.set(std::move(areas), std::move(provinces), std::move(locations));
        } else {
            result.map_hierarchy.build_default(result.geography);
        }
        if (!result.map_hierarchy.validate(result.geography.province_count()))
            throw std::runtime_error("invalid Area/Province/Location hierarchy");
    }
    result.scope_index.rebuild(result.countries.size(), result.geography);
    result.state_regions.rebuild(result.geography);
    result.spatial_placement.load_from_worldpack(
        pack, result.geography.province_count());
    if (result.metadata.valid() &&
        result.spatial_placement.anchor_count() < result.metadata.authored_hub_count)
        throw std::runtime_error("world metadata authored hub count exceeds placement data");

    const auto offsets_key = static_key(WorldChunkType::AdjacencyOffsets);
    const auto neighbors_key = static_key(WorldChunkType::AdjacencyNeighbors);
    if (pack.contains(offsets_key) || pack.contains(neighbors_key)) {
        if (!pack.contains(offsets_key) || !pack.contains(neighbors_key))
            throw std::runtime_error("incomplete adjacency chunks");
        const auto offset_bytes = pack.read(offsets_key);
        const auto neighbor_bytes = pack.read(neighbors_key);
        Reader offsets_reader(offset_bytes);
        const auto offset_count = offsets_reader.u32();
        if (offset_count > result.geography.province_count() + 1u)
            throw std::runtime_error("adjacency offsets exceed province count");
        std::vector<std::uint32_t> offsets;
        offsets.reserve(offset_count);
        for (std::uint32_t index = 0u; index < offset_count; ++index)
            offsets.push_back(offsets_reader.u32());
        if (!offsets_reader.done()) throw std::runtime_error("trailing adjacency offset bytes");

        Reader neighbors_reader(neighbor_bytes);
        const auto neighbor_count = neighbors_reader.u32();
        if (neighbor_count > 20'000'000u)
            throw std::runtime_error("adjacency neighbors exceed safety cap");
        std::vector<ProvinceNeighbor> neighbors;
        neighbors.reserve(neighbor_count);
        for (std::uint32_t index = 0u; index < neighbor_count; ++index)
            neighbors.push_back({neighbors_reader.u32(), neighbors_reader.u16(),
                                 neighbors_reader.u16()});
        if (!neighbors_reader.done()) throw std::runtime_error("trailing adjacency neighbor bytes");
        if (offsets.size() != result.geography.province_count() + 1u)
            throw std::runtime_error("adjacency province count mismatch");
        result.adjacency.load_csr(offsets, neighbors);
        if (!result.adjacency.is_symmetric())
            throw std::runtime_error("world adjacency is not symmetric");
    }

    result.static_layers = load_world_static_layers(
        pack, result.geography.province_count(), result.geography.state_count());
    result.map_labels.load_from_worldpack(pack);
    const auto presentation_key = static_key(WorldChunkType::CountryPresentation);
    if (pack.contains(presentation_key)) {
        const auto bytes = pack.read(presentation_key);
        Reader reader(bytes);
        if (reader.u32() != 0x314c5043u || reader.u32() != result.countries.size())
            throw std::runtime_error("country presentation does not match country identities");
        for (auto& country : result.countries) {
            for (auto& channel : country.map_color) channel = reader.u8();
            country.authored_map_color = true;
        }
        if (!reader.done()) throw std::runtime_error("trailing country presentation bytes");
    }
    return result;
}

} // namespace thunder
