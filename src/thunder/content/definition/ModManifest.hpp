#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

using ModStableId = std::uint64_t;

// Mod IDs are canonical, lower-case package names (for example
// "studio.campaign-pack"). The stable ID is derived only from that canonical
// text and is therefore independent of discovery/insertion order.
[[nodiscard]] ModStableId stable_mod_id(std::string_view canonical_id) noexcept;
[[nodiscard]] bool is_valid_mod_id(std::string_view id) noexcept;

struct ModVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string prerelease;
    std::string build_metadata;

    [[nodiscard]] static std::optional<ModVersion> parse(std::string_view text);
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::strong_ordering compare_precedence(const ModVersion& other) const noexcept;

    // Semantic-version build metadata does not participate in precedence.
    friend bool operator==(const ModVersion& a, const ModVersion& b) noexcept {
        return a.compare_precedence(b) == std::strong_ordering::equal;
    }
    friend std::strong_ordering operator<=>(const ModVersion& a, const ModVersion& b) noexcept {
        return a.compare_precedence(b);
    }
};

[[nodiscard]] std::uint64_t stable_mod_version_key(const ModVersion& version);

enum class ModVersionComparator : std::uint8_t {
    Equal,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
};

struct ModVersionPredicate {
    ModVersionComparator comparator = ModVersionComparator::Equal;
    ModVersion version;
};

// A conjunction of semantic-version predicates. Empty means any version.
// The parser accepts *, exact versions, =, ==, <, <=, >, >=, ^ and ~.
struct ModVersionRequirement {
    std::vector<ModVersionPredicate> predicates;

    [[nodiscard]] static std::optional<ModVersionRequirement> parse(
        std::string_view text, std::string* error = nullptr);
    [[nodiscard]] bool matches(const ModVersion& version) const noexcept;
    [[nodiscard]] std::string canonical() const;
};

struct ModDependency {
    std::string id;
    ModVersionRequirement version;
};

struct ModManifest {
    std::string id;
    ModStableId stable_id = 0;
    ModVersion version;
    std::int32_t load_priority = 0;
    std::vector<ModDependency> required_dependencies;
    std::vector<ModDependency> optional_dependencies;
    std::vector<ModDependency> conflicts;
    std::vector<std::string> load_before;
    std::vector<std::string> load_after;
    std::string source_name;
};

enum class ModDiagnosticSeverity : std::uint8_t { Warning, Error };

enum class ModDiagnosticCode : std::uint8_t {
    InvalidSyntax,
    UnknownField,
    DuplicateField,
    InvalidModId,
    InvalidVersion,
    InvalidVersionRequirement,
    InvalidPriority,
    DuplicateRelationship,
    DuplicateModId,
    StableIdMismatch,
    StableIdCollision,
    MissingRequiredDependency,
    RequiredVersionMismatch,
    OptionalVersionMismatch,
    Conflict,
    DependencyCycle,
};

struct ModDiagnostic {
    ModDiagnosticSeverity severity = ModDiagnosticSeverity::Error;
    ModDiagnosticCode code = ModDiagnosticCode::InvalidSyntax;
    std::string source;
    std::uint32_t line = 0;
    std::string mod_id;
    std::string related_mod_id;
    std::string message;
};

struct ModManifestParseResult {
    ModManifest manifest;
    std::vector<ModDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept;
};

// Manifest syntax is a deliberately small, tool-friendly key/value format.
// Relationship keys may be repeated; see docs/MOD_RUNTIME.md.
[[nodiscard]] ModManifestParseResult parse_mod_manifest(
    std::string_view source, std::string_view source_name = {});

// Package content hashes are supplied by the package/catalog layer after it has
// hashed the package bytes. Filesystem paths never participate in deterministic
// planning or the compatibility hash.
struct ModPackage {
    ModManifest manifest;
    std::filesystem::path content_root;
    std::uint64_t package_content_hash = 0;
};

struct ModLoadEntry {
    ModManifest manifest;
    std::filesystem::path content_root;
    std::uint64_t package_content_hash = 0;
    std::uint32_t load_index = 0;
};

struct ModLoadPlan {
    std::vector<ModLoadEntry> entries;
    std::vector<ModDiagnostic> diagnostics;
    std::uint64_t content_hash = 0;

    [[nodiscard]] bool ok() const noexcept;
};

// Builds a deterministic dependency-respecting total order. Registration order
// is not used as a tie breaker; ready packages are ordered by explicit priority
// and then canonical ID.
[[nodiscard]] ModLoadPlan build_mod_load_plan(std::span<const ModPackage> packages);

} // namespace thunder
