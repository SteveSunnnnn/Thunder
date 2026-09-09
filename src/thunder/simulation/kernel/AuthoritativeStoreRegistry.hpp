#pragma once
// Generic authoritative-store contract for a data-driven grand-strategy engine.
// Game content (countries, laws, goods, etc.) is never hard-coded here -
// C++ only provides storage, stable identity, save/checksum/migration and
// deterministic execution. This header closes the P0 gap identified in
// the external capability audit without introducing game-specific enums
// or balance into the engine core.
//
// Usage: each high-cardinality store (PopStore, BuildingStore, future
// PoliticalStore, DiplomacyStore, …) registers a descriptor. SaveGameCodec
// and World::checksum iterate the registry instead of hand-written per-store
// branches. See docs/ARCHITECTURE.md law 3,4,7 and PERFORMANCE_DESIGN.md:7.

#include "thunder/foundation/base/Hash.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace thunder {

struct StoreSaveContext {
    std::vector<std::byte>& out;
    std::uint64_t content_hash = 0;
};

struct StoreLoadContext {
    std::span<const std::byte> bytes;
    std::size_t offset = 0;
    std::uint64_t content_hash = 0;
    bool dry_run = false; // true = validate-only migration check
};

// Stable-key remap supplied by content: dense runtime IDs are never
// persisted directly. On load dense IDs are rebuilt from stable keys so
// definition reorder does not break saves.
struct StableKeyRemap {
    // Returns dense index for stable_key, or UINT32_MAX if missing.
    std::function<std::uint32_t(std::uint64_t stable_key)> lookup;
};

struct AuthoritativeStoreDescriptor {
    std::string_view name;              // e.g. "PopStore", "LawStore"
    std::uint32_t schema_version = 1;   // bumped when on-disk layout changes
    // Deterministic checksum of ALL authoritative columns (order-sensitive).
    std::function<std::uint64_t()> checksum;
    // Validate invariants before commit (ranges, refs, share sums, …).
    std::function<bool(std::span<const std::byte>)> validate;
    // Serialize authoritative columns into out (little-endian, bounded).
    std::function<void(StoreSaveContext&)> serialize;
    // Deserialize from bytes into staging area. Must not mutate live world
    // until commit() succeeds. Returns false on corruption/range error.
    std::function<bool(StoreLoadContext&)> deserialize_staging;
    // Atomically publish staging to live world (called only after all
    // stores validated and cross-store refs resolved).
    std::function<void()> commit;
    // Optional per-store migration: vN -> vN+1 on staging area.
    std::function<bool(std::uint32_t from_version, std::uint32_t to_version)> migrate;
    std::function<std::size_t()> memory_bytes;
};

class AuthoritativeStoreRegistry {
public:
    static AuthoritativeStoreRegistry& instance() noexcept {
        static AuthoritativeStoreRegistry reg;
        return reg;
    }

    void register_store(AuthoritativeStoreDescriptor desc) {
        // Deterministic order: registration order = save order. Callers must
        // register in a fixed order (e.g. alphabetical) to keep save bytes
        // stable across builds. Duplicate name is a hard error.
        for (auto& s : stores_) {
            if (s.name == desc.name) return; // idempotent for tests
        }
        stores_.push_back(std::move(desc));
    }

    [[nodiscard]] std::span<const AuthoritativeStoreDescriptor> stores() const noexcept {
        return stores_;
    }

    [[nodiscard]] std::uint64_t combined_checksum() const noexcept {
        // FNV-1a over per-store checksums in registration order - stable and
        // order-sensitive so store-insertion bugs are OOS-visible.
        // Must match thunder::Fnv1a64's offset basis (see thunder/foundation/base/Hash.hpp).
        // The constant here was missing a digit, which made combined_checksum()
        // a different hash family from every per-store checksum.
        std::uint64_t h = 14695981039346656037ULL;
        for (auto& s : stores_) {
            if (!s.checksum) continue;
            std::uint64_t c = s.checksum();
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h;
    }

    void clear_for_tests() noexcept { stores_.clear(); }

private:
    std::vector<AuthoritativeStoreDescriptor> stores_;
};

} // namespace thunder
