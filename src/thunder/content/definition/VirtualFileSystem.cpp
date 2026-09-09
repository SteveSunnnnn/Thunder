#include "thunder/content/definition/VirtualFileSystem.hpp"
#include "thunder/content/definition/ModManifest.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace thunder {

void VirtualFileSystem::mount(VfsMount mount_value) {
    if (has_load_plan_) {
        throw std::logic_error("cannot append a manual VFS mount after a mod load plan");
    }
    if (mount_value.name.empty()) throw std::invalid_argument("VFS mount requires name");
    if (mount_value.root.empty()) throw std::invalid_argument("VFS mount requires root");
    mounts_.push_back(std::move(mount_value));
}

void VirtualFileSystem::mount_plan(const ModLoadPlan& plan) {
    if (!mounts_.empty() || has_load_plan_) {
        throw std::logic_error("mod load plan requires an empty VFS");
    }
    if (!plan.ok()) throw std::invalid_argument("cannot mount an invalid mod load plan");
    if (plan.entries.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::length_error("mod load plan exceeds VFS priority range");
    }

    mounts_.reserve(plan.entries.size());
    std::set<std::string, std::less<>> mounted_ids;
    for (std::size_t i = 0u; i < plan.entries.size(); ++i) {
        const auto& entry = plan.entries[i];
        if (!is_valid_mod_id(entry.manifest.id) ||
            entry.manifest.stable_id != stable_mod_id(entry.manifest.id)) {
            throw std::invalid_argument("planned VFS mount has invalid stable mod identity");
        }
        if (!mounted_ids.insert(entry.manifest.id).second) {
            throw std::invalid_argument("planned VFS contains duplicate mod id");
        }
        if (entry.load_index != static_cast<std::uint32_t>(i)) {
            throw std::invalid_argument("planned VFS load indices must be dense and ordered");
        }
        if (entry.content_root.empty()) {
            throw std::invalid_argument("planned VFS mount requires content root");
        }
        mounts_.push_back({entry.manifest.id, entry.content_root,
                           static_cast<std::int32_t>(entry.load_index)});
    }
    load_plan_hash_ = plan.content_hash;
    has_load_plan_ = true;
}

std::vector<VfsFile> VirtualFileSystem::enumerate(std::string_view extension) const {
    std::unordered_map<std::string, VfsFile> resolved;
    resolved.reserve(4096u);
    std::vector<const VfsMount*> order;
    order.reserve(mounts_.size());
    for (const auto& mount_value : mounts_) order.push_back(&mount_value);
    std::stable_sort(order.begin(), order.end(), [](const VfsMount* a, const VfsMount* b) {
        if (a->priority != b->priority) return a->priority < b->priority;
        return a->name < b->name;
    });

    for (const auto* mount_value : order) {
        if (!std::filesystem::exists(mount_value->root)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(mount_value->root)) {
            if (!entry.is_regular_file()) continue;
            if (!extension.empty() && entry.path().extension() != extension) continue;
            const auto relative = std::filesystem::relative(entry.path(), mount_value->root).generic_string();
            resolved.insert_or_assign(relative, VfsFile{relative, entry.path(), mount_value->name, mount_value->priority});
        }
    }

    std::vector<VfsFile> result;
    result.reserve(resolved.size());
    for (auto& [path, file] : resolved) {
        (void)path;
        result.push_back(std::move(file));
    }
    std::sort(result.begin(), result.end(), [](const VfsFile& a, const VfsFile& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.logical_path < b.logical_path;
    });
    return result;
}

std::string VirtualFileSystem::read_text(const VfsFile& file) {
    std::ifstream input(file.physical_path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to open VFS file: " + file.physical_path.string());
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error("failed to size VFS file");
    std::string data(static_cast<std::size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    if (!data.empty()) input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input && !data.empty()) throw std::runtime_error("failed to read VFS file");
    return data;
}

} // namespace thunder
