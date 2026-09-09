#include "thunder/simulation/living/LivingMapSystem.hpp"
#include "thunder/foundation/base/DeterministicRng.hpp"
#include "thunder/simulation/economy/BuildingStore.hpp"
#include "thunder/simulation/economy/PopStore.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {
constexpr std::uint64_t living_seed = 0xC0FE1836A11CE55ull;

std::int32_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
    auto q = value / divisor;
    const auto r = value % divisor;
    if (r != 0 && ((r < 0) != (divisor < 0))) --q;
    return static_cast<std::int32_t>(q);
}

std::uint16_t clamp_u16(std::int64_t value) noexcept {
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(value, 0, std::numeric_limits<std::uint16_t>::max()));
}

std::uint16_t q_population(std::uint64_t population) noexcept {
    if (population == 0u) return 0u;
    const double v = std::log2(static_cast<double>(population) + 1.0) / 24.0;
    return static_cast<std::uint16_t>(std::clamp(v, 0.0, 1.0) * 65535.0 + 0.5);
}

std::uint16_t q_ratio(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    if (denominator == 0u) return 0u;
    const auto scaled = (numerator * 65535ull) / denominator;
    return static_cast<std::uint16_t>(std::min<std::uint64_t>(scaled, 65535ull));
}

std::size_t capped_count(std::uint64_t value, std::uint64_t divisor, std::size_t minimum, std::size_t maximum) noexcept {
    if (value == 0u) return 0u;
    const auto raw = static_cast<std::size_t>(value / divisor);
    return std::clamp(raw + minimum, minimum, maximum);
}

const PlacementCandidate* choose_weighted(std::span<const PlacementCandidate> candidates, std::uint64_t random) noexcept {
    if (candidates.empty()) return nullptr;
    std::uint64_t total = 0;
    for (const auto& c : candidates) total += c.weight;
    if (total == 0u) return &candidates.front();
    auto target = random % total;
    for (const auto& c : candidates) {
        if (target < c.weight) return &c;
        target -= c.weight;
    }
    return &candidates.back();
}
} // namespace

void LivingMapSystem::set_provinces(std::span<const ProvinceVisualDefinition> definitions) {
    if (definitions.size() > 65'534u) throw std::invalid_argument("LivingMap supports at most 65,534 provinces in R16 province payloads");
    province_defs_.assign(definitions.begin(), definitions.end());
    province_cache_.clear();
    province_cache_.resize(province_defs_.size());
    spatial_index_dirty_ = true;
    if (placement_.province_count() != 0u && placement_.province_count() != province_defs_.size()) {
        placement_.clear();
    }
    build_chunk_layout();
}

void LivingMapSystem::set_spatial_placement(SpatialPlacementDatabase placement) {
    if (!province_defs_.empty() && placement.province_count() != province_defs_.size()) {
        throw std::invalid_argument("LivingMap spatial placement province count mismatch");
    }
    placement_ = std::move(placement);
    for (auto& cache : province_cache_) cache.signature = 0u;
    build_chunk_layout();
}

void LivingMapSystem::set_building_visual(BuildingTypeId type, BuildingVisualKind kind, std::uint16_t asset_family) {
    if (!type.valid()) throw std::invalid_argument("invalid BuildingTypeId for LivingMap visual mapping");
    const auto index = static_cast<std::size_t>(type.value());
    if (building_visuals_.size() <= index) building_visuals_.resize(index + 1u);
    building_visuals_[index] = BuildingVisualRecord{kind, asset_family};
}

LivingChunkKey LivingMapSystem::make_chunk_key(double x_m, double y_m) noexcept {
    const auto xi = static_cast<std::int64_t>(std::floor(x_m));
    const auto yi = static_cast<std::int64_t>(std::floor(y_m));
    return LivingChunkKey{floor_div(xi, chunk_size_m), floor_div(yi, chunk_size_m)};
}

std::uint16_t LivingMapSystem::province_r16(ProvinceId province) {
    if (!province.valid() || province.value() >= 65'534u) throw std::out_of_range("ProvinceId cannot be represented in LivingMap R16 payload");
    return static_cast<std::uint16_t>(province.value() + 1u);
}

void LivingMapSystem::build_chunk_layout() {
    chunk_keys_.clear();
    province_chunk_offsets_.assign(province_defs_.size() + 1u, 0u);
    province_chunk_indices_.clear();
    chunk_province_offsets_.clear();
    chunk_provinces_.clear();

    std::vector<std::pair<ProvinceId, LivingChunkKey>> province_pairs;
    province_pairs.reserve(province_defs_.size() * 2u);
    for (std::size_t i = 0; i < province_defs_.size(); ++i) {
        const ProvinceId province{static_cast<ProvinceId::rep_type>(i)};
        const auto spatial_chunks = placement_.chunks(province);
        if (spatial_chunks.empty()) {
            province_pairs.emplace_back(province, make_chunk_key(province_defs_[i].center_x_m, province_defs_[i].center_y_m));
        } else {
            for (const auto key : spatial_chunks) province_pairs.emplace_back(province, living_key(key));
        }
    }
    std::sort(province_pairs.begin(), province_pairs.end());
    province_pairs.erase(std::unique(province_pairs.begin(), province_pairs.end()), province_pairs.end());

    std::vector<std::pair<LivingChunkKey, ProvinceId>> chunk_pairs;
    chunk_pairs.reserve(province_pairs.size());
    for (const auto& [province, key] : province_pairs) chunk_pairs.emplace_back(key, province);
    std::sort(chunk_pairs.begin(), chunk_pairs.end());

    LivingChunkKey previous{};
    bool have_previous = false;
    chunk_province_offsets_.push_back(0u);
    for (const auto& [key, province] : chunk_pairs) {
        if (!have_previous || !(key == previous)) {
            if (have_previous) chunk_province_offsets_.push_back(static_cast<std::uint32_t>(chunk_provinces_.size()));
            chunk_keys_.push_back(key); previous = key; have_previous = true;
        }
        chunk_provinces_.push_back(province);
    }
    if (have_previous) chunk_province_offsets_.push_back(static_cast<std::uint32_t>(chunk_provinces_.size()));

    std::size_t cursor = 0;
    for (std::size_t p = 0; p < province_defs_.size(); ++p) {
        province_chunk_offsets_[p] = static_cast<std::uint32_t>(province_chunk_indices_.size());
        while (cursor < province_pairs.size() && province_pairs[cursor].first.value() == p) {
            const auto key = province_pairs[cursor].second;
            const auto it = std::lower_bound(chunk_keys_.begin(), chunk_keys_.end(), key);
            if (it == chunk_keys_.end() || !(*it == key)) throw std::logic_error("LivingMap chunk layout invariant failed");
            province_chunk_indices_.push_back(static_cast<std::uint32_t>(it - chunk_keys_.begin()));
            ++cursor;
        }
    }
    province_chunk_offsets_[province_defs_.size()] = static_cast<std::uint32_t>(province_chunk_indices_.size());

    chunk_cache_.clear();
    chunk_cache_.resize(chunk_keys_.size());
    dirty_chunk_epoch_.assign(chunk_keys_.size(), 0u);
    dirty_chunk_indices_.clear();
    total_instances_ = 0u;
}

void LivingMapSystem::rebuild_spatial_index(const World& world) {
    spatial_index_.rebuild(province_defs_.size(), world.pops, world.buildings);
    spatial_index_dirty_ = false;
}

std::uint64_t LivingMapSystem::make_signature(const ProvinceVisualAggregate& a) const noexcept {
    Fnv1a64 h;
    h.add(a.population / 2'500ull);
    h.add(a.employed / 2'500ull);
    const auto average_sol = a.population > 0u ? a.sol_weighted / a.population : 0u;
    h.add(average_sol / 100u);
    h.add(a.urban_levels); h.add(a.factory_levels); h.add(a.farm_levels); h.add(a.mine_levels); h.add(a.port_levels);
    return h.value();
}

void LivingMapSystem::generate_province(ProvinceId province, ProvinceCache& cache) const {
    const auto pi = static_cast<std::size_t>(province.value());
    const auto& def = province_defs_[pi];
    const auto fallback_chunk = make_chunk_key(def.center_x_m, def.center_y_m);
    const auto fallback_origin_x = static_cast<std::int64_t>(fallback_chunk.x) * chunk_size_m;
    const auto fallback_origin_y = static_cast<std::int64_t>(fallback_chunk.y) * chunk_size_m;
    const auto fallback_center_x = static_cast<std::int64_t>(std::llround(def.center_x_m)) - fallback_origin_x;
    const auto fallback_center_y = static_cast<std::int64_t>(std::llround(def.center_y_m)) - fallback_origin_y;
    const auto radius = static_cast<std::int64_t>(std::min<std::uint32_t>(def.visual_radius_m, 24'000u));

    const auto anchors = placement_.anchors(province);
    if (!anchors.empty()) {
        cache.cluster_chunk = living_key(anchors.front().chunk);
        cache.cluster.local_x_m = anchors.front().local_x_m;
        cache.cluster.local_y_m = anchors.front().local_y_m;
    } else {
        auto urban = placement_.candidates(province, PlacementClass::Urban);
        if (urban.empty()) urban = placement_.candidates(province, PlacementClass::Buildable);
        if (!urban.empty()) {
            cache.cluster_chunk = living_key(urban.front().chunk);
            cache.cluster.local_x_m = urban.front().local_x_m;
            cache.cluster.local_y_m = urban.front().local_y_m;
        } else {
            cache.cluster_chunk = fallback_chunk;
            cache.cluster.local_x_m = clamp_u16(fallback_center_x);
            cache.cluster.local_y_m = clamp_u16(fallback_center_y);
        }
    }

    const auto residential = capped_count(cache.aggregate.population, 5'000u, 2u, 192u);
    const auto commercial = residential > 0u ? std::clamp<std::size_t>(residential / 7u, 1u, 32u) : 0u;
    const auto factories = std::min<std::size_t>(static_cast<std::size_t>(cache.aggregate.factory_levels) * 2u, 80u);
    const auto farms = std::min<std::size_t>(static_cast<std::size_t>(cache.aggregate.farm_levels) * 2u, 96u);
    const auto mines = std::min<std::size_t>(static_cast<std::size_t>(cache.aggregate.mine_levels), 48u);
    const auto ports = def.coastal ? std::min<std::size_t>(static_cast<std::size_t>(cache.aggregate.port_levels) * 2u, 24u) : 0u;
    const auto total = residential + commercial + factories + farms + mines + ports;
    cache.instances.clear(); cache.instances.reserve(total);

    auto emit = [&](LivingInstanceKind kind, PlacementClass primary, PlacementClass secondary,
                    std::size_t count, std::uint16_t family, std::uint16_t scale_base) {
        auto candidates = placement_.candidates(province, primary);
        if (candidates.empty() && secondary != primary) candidates = placement_.candidates(province, secondary);
        if (candidates.empty()) candidates = placement_.candidates(province, PlacementClass::Buildable);
        for (std::size_t i = 0; i < count; ++i) {
            const auto stream = (static_cast<std::uint64_t>(province.value()) << 32u)
                ^ (static_cast<std::uint64_t>(kind) << 24u) ^ static_cast<std::uint64_t>(i);
            const auto r0 = DeterministicRng::keyed_u64(living_seed, stream, 0u);
            const auto r1 = DeterministicRng::keyed_u64(living_seed, stream, 1u);
            PlacedInstance placed;
            if (const auto* candidate = choose_weighted(candidates, r0)) {
                placed.chunk = living_key(candidate->chunk);
                placed.gpu.local_x_m = candidate->local_x_m;
                placed.gpu.local_y_m = candidate->local_y_m;
                placed.gpu.local_z_half_m = candidate->local_z_half_m;
            } else {
                placed.chunk = fallback_chunk;
                const auto signed_x = static_cast<std::int64_t>(static_cast<std::int32_t>(r0 & 0xFFFFu) - 32768);
                const auto signed_y = static_cast<std::int64_t>(static_cast<std::int32_t>(r1 & 0xFFFFu) - 32768);
                std::int64_t spread = radius;
                if (kind == LivingInstanceKind::Residential || kind == LivingInstanceKind::Commercial || kind == LivingInstanceKind::Factory) spread = radius / 2;
                if (kind == LivingInstanceKind::Port) spread = std::max<std::int64_t>(1, radius / 3);
                const auto local_x = std::clamp<std::int64_t>(fallback_center_x + (signed_x * spread) / 32768, 0, chunk_size_m - 1);
                const auto local_y = std::clamp<std::int64_t>(fallback_center_y + (signed_y * spread) / 32768, 0, chunk_size_m - 1);
                placed.gpu.local_x_m = clamp_u16(local_x); placed.gpu.local_y_m = clamp_u16(local_y); placed.gpu.local_z_half_m = 0;
            }
            placed.gpu.yaw_u16 = static_cast<std::uint16_t>((r0 >> 16u) & 0xFFFFu);
            placed.gpu.scale_milli = static_cast<std::uint16_t>(std::min<std::uint32_t>(65'535u, static_cast<std::uint32_t>(scale_base) + static_cast<std::uint32_t>((r1 >> 16u) & 0xFFu)));
            const auto local_variant = static_cast<std::uint16_t>(
                (family + static_cast<std::uint16_t>((r0 >> 40u) & 0x0Fu)) & 0x00FFu);
            // Keep the lower byte available for the semantic building kind;
            // the upper byte selects the authored architecture family. The
            // instance remains the existing 16-byte contract.
            placed.gpu.asset_variant = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(def.architecture_family) << 8u) | local_variant);
            placed.gpu.province_r16 = province_r16(province);
            placed.gpu.kind = static_cast<std::uint8_t>(kind);
            placed.gpu.lod_mask = (kind == LivingInstanceKind::Farm || kind == LivingInstanceKind::Mine) ? 0x01u : 0x03u;
            cache.instances.push_back(placed);
        }
    };

    emit(LivingInstanceKind::Residential, PlacementClass::Urban, PlacementClass::Buildable, residential, 0u, 900u);
    emit(LivingInstanceKind::Commercial, PlacementClass::Urban, PlacementClass::Buildable, commercial, 32u, 950u);
    emit(LivingInstanceKind::Factory, PlacementClass::Industrial, PlacementClass::Urban, factories, 64u, 1100u);
    emit(LivingInstanceKind::Farm, PlacementClass::Farm, PlacementClass::Rural, farms, 96u, 1000u);
    emit(LivingInstanceKind::Mine, PlacementClass::Mine, PlacementClass::Buildable, mines, 128u, 1050u);
    emit(LivingInstanceKind::Port, PlacementClass::Port, PlacementClass::Buildable, ports, 160u, 1150u);

    cache.cluster.province_r16 = province_r16(province);
    cache.cluster.population_q = q_population(cache.aggregate.population);
    cache.cluster.urban_q = q_ratio(cache.aggregate.urban_levels + cache.aggregate.factory_levels,
        std::max<std::uint64_t>(1u, cache.aggregate.urban_levels + cache.aggregate.factory_levels + cache.aggregate.farm_levels + cache.aggregate.mine_levels));
    cache.cluster.industry_q = static_cast<std::uint16_t>(std::min<std::uint32_t>(65'535u, cache.aggregate.factory_levels * 1024u + cache.aggregate.mine_levels * 512u));
    cache.cluster.rural_q = static_cast<std::uint16_t>(std::min<std::uint32_t>(65'535u, cache.aggregate.farm_levels * 1024u));
    cache.cluster.flags = static_cast<std::uint16_t>(def.coastal ? 0x0001u : 0u) | (static_cast<std::uint16_t>(def.biome) << 8u);
}

void LivingMapSystem::rebuild_chunk(std::size_t chunk_index) {
    auto& out = chunk_cache_.at(chunk_index);
    const auto key = chunk_keys_.at(chunk_index);
    const auto begin = chunk_province_offsets_.at(chunk_index);
    const auto end = chunk_province_offsets_.at(chunk_index + 1u);
    std::size_t instance_count = 0u; std::size_t cluster_count = 0u;
    for (std::size_t i = begin; i < end; ++i) {
        const auto& province = province_cache_[chunk_provinces_[i].value()];
        for (const auto& instance : province.instances) if (instance.chunk == key) ++instance_count;
        if (province.cluster_chunk == key) ++cluster_count;
    }
    out.instances.clear(); out.instances.reserve(instance_count);
    out.clusters.clear(); out.clusters.reserve(cluster_count);
    for (std::size_t i = begin; i < end; ++i) {
        const auto& province = province_cache_[chunk_provinces_[i].value()];
        for (const auto& instance : province.instances) if (instance.chunk == key) out.instances.push_back(instance.gpu);
        if (province.cluster_chunk == key) out.clusters.push_back(province.cluster);
    }
    ++out.version;
}

void LivingMapSystem::mark_province_chunks_dirty(std::size_t province_index) {
    const auto begin = province_chunk_offsets_.at(province_index);
    const auto end = province_chunk_offsets_.at(province_index + 1u);
    for (std::size_t i = begin; i < end; ++i) {
        const auto ci = province_chunk_indices_[i];
        if (dirty_chunk_epoch_[ci] != update_epoch_) {
            dirty_chunk_epoch_[ci] = update_epoch_;
            dirty_chunk_indices_.push_back(ci);
        }
    }
}

LivingMapUpdateStats LivingMapSystem::update(const World& world, JobSystem& jobs) {
    if (province_defs_.empty()) return {};
    if (spatial_index_dirty_ || spatial_index_.province_count() != province_defs_.size()
        || !spatial_index_.current_for(world.pops, world.buildings)) rebuild_spatial_index(world);
    if (province_cache_.size() != province_defs_.size()) province_cache_.resize(province_defs_.size());

    const auto populations = world.pops.populations();
    const auto employed = world.pops.employed_all();
    const auto sol = world.pops.sol_all();
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();

    const auto dispatch = jobs.parallel_for(province_defs_.size(), 32u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t pi = begin; pi < end; ++pi) {
                const ProvinceId province{static_cast<ProvinceId::rep_type>(pi)};
                ProvinceVisualAggregate aggregate{};
                for (const auto pop : spatial_index_.pops(province)) {
                    const auto i = static_cast<std::size_t>(pop.value());
                    aggregate.population += populations[i]; aggregate.employed += employed[i];
                    aggregate.sol_weighted += static_cast<std::uint64_t>(std::max<std::int32_t>(0, sol[i])) * populations[i];
                }
                for (const auto building : spatial_index_.buildings(province)) {
                    const auto i = static_cast<std::size_t>(building.value());
                    const auto type = static_cast<std::size_t>(building_types[i].value());
                    const auto visual = type < building_visuals_.size() ? building_visuals_[type] : BuildingVisualRecord{};
                    const auto level = static_cast<std::uint32_t>(building_levels[i]);
                    switch (visual.kind) {
                        case BuildingVisualKind::Urban: aggregate.urban_levels += level; break;
                        case BuildingVisualKind::Factory: aggregate.factory_levels += level; break;
                        case BuildingVisualKind::Farm: aggregate.farm_levels += level; break;
                        case BuildingVisualKind::Mine: aggregate.mine_levels += level; break;
                        case BuildingVisualKind::Port: aggregate.port_levels += level; break;
                        case BuildingVisualKind::None: break;
                    }
                }
                province_cache_[pi].aggregate = aggregate;
            }
        });

    if (++update_epoch_ == 0u) { std::fill(dirty_chunk_epoch_.begin(), dirty_chunk_epoch_.end(), 0u); update_epoch_ = 1u; }
    dirty_chunk_indices_.clear();
    LivingMapUpdateStats stats{}; stats.workers_used = dispatch.workers_used;
    for (std::size_t pi = 0; pi < province_cache_.size(); ++pi) {
        auto& cache = province_cache_[pi]; const auto signature = make_signature(cache.aggregate);
        if (signature == cache.signature && cache.signature != 0u) continue;
        cache.signature = signature; generate_province(ProvinceId{static_cast<ProvinceId::rep_type>(pi)}, cache); ++stats.dirty_provinces;
        mark_province_chunks_dirty(pi);
    }
    for (const auto ci_raw : dirty_chunk_indices_) {
        const auto ci = static_cast<std::size_t>(ci_raw); const auto old_instances = chunk_cache_[ci].instances.size();
        rebuild_chunk(ci); const auto new_instances = chunk_cache_[ci].instances.size(); total_instances_ = total_instances_ - old_instances + new_instances;
        ++stats.dirty_chunks; stats.upload_bytes += new_instances * sizeof(LivingInstanceGpu) + chunk_cache_[ci].clusters.size() * sizeof(LivingClusterGpu);
    }
    stats.total_instances = total_instances_; return stats;
}

LivingChunkKey LivingMapSystem::chunk_key(std::size_t index) const { return chunk_keys_.at(index); }
std::size_t LivingMapSystem::find_chunk_index(LivingChunkKey key) const noexcept {
    const auto it = std::lower_bound(chunk_keys_.begin(), chunk_keys_.end(), key);
    if (it == chunk_keys_.end() || !(*it == key)) return chunk_keys_.size();
    return static_cast<std::size_t>(it - chunk_keys_.begin());
}
std::span<const LivingInstanceGpu> LivingMapSystem::chunk_instances(std::size_t index) const { return chunk_cache_.at(index).instances; }
std::span<const LivingClusterGpu> LivingMapSystem::chunk_clusters(std::size_t index) const { return chunk_cache_.at(index).clusters; }
std::uint32_t LivingMapSystem::chunk_version(std::size_t index) const { return chunk_cache_.at(index).version; }
const ProvinceVisualAggregate& LivingMapSystem::province_aggregate(ProvinceId province) const {
    const auto i = static_cast<std::size_t>(province.value());
    if (!province.valid() || i >= province_cache_.size()) throw std::out_of_range("invalid ProvinceId for LivingMap aggregate");
    return province_cache_[i].aggregate;
}

std::size_t LivingMapSystem::memory_bytes() const noexcept {
    std::size_t bytes = province_defs_.capacity() * sizeof(ProvinceVisualDefinition)
        + building_visuals_.capacity() * sizeof(BuildingVisualRecord)
        + province_cache_.capacity() * sizeof(ProvinceCache) + chunk_keys_.capacity() * sizeof(LivingChunkKey)
        + province_chunk_offsets_.capacity() * sizeof(std::uint32_t) + province_chunk_indices_.capacity() * sizeof(std::uint32_t)
        + chunk_province_offsets_.capacity() * sizeof(std::uint32_t) + chunk_provinces_.capacity() * sizeof(ProvinceId)
        + chunk_cache_.capacity() * sizeof(ChunkCache) + spatial_index_.memory_bytes() + placement_.memory_bytes();
    for (const auto& p : province_cache_) bytes += p.instances.capacity() * sizeof(PlacedInstance);
    for (const auto& c : chunk_cache_) bytes += c.instances.capacity() * sizeof(LivingInstanceGpu) + c.clusters.capacity() * sizeof(LivingClusterGpu);
    return bytes;
}

std::uint64_t LivingMapSystem::checksum() const noexcept {
    Fnv1a64 h; h.add(chunk_keys_.size());
    for (std::size_t ci = 0; ci < chunk_keys_.size(); ++ci) {
        h.add(chunk_keys_[ci]); const auto& chunk = chunk_cache_[ci];
        h.add_bytes(std::as_bytes(std::span<const LivingInstanceGpu>{chunk.instances}));
        h.add_bytes(std::as_bytes(std::span<const LivingClusterGpu>{chunk.clusters}));
    }
    return h.value();
}

} // namespace thunder
