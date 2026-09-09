#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {
enum class AssetKind : std::uint8_t { Mesh, Texture, Shader, Material, Audio, Font, World, Script };
struct AssetPackEntry { std::uint64_t key_hash=0; AssetKind kind=AssetKind::Mesh; std::uint8_t lod=0; std::uint64_t offset=0; std::uint64_t size=0; std::uint64_t content_hash=0; friend auto operator<=>(const AssetPackEntry&,const AssetPackEntry&)=default; };
class AssetPackWriter {
public:
    void add(std::string key, AssetKind kind, std::uint8_t lod, std::span<const std::byte> payload);
    void write(const std::filesystem::path& path) const;
private:
    struct Pending { std::string key; AssetPackEntry entry; std::vector<std::byte> payload; };
    std::vector<Pending> pending_;
};
class AssetPackReader {
public:
    void open(const std::filesystem::path& path);
    [[nodiscard]] std::optional<AssetPackEntry> find(std::string_view key, std::uint8_t lod=0) const noexcept;
    [[nodiscard]] std::vector<std::byte> read(const AssetPackEntry& entry) const;
    [[nodiscard]] std::span<const AssetPackEntry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::uint64_t build_hash() const noexcept { return build_hash_; }
private:
    std::filesystem::path path_;
    std::vector<AssetPackEntry> entries_;
    std::uint64_t build_hash_=0;
    std::uint64_t payload_end_=0;
};
struct ResidentAsset { std::uint64_t key_hash=0; std::uint8_t lod=0; std::size_t bytes=0; std::uint64_t last_used_frame=0; bool pinned=false; };
class AssetResidencyManager {
public:
    explicit AssetResidencyManager(std::size_t budget_bytes):budget_(budget_bytes){}
    void touch(std::uint64_t key_hash,std::uint8_t lod,std::size_t bytes,std::uint64_t frame,bool pinned=false);
    std::vector<ResidentAsset> enforce_budget();
    [[nodiscard]] std::size_t resident_bytes() const noexcept{return resident_bytes_;}
    [[nodiscard]] std::size_t budget_bytes() const noexcept{return budget_;}
private:
    std::size_t budget_=0,resident_bytes_=0;std::vector<ResidentAsset> resident_;
};
[[nodiscard]] std::uint64_t asset_key_hash(std::string_view key) noexcept;

class AssetHotReloader {
public:
    void track_file(std::string_view key, const std::filesystem::path& file_path);
    [[nodiscard]] std::vector<std::string> poll_changed_assets();
    [[nodiscard]] std::size_t tracked_count() const noexcept { return tracked_.size(); }

private:
    struct TrackedFile {
        std::string key;
        std::filesystem::path path;
        std::filesystem::file_time_type last_write_time{};
    };
    std::vector<TrackedFile> tracked_;
};

} // namespace thunder

