#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

struct ModLoadPlan;

struct VfsMount {
    std::string name;
    std::filesystem::path root;
    std::int32_t priority = 0;
};

struct VfsFile {
    std::string logical_path;
    std::filesystem::path physical_path;
    std::string mount_name;
    std::int32_t priority = 0;
};

// Startup-only mod overlay resolver. Highest priority wins per logical path;
// result ordering is deterministic and independent of directory iteration order.
class VirtualFileSystem {
public:
    void mount(VfsMount mount);
    // Consumes the complete deterministic order produced by the mod planner.
    // A planned VFS is immutable with respect to additional mounts so its
    // compatibility hash cannot silently become stale.
    void mount_plan(const ModLoadPlan& plan);
    [[nodiscard]] std::vector<VfsFile> enumerate(std::string_view extension = "") const;
    [[nodiscard]] static std::string read_text(const VfsFile& file);
    [[nodiscard]] std::size_t mount_count() const noexcept { return mounts_.size(); }
    [[nodiscard]] bool has_load_plan() const noexcept { return has_load_plan_; }
    [[nodiscard]] std::uint64_t load_plan_hash() const noexcept { return load_plan_hash_; }

private:
    std::vector<VfsMount> mounts_;
    std::uint64_t load_plan_hash_ = 0;
    bool has_load_plan_ = false;
};

} // namespace thunder
