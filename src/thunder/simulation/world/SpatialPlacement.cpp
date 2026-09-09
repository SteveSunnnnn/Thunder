#include "thunder/simulation/world/SpatialPlacement.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace thunder {
namespace {
constexpr std::uint32_t placement_magic = 0x31434C50u; // PLC1 little-endian
constexpr std::uint32_t anchor_magic = 0x31434E41u;    // ANC1 little-endian

class Writer {
public:
    void u8(std::uint8_t v) { bytes.push_back(static_cast<std::byte>(v)); }
    void u16(std::uint16_t v) { for (unsigned s = 0; s < 16; s += 8) u8(static_cast<std::uint8_t>(v >> s)); }
    void u32(std::uint32_t v) { for (unsigned s = 0; s < 32; s += 8) u8(static_cast<std::uint8_t>(v >> s)); }
    void u64(std::uint64_t v) { for (unsigned s = 0; s < 64; s += 8) u8(static_cast<std::uint8_t>(v >> s)); }
    void i16(std::int16_t v) { u16(std::bit_cast<std::uint16_t>(v)); }
    std::vector<std::byte> bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : bytes(data) {}
    void need(std::size_t n) {
        if (n > bytes.size() - pos) throw std::runtime_error("truncated spatial world chunk");
    }
    std::uint8_t u8() { need(1); return std::to_integer<std::uint8_t>(bytes[pos++]); }
    std::uint16_t u16() {
        std::uint16_t v = 0;
        for (unsigned s = 0; s < 16; s += 8) v = static_cast<std::uint16_t>(v | static_cast<std::uint16_t>(u8()) << s);
        return v;
    }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (unsigned s = 0; s < 32; s += 8) v |= static_cast<std::uint32_t>(u8()) << s;
        return v;
    }
    std::uint64_t u64() {
        std::uint64_t v = 0;
        for (unsigned s = 0; s < 64; s += 8) v |= static_cast<std::uint64_t>(u8()) << s;
        return v;
    }
    std::int16_t i16() { return std::bit_cast<std::int16_t>(u16()); }
    [[nodiscard]] bool done() const noexcept { return pos == bytes.size(); }
    std::span<const std::byte> bytes;
    std::size_t pos = 0;
};

bool candidate_less(const PlacementCandidate& a, const PlacementCandidate& b) noexcept {
    return std::tie(a.province, a.placement_class, a.chunk, a.local_x_m, a.local_y_m, a.local_z_half_m, a.weight, a.flags)
        < std::tie(b.province, b.placement_class, b.chunk, b.local_x_m, b.local_y_m, b.local_z_half_m, b.weight, b.flags);
}

bool anchor_less(const SettlementAnchor& a, const SettlementAnchor& b) noexcept {
    if (a.province != b.province) return a.province < b.province;
    if (a.importance != b.importance) return a.importance > b.importance;
    return std::tie(a.key_hash, a.chunk, a.local_x_m, a.local_y_m, a.local_z_half_m)
        < std::tie(b.key_hash, b.chunk, b.local_x_m, b.local_y_m, b.local_z_half_m);
}

void validate_local(std::uint16_t x, std::uint16_t y) {
    if (x >= static_cast<std::uint16_t>(SpatialPlacementDatabase::chunk_size_m)
        || y >= static_cast<std::uint16_t>(SpatialPlacementDatabase::chunk_size_m)) {
        throw std::runtime_error("spatial local coordinate exceeds 64-km chunk");
    }
}
} // namespace

std::vector<std::byte> SpatialPlacementWire::placement_candidates(std::span<const PlacementCandidateLocal> records) {
    if (records.size() > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("too many placement candidates");
    Writer w; w.u32(placement_magic); w.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& r : records) {
        validate_local(r.local_x_m, r.local_y_m);
        if (r.placement_class >= PlacementClass::Count) throw std::invalid_argument("invalid placement class");
        if (r.weight == 0u) throw std::invalid_argument("placement weight must be non-zero");
        w.u32(r.province.value()); w.u16(r.local_x_m); w.u16(r.local_y_m); w.i16(r.local_z_half_m);
        w.u8(static_cast<std::uint8_t>(r.placement_class)); w.u8(r.flags); w.u16(r.weight); w.u16(0u);
    }
    return std::move(w.bytes);
}

std::vector<std::byte> SpatialPlacementWire::settlement_anchors(std::span<const SettlementAnchorLocal> records) {
    if (records.size() > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("too many settlement anchors");
    Writer w; w.u32(anchor_magic); w.u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& r : records) {
        validate_local(r.local_x_m, r.local_y_m);
        w.u32(r.province.value()); w.u16(r.local_x_m); w.u16(r.local_y_m); w.i16(r.local_z_half_m);
        w.u16(r.importance); w.u64(r.key_hash);
    }
    return std::move(w.bytes);
}

void SpatialPlacementDatabase::clear() noexcept {
    province_count_ = 0;
    candidates_.clear(); anchors_.clear(); candidate_ranges_.clear(); anchor_offsets_.clear();
    province_chunk_offsets_.clear(); province_chunks_.clear();
}

void SpatialPlacementDatabase::load_from_worldpack(const WorldPackReader& pack, std::size_t province_count) {
    std::vector<PlacementCandidate> candidates;
    std::vector<SettlementAnchor> anchors;
    for (const auto& entry : pack.index()) {
        if (entry.key.type == WorldChunkType::PlacementCandidates) {
            if (entry.key.level != 0u) throw std::runtime_error("placement candidates must use level 0 chunks");
            const auto payload = pack.read(entry.key); Reader r(payload);
            if (r.u32() != placement_magic) throw std::runtime_error("invalid placement candidate chunk magic");
            const auto n = r.u32();
            if (n > 10'000'000u) throw std::runtime_error("placement candidate chunk exceeds safety cap");
            candidates.reserve(candidates.size() + n);
            for (std::uint32_t i = 0; i < n; ++i) {
                PlacementCandidate c;
                c.province = ProvinceId{r.u32()}; c.chunk = {entry.key.x, entry.key.y};
                c.local_x_m = r.u16(); c.local_y_m = r.u16(); c.local_z_half_m = r.i16();
                const auto cls = r.u8(); c.flags = r.u8(); c.weight = r.u16(); const auto reserved = r.u16();
                if (reserved != 0u) throw std::runtime_error("placement candidate reserved field is non-zero");
                if (cls >= static_cast<std::uint8_t>(PlacementClass::Count)) throw std::runtime_error("invalid placement candidate class");
                c.placement_class = static_cast<PlacementClass>(cls);
                candidates.push_back(c);
            }
            if (!r.done()) throw std::runtime_error("trailing placement candidate bytes");
        } else if (entry.key.type == WorldChunkType::SettlementAnchors) {
            if (entry.key.level != 0u) throw std::runtime_error("settlement anchors must use level 0 chunks");
            const auto payload = pack.read(entry.key); Reader r(payload);
            if (r.u32() != anchor_magic) throw std::runtime_error("invalid settlement anchor chunk magic");
            const auto n = r.u32();
            if (n > 1'000'000u) throw std::runtime_error("settlement anchor chunk exceeds safety cap");
            anchors.reserve(anchors.size() + n);
            for (std::uint32_t i = 0; i < n; ++i) {
                SettlementAnchor a;
                a.province = ProvinceId{r.u32()}; a.chunk = {entry.key.x, entry.key.y};
                a.local_x_m = r.u16(); a.local_y_m = r.u16(); a.local_z_half_m = r.i16();
                a.importance = r.u16(); a.key_hash = r.u64(); anchors.push_back(a);
            }
            if (!r.done()) throw std::runtime_error("trailing settlement anchor bytes");
        }
    }
    build(province_count, candidates, anchors);
}

void SpatialPlacementDatabase::build(std::size_t province_count, std::span<const PlacementCandidate> candidates,
                                     std::span<const SettlementAnchor> anchors) {
    if (province_count > 65'534u) throw std::invalid_argument("spatial placement province count exceeds R16 province limit");
    province_count_ = province_count;
    candidates_.assign(candidates.begin(), candidates.end());
    anchors_.assign(anchors.begin(), anchors.end());
    for (const auto& c : candidates_) {
        if (!c.province.valid() || static_cast<std::size_t>(c.province.value()) >= province_count_) throw std::runtime_error("placement candidate province reference invalid");
        validate_local(c.local_x_m, c.local_y_m);
        if (c.placement_class >= PlacementClass::Count) throw std::runtime_error("placement candidate class invalid");
        if (c.weight == 0u) throw std::runtime_error("placement candidate weight is zero");
    }
    for (const auto& a : anchors_) {
        if (!a.province.valid() || static_cast<std::size_t>(a.province.value()) >= province_count_) throw std::runtime_error("settlement anchor province reference invalid");
        validate_local(a.local_x_m, a.local_y_m);
    }
    std::sort(candidates_.begin(), candidates_.end(), candidate_less);
    std::sort(anchors_.begin(), anchors_.end(), anchor_less);
    rebuild_indices();
}

std::size_t SpatialPlacementDatabase::range_index(std::size_t province, PlacementClass cls) const noexcept {
    return province * placement_class_count + static_cast<std::size_t>(cls);
}

void SpatialPlacementDatabase::rebuild_indices() {
    candidate_ranges_.assign(province_count_ * placement_class_count, {});
    std::size_t cursor = 0;
    for (std::size_t p = 0; p < province_count_; ++p) {
        for (std::size_t c = 0; c < placement_class_count; ++c) {
            const auto begin = cursor;
            while (cursor < candidates_.size()
                   && candidates_[cursor].province.value() == p
                   && static_cast<std::size_t>(candidates_[cursor].placement_class) == c) ++cursor;
            candidate_ranges_[p * placement_class_count + c] = {
                static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(cursor)};
        }
    }
    if (cursor != candidates_.size()) throw std::runtime_error("placement candidate sort/index invariant failed");

    anchor_offsets_.assign(province_count_ + 1u, 0u);
    cursor = 0;
    for (std::size_t p = 0; p < province_count_; ++p) {
        anchor_offsets_[p] = static_cast<std::uint32_t>(cursor);
        while (cursor < anchors_.size() && anchors_[cursor].province.value() == p) ++cursor;
    }
    anchor_offsets_[province_count_] = static_cast<std::uint32_t>(cursor);
    if (cursor != anchors_.size()) throw std::runtime_error("settlement anchor sort/index invariant failed");

    std::vector<std::pair<ProvinceId, SpatialChunkKey>> pairs;
    pairs.reserve(candidates_.size() + anchors_.size());
    for (const auto& c : candidates_) pairs.emplace_back(c.province, c.chunk);
    for (const auto& a : anchors_) pairs.emplace_back(a.province, a.chunk);
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    province_chunk_offsets_.assign(province_count_ + 1u, 0u);
    province_chunks_.clear(); province_chunks_.reserve(pairs.size());
    cursor = 0;
    for (std::size_t p = 0; p < province_count_; ++p) {
        province_chunk_offsets_[p] = static_cast<std::uint32_t>(province_chunks_.size());
        while (cursor < pairs.size() && pairs[cursor].first.value() == p) {
            province_chunks_.push_back(pairs[cursor].second); ++cursor;
        }
    }
    province_chunk_offsets_[province_count_] = static_cast<std::uint32_t>(province_chunks_.size());
}

std::span<const PlacementCandidate> SpatialPlacementDatabase::candidates(ProvinceId province, PlacementClass cls) const noexcept {
    if (!province.valid() || static_cast<std::size_t>(province.value()) >= province_count_ || cls >= PlacementClass::Count) return {};
    const auto r = candidate_ranges_[range_index(static_cast<std::size_t>(province.value()), cls)];
    return std::span<const PlacementCandidate>{candidates_}.subspan(r.begin, r.end - r.begin);
}

std::span<const SettlementAnchor> SpatialPlacementDatabase::anchors(ProvinceId province) const noexcept {
    if (!province.valid() || static_cast<std::size_t>(province.value()) >= province_count_) return {};
    const auto p = static_cast<std::size_t>(province.value());
    return std::span<const SettlementAnchor>{anchors_}.subspan(anchor_offsets_[p], anchor_offsets_[p + 1u] - anchor_offsets_[p]);
}

std::span<const SpatialChunkKey> SpatialPlacementDatabase::chunks(ProvinceId province) const noexcept {
    if (!province.valid() || static_cast<std::size_t>(province.value()) >= province_count_) return {};
    const auto p = static_cast<std::size_t>(province.value());
    return std::span<const SpatialChunkKey>{province_chunks_}.subspan(province_chunk_offsets_[p], province_chunk_offsets_[p + 1u] - province_chunk_offsets_[p]);
}

std::uint64_t SpatialPlacementDatabase::checksum() const noexcept {
    Fnv1a64 h; h.add(province_count_); h.add(candidates_.size());
    for (const auto& c : candidates_) { h.add(c.province.value()); h.add(c.chunk.x); h.add(c.chunk.y); h.add(c.local_x_m); h.add(c.local_y_m); h.add(c.local_z_half_m); h.add(static_cast<std::uint8_t>(c.placement_class)); h.add(c.flags); h.add(c.weight); }
    h.add(anchors_.size());
    for (const auto& a : anchors_) { h.add(a.province.value()); h.add(a.chunk.x); h.add(a.chunk.y); h.add(a.local_x_m); h.add(a.local_y_m); h.add(a.local_z_half_m); h.add(a.importance); h.add(a.key_hash); }
    return h.value();
}

std::size_t SpatialPlacementDatabase::memory_bytes() const noexcept {
    return candidates_.capacity() * sizeof(PlacementCandidate) + anchors_.capacity() * sizeof(SettlementAnchor)
        + candidate_ranges_.capacity() * sizeof(Range) + anchor_offsets_.capacity() * sizeof(std::uint32_t)
        + province_chunk_offsets_.capacity() * sizeof(std::uint32_t) + province_chunks_.capacity() * sizeof(SpatialChunkKey);
}

} // namespace thunder
