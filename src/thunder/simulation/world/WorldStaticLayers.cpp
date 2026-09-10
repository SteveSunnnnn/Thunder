#include "thunder/simulation/world/WorldStaticLayers.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

namespace thunder {
namespace {

constexpr std::uint32_t architecture_magic = 0x31435241u; // ARC1
constexpr std::uint32_t resource_magic = 0x31534452u;    // RDS1
constexpr std::uint32_t max_static_records = 5'000'000u;

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    void need(std::size_t count) const {
        if (position_ > bytes_.size() || count > bytes_.size() - position_)
            throw std::runtime_error("truncated world static-layer chunk");
    }

    std::uint32_t u32() {
        need(4u);
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32u; shift += 8u)
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[position_++])) << shift;
        return value;
    }

    std::uint64_t u64() {
        need(8u);
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64u; shift += 8u)
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[position_++])) << shift;
        return value;
    }

    double f64() { return std::bit_cast<double>(u64()); }

    [[nodiscard]] bool done() const noexcept { return position_ == bytes_.size(); }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};


std::vector<ArchitectureRegionRecord> decode_architecture(
    const WorldPackReader& pack,
    const WorldChunkIndexEntry& entry,
    std::size_t province_count,
    std::size_t state_count) {
    const auto payload = pack.read(entry.key);
    Reader reader(payload);
    if (reader.u32() != architecture_magic)
        throw std::runtime_error("invalid architecture-region chunk magic");
    const auto count = reader.u32();
    if (count > max_static_records)
        throw std::runtime_error("architecture-region records exceed safety cap");
    std::vector<ArchitectureRegionRecord> records;
    records.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        ArchitectureRegionRecord record{
            ProvinceId{reader.u32()}, StateId{reader.u32()}, reader.u64(), reader.u64()};
        if (!record.province.valid() ||
            static_cast<std::size_t>(record.province.value()) >= province_count ||
            !record.state.valid() ||
            static_cast<std::size_t>(record.state.value()) >= state_count ||
            record.region_key == 0u || record.family_key == 0u)
            throw std::runtime_error("architecture-region reference or key is invalid");
        records.push_back(record);
    }
    if (!reader.done())
        throw std::runtime_error("trailing architecture-region bytes");
    return records;
}

std::vector<ResourceDistributionRecord> decode_resources(
    const WorldPackReader& pack,
    const WorldChunkIndexEntry& entry,
    std::size_t state_count) {
    const auto payload = pack.read(entry.key);
    Reader reader(payload);
    if (reader.u32() != resource_magic)
        throw std::runtime_error("invalid resource-distribution chunk magic");
    const auto state_record_count = reader.u32();
    if (state_record_count > state_count)
        throw std::runtime_error("resource-distribution state count exceeds world states");
    std::vector<ResourceDistributionRecord> records;
    std::vector<std::uint8_t> state_seen(state_count, 0u);
    for (std::uint32_t state_index = 0; state_index < state_record_count; ++state_index) {
        const StateId state{reader.u32()};
        const auto resource_count = reader.u32();
        if (!state.valid() || static_cast<std::size_t>(state.value()) >= state_count ||
            resource_count > max_static_records)
            throw std::runtime_error("resource-distribution state record is invalid");
        if (state_seen[state.value()] != 0u)
            throw std::runtime_error("resource-distribution state is duplicated");
        state_seen[state.value()] = 1u;
        if (records.size() > max_static_records - resource_count)
            throw std::runtime_error("resource-distribution records exceed safety cap");
        for (std::uint32_t resource_index = 0; resource_index < resource_count; ++resource_index) {
            const auto key = reader.u64();
            const auto capacity = reader.f64();
            if (key == 0u || !std::isfinite(capacity) || capacity < 0.0)
                throw std::runtime_error("resource-distribution value is invalid");
            records.push_back({state, key, capacity});
        }
    }
    for (const auto seen : state_seen) {
        if (seen == 0u)
            throw std::runtime_error("resource-distribution is missing a world state");
    }
    if (!reader.done())
        throw std::runtime_error("trailing resource-distribution bytes");
    return records;
}

} // namespace

bool WorldStaticLayers::validate(std::size_t province_count,
                                 std::size_t state_count) const noexcept {
    const auto valid_province = [province_count](ProvinceId id) {
        return id.valid() && static_cast<std::size_t>(id.value()) < province_count;
    };
    const auto valid_state = [state_count](StateId id) {
        return id.valid() && static_cast<std::size_t>(id.value()) < state_count;
    };
    if (rivers.size() > max_static_records || transports.size() > max_static_records ||
        architecture_regions.size() > max_static_records ||
        resource_distribution.size() > max_static_records)
        return false;
    const auto valid_chunk = [](const WorldChunkKey& key, WorldChunkType type) {
        return key.type == type && key.level == 0u;
    };
    if (!std::is_sorted(rivers.begin(), rivers.end()) ||
        std::adjacent_find(rivers.begin(), rivers.end()) != rivers.end() ||
        !std::is_sorted(transports.begin(), transports.end()) ||
        std::adjacent_find(transports.begin(), transports.end()) != transports.end())
        return false;
    for (const auto& key : rivers) if (!valid_chunk(key, WorldChunkType::RiverPolyline)) return false;
    for (const auto& key : transports) if (!valid_chunk(key, WorldChunkType::TransportPolyline)) return false;
    for (const auto& record : architecture_regions) {
        if (!valid_province(record.province) || !valid_state(record.state) ||
            record.region_key == 0u || record.family_key == 0u)
            return false;
    }
    std::vector<std::uint8_t> architecture_seen(province_count, 0u);
    for (const auto& record : architecture_regions) {
        const auto province = static_cast<std::size_t>(record.province.value());
        if (architecture_seen[province] != 0u) return false;
        architecture_seen[province] = 1u;
    }
    for (const auto& record : resource_distribution) {
        if (!valid_state(record.state) || record.resource_key == 0u ||
            !std::isfinite(record.capacity) || record.capacity < 0.0)
            return false;
    }
    return true;
}

std::uint64_t WorldStaticLayers::checksum() const noexcept {
    Fnv1a64 hash;
    const auto add_key = [&hash](const WorldChunkKey& key) {
        hash.add(static_cast<std::uint16_t>(key.type));
        hash.add(key.level); hash.add(key.x); hash.add(key.y); hash.add(key.variant);
    };
    hash.add(rivers.size()); for (const auto& key : rivers) add_key(key);
    hash.add(transports.size()); for (const auto& key : transports) add_key(key);
    hash.add(architecture_regions.size());
    for (const auto& record : architecture_regions) {
        hash.add(record.province.value()); hash.add(record.state.value());
        hash.add(record.region_key); hash.add(record.family_key);
    }
    hash.add(resource_distribution.size());
    for (const auto& record : resource_distribution) {
        hash.add(record.state.value()); hash.add(record.resource_key);
        hash.add(std::bit_cast<std::uint64_t>(record.capacity));
    }
    return hash.value();
}

std::size_t WorldStaticLayers::memory_bytes() const noexcept {
    return rivers.capacity() * sizeof(WorldChunkKey) +
           transports.capacity() * sizeof(WorldChunkKey) +
           architecture_regions.capacity() * sizeof(ArchitectureRegionRecord) +
           resource_distribution.capacity() * sizeof(ResourceDistributionRecord);
}

WorldStaticLayers load_world_static_layers(const WorldPackReader& pack,
                                           std::size_t province_count,
                                           std::size_t state_count) {
    WorldStaticLayers layers;
    for (const auto& entry : pack.index()) {
        switch (entry.key.type) {
        case WorldChunkType::RiverPolyline:
            if (entry.key.level != 0u)
                throw std::runtime_error("river polyline chunks must use level 0");
            layers.rivers.push_back(entry.key);
            break;
        case WorldChunkType::TransportPolyline:
            if (entry.key.level != 0u)
                throw std::runtime_error("transport polyline chunks must use level 0");
            layers.transports.push_back(entry.key);
            break;
        case WorldChunkType::ArchitectureRegion: {
            const auto decoded = decode_architecture(pack, entry, province_count, state_count);
            layers.architecture_regions.insert(layers.architecture_regions.end(), decoded.begin(), decoded.end());
            break;
        }
        case WorldChunkType::ResourceDistribution: {
            const auto decoded = decode_resources(pack, entry, state_count);
            layers.resource_distribution.insert(layers.resource_distribution.end(), decoded.begin(), decoded.end());
            break;
        }
        default:
            break;
        }
    }
    if (!layers.validate(province_count, state_count))
        throw std::runtime_error("world static-layer validation failed");
    return layers;
}

} // namespace thunder
