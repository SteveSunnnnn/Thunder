#include "thunder/simulation/living/TransportNetwork.hpp"
#include "thunder/simulation/living/LivingMapSystem.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {
constexpr double chunk_size = static_cast<double>(LivingMapSystem::chunk_size_m);

std::uint16_t province_r16(ProvinceId province) {
    if (!province.valid() || province.value() >= 65'534u) throw std::out_of_range("Transport province cannot be represented as R16");
    return static_cast<std::uint16_t>(province.value() + 1u);
}
std::int32_t chunk_coord(double x) noexcept { return static_cast<std::int32_t>(std::floor(x / chunk_size)); }
std::uint16_t local_q(double v, std::int32_t chunk) noexcept {
    const auto local = std::llround(v - static_cast<double>(chunk) * chunk_size);
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(local, 0, 63'999));
}

bool clip_axis(double p, double q, double& t0, double& t1) noexcept {
    if (std::abs(p) < 1e-12) return q >= 0.0;
    const double r = q / p;
    if (p < 0.0) { if (r > t1) return false; if (r > t0) t0 = r; }
    else { if (r < t0) return false; if (r < t1) t1 = r; }
    return true;
}

bool clip_to_chunk(double x0, double y0, double x1, double y1, LivingChunkKey key,
                   double& ox0, double& oy0, double& ox1, double& oy1) noexcept {
    const double min_x = static_cast<double>(key.x) * chunk_size;
    const double min_y = static_cast<double>(key.y) * chunk_size;
    const double max_x = min_x + chunk_size - 1.0;
    const double max_y = min_y + chunk_size - 1.0;
    const double dx = x1 - x0, dy = y1 - y0;
    double t0 = 0.0, t1 = 1.0;
    if (!clip_axis(-dx, x0 - min_x, t0, t1) || !clip_axis(dx, max_x - x0, t0, t1)
        || !clip_axis(-dy, y0 - min_y, t0, t1) || !clip_axis(dy, max_y - y0, t0, t1)) return false;
    ox0 = x0 + t0 * dx; oy0 = y0 + t0 * dy;
    ox1 = x0 + t1 * dx; oy1 = y0 + t1 * dy;
    return t1 >= t0;
}

struct TempSegment { LivingChunkKey key; std::uint32_t link_index; TransportSegmentGpu segment; };

template <typename Fn>
void visit_segment_chunks(double x0, double y0, double x1, double y1, Fn&& fn) {
    std::int32_t cx = chunk_coord(x0);
    std::int32_t cy = chunk_coord(y0);
    const std::int32_t end_x = chunk_coord(x1);
    const std::int32_t end_y = chunk_coord(y1);
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const int step_x = (dx > 0.0) - (dx < 0.0);
    const int step_y = (dy > 0.0) - (dy < 0.0);
    const double inf = std::numeric_limits<double>::infinity();

    const double next_x = step_x > 0
        ? static_cast<double>(cx + 1) * chunk_size
        : static_cast<double>(cx) * chunk_size;
    const double next_y = step_y > 0
        ? static_cast<double>(cy + 1) * chunk_size
        : static_cast<double>(cy) * chunk_size;
    double t_max_x = step_x == 0 ? inf : (next_x - x0) / dx;
    double t_max_y = step_y == 0 ? inf : (next_y - y0) / dy;
    const double t_delta_x = step_x == 0 ? inf : chunk_size / std::abs(dx);
    const double t_delta_y = step_y == 0 ? inf : chunk_size / std::abs(dy);

    const auto max_steps = static_cast<std::uint64_t>(
        std::llabs(static_cast<long long>(end_x) - static_cast<long long>(cx))
        + std::llabs(static_cast<long long>(end_y) - static_cast<long long>(cy)) + 2ll);
    for (std::uint64_t step = 0; step < max_steps; ++step) {
        fn(LivingChunkKey{cx, cy});
        if (cx == end_x && cy == end_y) return;
        if (t_max_x < t_max_y) {
            cx += step_x;
            t_max_x += t_delta_x;
        } else if (t_max_y < t_max_x) {
            cy += step_y;
            t_max_y += t_delta_y;
        } else {
            cx += step_x;
            cy += step_y;
            t_max_x += t_delta_x;
            t_max_y += t_delta_y;
        }
    }
    throw std::runtime_error("transport grid traversal failed to reach destination chunk");
}
}

void TransportNetwork::build(std::span<const ProvinceVisualDefinition> provinces,
                             std::span<const TransportLinkDefinition> links) {
    links_.assign(links.begin(), links.end());
    last_build_candidate_chunks_ = 0u;
    std::vector<TempSegment> temp;
    temp.reserve(links.size() * 2u);
    for (std::size_t li = 0; li < links.size(); ++li) {
        const auto& link = links[li];
        const auto ai = static_cast<std::size_t>(link.a.value());
        const auto bi = static_cast<std::size_t>(link.b.value());
        if (!link.a.valid() || !link.b.valid() || ai >= provinces.size() || bi >= provinces.size()) throw std::out_of_range("Transport link province outside definition range");
        const auto& a = provinces[ai]; const auto& b = provinces[bi];
        visit_segment_chunks(a.center_x_m, a.center_y_m, b.center_x_m, b.center_y_m,
            [&](LivingChunkKey key) {
                ++last_build_candidate_chunks_;
                double x0=0.0, y0=0.0, x1=0.0, y1=0.0;
                if (!clip_to_chunk(a.center_x_m,a.center_y_m,b.center_x_m,b.center_y_m,key,x0,y0,x1,y1)) return;
                if (std::abs(x1-x0)+std::abs(y1-y0) < 0.01) return;
                TransportSegmentGpu segment;
                segment.x0_m=local_q(x0,key.x); segment.y0_m=local_q(y0,key.y);
                segment.x1_m=local_q(x1,key.x); segment.y1_m=local_q(y1,key.y);
                segment.province_a_r16=province_r16(link.a); segment.province_b_r16=province_r16(link.b);
                segment.kind=static_cast<std::uint8_t>(link.kind);
                segment.level=std::max<std::uint8_t>(1u,link.level); segment.flags=link.flags;
                temp.push_back(TempSegment{key,static_cast<std::uint32_t>(li),segment});
            });
    }
    std::sort(temp.begin(),temp.end(),[](const TempSegment& a,const TempSegment& b){ if(a.key!=b.key)return a.key<b.key; if(a.link_index!=b.link_index)return a.link_index<b.link_index; return a.segment.x0_m<b.segment.x0_m; });
    chunk_keys_.clear(); chunk_offsets_.clear(); segments_.clear(); segment_chunk_indices_.clear(); chunk_versions_.clear();
    chunk_offsets_.push_back(0u);
    LivingChunkKey prev{}; bool have=false; std::uint32_t current_chunk=0;
    for(const auto& t:temp){
        if(!have || !(t.key==prev)) { if(have) chunk_offsets_.push_back(static_cast<std::uint32_t>(segments_.size())); chunk_keys_.push_back(t.key); prev=t.key; have=true; current_chunk=static_cast<std::uint32_t>(chunk_keys_.size()-1u); }
        segments_.push_back(t.segment); segment_chunk_indices_.push_back(current_chunk);
    }
    if(have) chunk_offsets_.push_back(static_cast<std::uint32_t>(segments_.size()));
    chunk_versions_.assign(chunk_keys_.size(),1u);

    link_segment_offsets_.assign(links_.size()+1u,0u);
    for(const auto& t:temp) ++link_segment_offsets_[t.link_index+1u];
    for(std::size_t i=1;i<link_segment_offsets_.size();++i) link_segment_offsets_[i]+=link_segment_offsets_[i-1u];
    link_segment_indices_.resize(temp.size()); auto cursor=link_segment_offsets_;
    for(std::size_t si=0;si<temp.size();++si) link_segment_indices_[cursor[temp[si].link_index]++]=static_cast<std::uint32_t>(si);
}

void TransportNetwork::set_link_level(std::size_t link_index, std::uint8_t level) {
    if(link_index>=links_.size()) throw std::out_of_range("invalid transport link index");
    level=std::max<std::uint8_t>(1u,level); if(links_[link_index].level==level)return; links_[link_index].level=level;
    const auto begin=link_segment_offsets_.at(link_index),end=link_segment_offsets_.at(link_index+1u);
    std::uint32_t last_chunk=std::numeric_limits<std::uint32_t>::max();
    for(std::uint32_t i=begin;i<end;++i){const auto si=link_segment_indices_[i];segments_[si].level=level;const auto ci=segment_chunk_indices_[si];if(ci!=last_chunk){++chunk_versions_[ci];last_chunk=ci;}}
}

LivingChunkKey TransportNetwork::chunk_key(std::size_t index) const{return chunk_keys_.at(index);}
std::span<const TransportSegmentGpu> TransportNetwork::chunk_segments(std::size_t index) const{const auto b=chunk_offsets_.at(index),e=chunk_offsets_.at(index+1u);return std::span<const TransportSegmentGpu>{segments_}.subspan(b,e-b);}
std::uint32_t TransportNetwork::chunk_version(std::size_t index) const{return chunk_versions_.at(index);}
std::size_t TransportNetwork::memory_bytes() const noexcept{return links_.capacity()*sizeof(TransportLinkDefinition)+chunk_keys_.capacity()*sizeof(LivingChunkKey)+chunk_offsets_.capacity()*sizeof(std::uint32_t)+segments_.capacity()*sizeof(TransportSegmentGpu)+segment_chunk_indices_.capacity()*sizeof(std::uint32_t)+chunk_versions_.capacity()*sizeof(std::uint32_t)+link_segment_offsets_.capacity()*sizeof(std::uint32_t)+link_segment_indices_.capacity()*sizeof(std::uint32_t);}
std::uint64_t TransportNetwork::checksum() const noexcept{Fnv1a64 h;h.add(chunk_keys_.size());for(std::size_t i=0;i<chunk_keys_.size();++i){h.add(chunk_keys_[i]);}h.add_bytes(std::as_bytes(std::span<const TransportSegmentGpu>{segments_}));return h.value();}
} // namespace thunder
