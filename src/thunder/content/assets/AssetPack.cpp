#include "thunder/content/assets/AssetPack.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
namespace thunder {
namespace {constexpr std::array<char,8> magic{{'T','H','U','N','D','R','A','S'}};constexpr std::uint32_t version=1;constexpr std::uint64_t header_size=48;constexpr std::uint64_t index_size=40;
template<class T>void put(std::ostream&o,T v){for(std::size_t i=0;i<sizeof(T);++i){const auto b=static_cast<unsigned char>((static_cast<std::uint64_t>(v)>>(i*8u))&0xffu);o.put(static_cast<char>(b));}if(!o)throw std::runtime_error("asset pack write failed");}
template<class T>T get(std::istream&i){std::uint64_t v=0;for(std::size_t n=0;n<sizeof(T);++n){const int c=i.get();if(c<0)throw std::runtime_error("truncated asset pack");v|=static_cast<std::uint64_t>(static_cast<unsigned char>(c))<<(n*8u);}return static_cast<T>(v);}
std::uint64_t hbytes(std::span<const std::byte>b){Fnv1a64 h;h.add_bytes(b);return h.value();}
std::uint64_t compute_asset_build_hash(std::span<const AssetPackEntry> es){Fnv1a64 h;for(const auto&e:es){h.add(e.key_hash);h.add(static_cast<std::uint8_t>(e.kind));h.add(e.lod);h.add(e.size);h.add(e.content_hash);}return h.value();}
}
std::uint64_t asset_key_hash(std::string_view key) noexcept{Fnv1a64 h;h.add(key);return h.value();}
void AssetPackWriter::add(std::string key,AssetKind kind,std::uint8_t lod,std::span<const std::byte> payload){if(key.empty())throw std::invalid_argument("asset key empty");Pending p;p.key=std::move(key);p.entry.key_hash=asset_key_hash(p.key);p.entry.kind=kind;p.entry.lod=lod;p.entry.size=payload.size();p.entry.content_hash=hbytes(payload);p.payload.assign(payload.begin(),payload.end());pending_.push_back(std::move(p));}
void AssetPackWriter::write(const std::filesystem::path& path) const {auto items=pending_;std::sort(items.begin(),items.end(),[](const Pending&a,const Pending&b){if(a.entry.key_hash!=b.entry.key_hash)return a.entry.key_hash<b.entry.key_hash;return a.entry.lod<b.entry.lod;});for(std::size_t i=1;i<items.size();++i)if(items[i-1].entry.key_hash==items[i].entry.key_hash&&items[i-1].entry.lod==items[i].entry.lod)throw std::runtime_error("asset hash/lod collision");std::ofstream o(path,std::ios::binary|std::ios::trunc);if(!o)throw std::runtime_error("cannot create asset pack");std::array<char,48> zero{};o.write(zero.data(),zero.size());std::vector<AssetPackEntry> es;es.reserve(items.size());for(auto&it:items){it.entry.offset=static_cast<std::uint64_t>(o.tellp());if(!it.payload.empty())o.write(reinterpret_cast<const char*>(it.payload.data()),static_cast<std::streamsize>(it.payload.size()));es.push_back(it.entry);}const auto index_off=static_cast<std::uint64_t>(o.tellp());for(const auto&e:es){put(o,e.key_hash);put(o,static_cast<std::uint8_t>(e.kind));put(o,e.lod);put(o,std::uint16_t{0});put(o,e.offset);put(o,e.size);put(o,e.content_hash);put(o,std::uint32_t{0});}const auto bh=compute_asset_build_hash(es);o.seekp(0);o.write(magic.data(),magic.size());put(o,version);put(o,static_cast<std::uint32_t>(es.size()));put(o,index_off);put(o,static_cast<std::uint64_t>(es.size())*index_size);put(o,bh);put(o,std::uint64_t{0});}
void AssetPackReader::open(const std::filesystem::path& path){std::ifstream i(path,std::ios::binary);if(!i)throw std::runtime_error("cannot open asset pack");std::array<char,8> got{};i.read(got.data(),got.size());if(got!=magic)throw std::runtime_error("invalid asset pack magic");if(get<std::uint32_t>(i)!=version)throw std::runtime_error("unsupported asset pack version");const auto count=get<std::uint32_t>(i);if(count>2'000'000u)throw std::runtime_error("asset count exceeds cap");const auto index_off=get<std::uint64_t>(i);const auto index_bytes=get<std::uint64_t>(i);build_hash_=get<std::uint64_t>(i);(void)get<std::uint64_t>(i);i.seekg(0,std::ios::end);const auto end=static_cast<std::uint64_t>(i.tellg());if(index_bytes!=static_cast<std::uint64_t>(count)*index_size||index_off<header_size||index_off> end||index_bytes>end-index_off)throw std::runtime_error("invalid asset index bounds");payload_end_=index_off;i.seekg(static_cast<std::streamoff>(index_off));entries_.clear();entries_.reserve(count);for(std::uint32_t n=0;n<count;++n){AssetPackEntry e;e.key_hash=get<std::uint64_t>(i);const auto k=get<std::uint8_t>(i);if(k>static_cast<std::uint8_t>(AssetKind::Script))throw std::runtime_error("invalid asset kind");e.kind=static_cast<AssetKind>(k);e.lod=get<std::uint8_t>(i);(void)get<std::uint16_t>(i);e.offset=get<std::uint64_t>(i);e.size=get<std::uint64_t>(i);e.content_hash=get<std::uint64_t>(i);(void)get<std::uint32_t>(i);if(e.offset<header_size||e.offset>payload_end_||e.size>payload_end_-e.offset)throw std::runtime_error("asset payload out of bounds");entries_.push_back(e);}if(!std::is_sorted(entries_.begin(),entries_.end(),[](const auto&a,const auto&b){if(a.key_hash!=b.key_hash)return a.key_hash<b.key_hash;return a.lod<b.lod;}))throw std::runtime_error("asset index not sorted");for(std::size_t n=1;n<entries_.size();++n)if(entries_[n-1].key_hash==entries_[n].key_hash&&entries_[n-1].lod==entries_[n].lod)throw std::runtime_error("duplicate asset lookup key");if(compute_asset_build_hash(entries_)!=build_hash_)throw std::runtime_error("asset build hash mismatch");path_=path;}
std::optional<AssetPackEntry> AssetPackReader::find(std::string_view key,std::uint8_t lod) const noexcept{const auto h=asset_key_hash(key);auto it=std::lower_bound(entries_.begin(),entries_.end(),std::pair{h,lod},[](const AssetPackEntry&e,const auto&k){return e.key_hash<k.first||(e.key_hash==k.first&&e.lod<k.second);});if(it==entries_.end()||it->key_hash!=h||it->lod!=lod)return std::nullopt;return *it;}
std::vector<std::byte> AssetPackReader::read(const AssetPackEntry&e)const{std::ifstream i(path_,std::ios::binary);if(!i)throw std::runtime_error("asset pack unavailable");i.seekg(static_cast<std::streamoff>(e.offset));std::vector<std::byte>b(static_cast<std::size_t>(e.size));if(!b.empty())i.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!i&&!b.empty())throw std::runtime_error("asset payload read failed");if(hbytes(b)!=e.content_hash)throw std::runtime_error("asset payload checksum mismatch");return b;}
void AssetResidencyManager::touch(std::uint64_t h,std::uint8_t lod,std::size_t bytes,std::uint64_t frame,bool pinned){auto it=std::find_if(resident_.begin(),resident_.end(),[&](const auto&r){return r.key_hash==h&&r.lod==lod;});if(it==resident_.end()){resident_.push_back({h,lod,bytes,frame,pinned});resident_bytes_+=bytes;}else{resident_bytes_-=it->bytes;it->bytes=bytes;it->last_used_frame=frame;it->pinned=it->pinned||pinned;resident_bytes_+=bytes;}}
std::vector<ResidentAsset> AssetResidencyManager::enforce_budget(){std::vector<ResidentAsset> evicted;while(resident_bytes_>budget_){auto it=resident_.end();for(auto p=resident_.begin();p!=resident_.end();++p)if(!p->pinned&&(it==resident_.end()||p->last_used_frame<it->last_used_frame))it=p;if(it==resident_.end())break;resident_bytes_-=it->bytes;evicted.push_back(*it);resident_.erase(it);}return evicted;}

void AssetHotReloader::track_file(std::string_view key, const std::filesystem::path& file_path) {
    for (auto& t : tracked_) {
        if (t.key == key) {
            t.path = file_path;
            if (std::filesystem::exists(file_path)) {
                t.last_write_time = std::filesystem::last_write_time(file_path);
            }
            return;
        }
    }
    std::filesystem::file_time_type lwt{};
    if (std::filesystem::exists(file_path)) {
        lwt = std::filesystem::last_write_time(file_path);
    }
    tracked_.push_back({std::string(key), file_path, lwt});
}

std::vector<std::string> AssetHotReloader::poll_changed_assets() {
    std::vector<std::string> changed;
    for (auto& t : tracked_) {
        if (std::filesystem::exists(t.path)) {
            const auto current_lwt = std::filesystem::last_write_time(t.path);
            if (current_lwt != t.last_write_time) {
                t.last_write_time = current_lwt;
                changed.push_back(t.key);
            }
        }
    }
    return changed;
}

} // namespace thunder

