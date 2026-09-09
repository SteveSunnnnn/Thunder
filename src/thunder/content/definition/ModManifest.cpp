#include "thunder/content/definition/ModManifest.hpp"

#include "thunder/foundation/base/Hash.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace thunder {
namespace {

constexpr std::size_t max_mod_id_length = 128u;

[[nodiscard]] bool ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] bool ascii_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
[[nodiscard]] bool identifier_char(char c) noexcept {
    return ascii_alpha(c) || ascii_digit(c) || c == '-';
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r' || value.front() == '\n')) {
        value.remove_prefix(1u);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '\r' || value.back() == '\n')) {
        value.remove_suffix(1u);
    }
    return value;
}

[[nodiscard]] bool valid_dot_identifiers(std::string_view text,
                                         bool reject_numeric_leading_zero) noexcept {
    if (text.empty()) return false;
    std::size_t begin = 0u;
    while (begin < text.size()) {
        const auto end = text.find('.', begin);
        const auto part = text.substr(begin, end == std::string_view::npos
                                                ? text.size() - begin
                                                : end - begin);
        if (part.empty()) return false;
        bool numeric = true;
        for (const char c : part) {
            if (!identifier_char(c)) return false;
            numeric = numeric && ascii_digit(c);
        }
        if (reject_numeric_leading_zero && numeric && part.size() > 1u && part.front() == '0') {
            return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1u;
    }
    return true;
}

[[nodiscard]] bool parse_u32_component(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty() || (text.size() > 1u && text.front() == '0')) return false;
    for (const char c : text) if (!ascii_digit(c)) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

[[nodiscard]] std::strong_ordering compare_identifier(std::string_view a,
                                                       std::string_view b) noexcept {
    const bool a_numeric = !a.empty() && std::all_of(a.begin(), a.end(), ascii_digit);
    const bool b_numeric = !b.empty() && std::all_of(b.begin(), b.end(), ascii_digit);
    if (a_numeric != b_numeric) return a_numeric ? std::strong_ordering::less
                                                 : std::strong_ordering::greater;
    if (a_numeric) {
        if (a.size() != b.size()) return a.size() < b.size() ? std::strong_ordering::less
                                                             : std::strong_ordering::greater;
    }
    if (a < b) return std::strong_ordering::less;
    if (a > b) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

[[nodiscard]] std::string_view next_identifier(std::string_view text,
                                                std::size_t& cursor) noexcept {
    if (cursor >= text.size()) return {};
    const auto end = text.find('.', cursor);
    const auto result = text.substr(cursor, end == std::string_view::npos
                                               ? text.size() - cursor
                                               : end - cursor);
    cursor = end == std::string_view::npos ? text.size() : end + 1u;
    return result;
}

[[nodiscard]] const char* comparator_text(ModVersionComparator comparator) noexcept {
    switch (comparator) {
    case ModVersionComparator::Equal: return "=";
    case ModVersionComparator::Greater: return ">";
    case ModVersionComparator::GreaterOrEqual: return ">=";
    case ModVersionComparator::Less: return "<";
    case ModVersionComparator::LessOrEqual: return "<=";
    }
    return "=";
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

[[nodiscard]] bool increment(std::uint32_t value, std::uint32_t& result) noexcept {
    if (value == std::numeric_limits<std::uint32_t>::max()) return false;
    result = value + 1u;
    return true;
}

[[nodiscard]] std::string_view strip_comment(std::string_view line) noexcept {
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c == '#') return line.substr(0u, i);
        if (c == '/' && i + 1u < line.size() && line[i + 1u] == '/') {
            return line.substr(0u, i);
        }
    }
    return line;
}

[[nodiscard]] std::optional<std::string> decode_value(std::string_view raw,
                                                       std::string& error) {
    raw = trim(raw);
    if (raw.empty()) {
        error = "value must not be empty";
        return std::nullopt;
    }
    if (raw.front() != '"') return std::string{raw};

    std::string result;
    result.reserve(raw.size());
    bool escaped = false;
    // Track the closing quote explicitly. The old test inspected raw[i - 1]
    // after the loop, which for a lone `"` never entered the loop and happened
    // to point at the opening quote — so it was accepted as an empty value and
    // failed later with a misleading "invalid mod id".
    bool closed = false;
    std::size_t i = 1u;
    for (; i < raw.size(); ++i) {
        const char c = raw[i];
        if (escaped) {
            switch (c) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default:
                error = "unsupported escape sequence in quoted value";
                return std::nullopt;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            ++i;
            closed = true;
            break;
        } else {
            result.push_back(c);
        }
    }
    if (escaped || !closed) {
        error = "unterminated quoted value";
        return std::nullopt;
    }
    if (!trim(raw.substr(i)).empty()) {
        error = "unexpected text after quoted value";
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<ModDependency> parse_dependency_value(
    std::string_view value, std::string& error) {
    value = trim(value);
    if (value.empty()) {
        error = "dependency value must not be empty";
        return std::nullopt;
    }

    std::size_t split = 0u;
    while (split < value.size() && value[split] != '@' && value[split] != ' ' &&
           value[split] != '\t') {
        ++split;
    }
    ModDependency dependency;
    dependency.id = std::string{value.substr(0u, split)};
    if (!is_valid_mod_id(dependency.id)) {
        error = "invalid dependency mod id '" + dependency.id + "'";
        return std::nullopt;
    }

    std::string_view requirement = trim(value.substr(split));
    if (!requirement.empty() && requirement.front() == '@') {
        requirement.remove_prefix(1u);
        requirement = trim(requirement);
    }
    if (requirement.empty()) requirement = "*";
    if (auto parsed = ModVersionRequirement::parse(requirement, &error)) {
        dependency.version = std::move(*parsed);
        return dependency;
    }
    return std::nullopt;
}

void hash_u32(Fnv1a64& hash, std::uint32_t value) noexcept {
    for (std::uint32_t shift = 0u; shift < 32u; shift += 8u) {
        const auto byte = static_cast<std::byte>((value >> shift) & 0xffu);
        hash.add_bytes(std::span{&byte, std::size_t{1}});
    }
}

void hash_u64(Fnv1a64& hash, std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0u; shift < 64u; shift += 8u) {
        const auto byte = static_cast<std::byte>((value >> shift) & 0xffu);
        hash.add_bytes(std::span{&byte, std::size_t{1}});
    }
}

void hash_text(Fnv1a64& hash, std::string_view text) noexcept {
    hash_u64(hash, static_cast<std::uint64_t>(text.size()));
    hash.add(text);
}

[[nodiscard]] bool has_errors(std::span<const ModDiagnostic> diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const ModDiagnostic& diagnostic) {
        return diagnostic.severity == ModDiagnosticSeverity::Error;
    });
}

void sort_diagnostics(std::vector<ModDiagnostic>& diagnostics) {
    std::sort(diagnostics.begin(), diagnostics.end(), [](const ModDiagnostic& a,
                                                         const ModDiagnostic& b) {
        return std::tie(a.severity, a.code, a.mod_id, a.related_mod_id, a.source, a.line,
                        a.message) <
               std::tie(b.severity, b.code, b.mod_id, b.related_mod_id, b.source, b.line,
                        b.message);
    });
}

[[nodiscard]] std::string dependency_key(const ModDependency& dependency) {
    return dependency.id + "@" + dependency.version.canonical();
}

void hash_manifest_relationships(Fnv1a64& hash, const ModManifest& manifest) {
    const auto hash_dependencies = [&hash](const std::vector<ModDependency>& dependencies,
                                           std::string_view tag) {
        std::vector<std::string> canonical;
        canonical.reserve(dependencies.size());
        for (const auto& dependency : dependencies) {
            canonical.push_back(dependency_key(dependency));
        }
        std::sort(canonical.begin(), canonical.end());
        hash_text(hash, tag);
        hash_u64(hash, static_cast<std::uint64_t>(canonical.size()));
        for (const auto& item : canonical) hash_text(hash, item);
    };
    const auto hash_ids = [&hash](const std::vector<std::string>& ids, std::string_view tag) {
        auto canonical = ids;
        std::sort(canonical.begin(), canonical.end());
        hash_text(hash, tag);
        hash_u64(hash, static_cast<std::uint64_t>(canonical.size()));
        for (const auto& item : canonical) hash_text(hash, item);
    };

    hash_dependencies(manifest.required_dependencies, "required");
    hash_dependencies(manifest.optional_dependencies, "optional");
    hash_dependencies(manifest.conflicts, "conflicts");
    hash_ids(manifest.load_before, "load_before");
    hash_ids(manifest.load_after, "load_after");
}

} // namespace

ModStableId stable_mod_id(std::string_view canonical_id) noexcept {
    Fnv1a64 hash;
    hash.add(canonical_id);
    return hash.value();
}

bool is_valid_mod_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > max_mod_id_length) return false;
    if (id.front() == '.' || id.front() == '-' || id.front() == '_' ||
        id.back() == '.' || id.back() == '-' || id.back() == '_') {
        return false;
    }
    bool previous_dot = false;
    for (const char c : id) {
        const bool valid = (c >= 'a' && c <= 'z') || ascii_digit(c) || c == '.' ||
                           c == '-' || c == '_';
        if (!valid || (c == '.' && previous_dot)) return false;
        previous_dot = c == '.';
    }
    return true;
}

std::optional<ModVersion> ModVersion::parse(std::string_view text) {
    text = trim(text);
    if (text.empty()) return std::nullopt;

    ModVersion result;
    const auto plus = text.find('+');
    if (plus != std::string_view::npos) {
        if (text.find('+', plus + 1u) != std::string_view::npos) return std::nullopt;
        result.build_metadata = std::string{text.substr(plus + 1u)};
        if (!valid_dot_identifiers(result.build_metadata, false)) return std::nullopt;
        text = text.substr(0u, plus);
    }
    const auto dash = text.find('-');
    if (dash != std::string_view::npos) {
        result.prerelease = std::string{text.substr(dash + 1u)};
        if (!valid_dot_identifiers(result.prerelease, true)) return std::nullopt;
        text = text.substr(0u, dash);
    }

    const auto first_dot = text.find('.');
    if (first_dot == std::string_view::npos) return std::nullopt;
    const auto second_dot = text.find('.', first_dot + 1u);
    if (second_dot == std::string_view::npos ||
        text.find('.', second_dot + 1u) != std::string_view::npos) {
        return std::nullopt;
    }
    if (!parse_u32_component(text.substr(0u, first_dot), result.major) ||
        !parse_u32_component(text.substr(first_dot + 1u, second_dot - first_dot - 1u),
                             result.minor) ||
        !parse_u32_component(text.substr(second_dot + 1u), result.patch)) {
        return std::nullopt;
    }
    return result;
}

std::string ModVersion::to_string() const {
    std::string result = std::to_string(major) + "." + std::to_string(minor) + "." +
                         std::to_string(patch);
    if (!prerelease.empty()) result += "-" + prerelease;
    if (!build_metadata.empty()) result += "+" + build_metadata;
    return result;
}

std::strong_ordering ModVersion::compare_precedence(const ModVersion& other) const noexcept {
    if (major != other.major) return major < other.major ? std::strong_ordering::less
                                                         : std::strong_ordering::greater;
    if (minor != other.minor) return minor < other.minor ? std::strong_ordering::less
                                                         : std::strong_ordering::greater;
    if (patch != other.patch) return patch < other.patch ? std::strong_ordering::less
                                                         : std::strong_ordering::greater;
    if (prerelease.empty() != other.prerelease.empty()) {
        return prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    if (prerelease.empty()) return std::strong_ordering::equal;

    std::size_t a_cursor = 0u;
    std::size_t b_cursor = 0u;
    for (;;) {
        const bool a_done = a_cursor >= prerelease.size();
        const bool b_done = b_cursor >= other.prerelease.size();
        if (a_done || b_done) {
            if (a_done == b_done) return std::strong_ordering::equal;
            return a_done ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        const auto comparison = compare_identifier(next_identifier(prerelease, a_cursor),
                                                   next_identifier(other.prerelease, b_cursor));
        if (comparison != std::strong_ordering::equal) return comparison;
    }
}

std::uint64_t stable_mod_version_key(const ModVersion& version) {
    Fnv1a64 hash;
    const auto canonical = version.to_string();
    hash.add(std::string_view{canonical});
    return hash.value();
}

std::optional<ModVersionRequirement> ModVersionRequirement::parse(std::string_view text,
                                                                  std::string* error) {
    if (error != nullptr) error->clear();
    text = trim(text);
    if (text.empty() || text == "*") return ModVersionRequirement{};

    ModVersionRequirement result;
    std::size_t cursor = 0u;
    while (cursor < text.size()) {
        while (cursor < text.size() &&
               (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == ',')) {
            ++cursor;
        }
        if (cursor >= text.size()) break;

        std::string_view operator_text;
        if (cursor + 1u < text.size() &&
            (text.substr(cursor, 2u) == ">=" || text.substr(cursor, 2u) == "<=" ||
             text.substr(cursor, 2u) == "==")) {
            operator_text = text.substr(cursor, 2u);
            cursor += 2u;
        } else if (text[cursor] == '=' || text[cursor] == '>' || text[cursor] == '<' ||
                   text[cursor] == '^' || text[cursor] == '~') {
            operator_text = text.substr(cursor, 1u);
            ++cursor;
        }
        while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t')) ++cursor;
        const auto version_begin = cursor;
        while (cursor < text.size() && text[cursor] != ' ' && text[cursor] != '\t' &&
               text[cursor] != ',') {
            ++cursor;
        }
        if (version_begin == cursor) {
            set_error(error, "version comparator is missing a version");
            return std::nullopt;
        }
        const auto version_text = text.substr(version_begin, cursor - version_begin);
        const auto version = ModVersion::parse(version_text);
        if (!version) {
            set_error(error, "invalid semantic version '" + std::string{version_text} + "'");
            return std::nullopt;
        }

        if (operator_text == "^") {
            result.predicates.push_back({ModVersionComparator::GreaterOrEqual, *version});
            ModVersion upper;
            bool ok = false;
            if (version->major != 0u) {
                ok = increment(version->major, upper.major);
            } else if (version->minor != 0u) {
                upper.major = 0u;
                ok = increment(version->minor, upper.minor);
            } else {
                upper.major = 0u;
                upper.minor = 0u;
                ok = increment(version->patch, upper.patch);
            }
            if (!ok) {
                set_error(error, "caret range upper bound overflows");
                return std::nullopt;
            }
            result.predicates.push_back({ModVersionComparator::Less, std::move(upper)});
        } else if (operator_text == "~") {
            result.predicates.push_back({ModVersionComparator::GreaterOrEqual, *version});
            ModVersion upper;
            upper.major = version->major;
            if (!increment(version->minor, upper.minor)) {
                set_error(error, "tilde range upper bound overflows");
                return std::nullopt;
            }
            result.predicates.push_back({ModVersionComparator::Less, std::move(upper)});
        } else {
            ModVersionComparator comparator = ModVersionComparator::Equal;
            if (operator_text == ">") comparator = ModVersionComparator::Greater;
            else if (operator_text == ">=") comparator = ModVersionComparator::GreaterOrEqual;
            else if (operator_text == "<") comparator = ModVersionComparator::Less;
            else if (operator_text == "<=") comparator = ModVersionComparator::LessOrEqual;
            result.predicates.push_back({comparator, *version});
        }
    }
    if (result.predicates.empty()) {
        set_error(error, "version requirement must contain a predicate or '*'");
        return std::nullopt;
    }
    return result;
}

bool ModVersionRequirement::matches(const ModVersion& version) const noexcept {
    for (const auto& predicate : predicates) {
        const auto comparison = version.compare_precedence(predicate.version);
        bool matches_predicate = false;
        switch (predicate.comparator) {
        case ModVersionComparator::Equal:
            matches_predicate = comparison == std::strong_ordering::equal;
            break;
        case ModVersionComparator::Greater:
            matches_predicate = comparison == std::strong_ordering::greater;
            break;
        case ModVersionComparator::GreaterOrEqual:
            matches_predicate = comparison != std::strong_ordering::less;
            break;
        case ModVersionComparator::Less:
            matches_predicate = comparison == std::strong_ordering::less;
            break;
        case ModVersionComparator::LessOrEqual:
            matches_predicate = comparison != std::strong_ordering::greater;
            break;
        }
        if (!matches_predicate) return false;
    }
    return true;
}

std::string ModVersionRequirement::canonical() const {
    if (predicates.empty()) return "*";
    std::vector<std::string> parts;
    parts.reserve(predicates.size());
    for (const auto& predicate : predicates) {
        parts.emplace_back(std::string{comparator_text(predicate.comparator)} +
                           predicate.version.to_string());
    }
    std::sort(parts.begin(), parts.end());
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result.push_back(' ');
        result += part;
    }
    return result;
}

bool ModManifestParseResult::ok() const noexcept { return !has_errors(diagnostics); }

ModManifestParseResult parse_mod_manifest(std::string_view source, std::string_view source_name) {
    ModManifestParseResult result;
    result.manifest.source_name = std::string{source_name};
    if (source.size() >= 3u && static_cast<unsigned char>(source[0]) == 0xefu &&
        static_cast<unsigned char>(source[1]) == 0xbbu &&
        static_cast<unsigned char>(source[2]) == 0xbfu) {
        source.remove_prefix(3u);
    }
    bool has_id = false;
    bool has_version = false;
    bool has_priority = false;

    const auto report = [&](ModDiagnosticCode code, std::uint32_t line, std::string message) {
        result.diagnostics.push_back({ModDiagnosticSeverity::Error, code,
                                      std::string{source_name}, line, result.manifest.id, {},
                                      std::move(message)});
    };

    std::size_t cursor = 0u;
    std::uint32_t line_number = 1u;
    while (cursor <= source.size()) {
        const auto line_end = source.find('\n', cursor);
        auto line = source.substr(cursor, line_end == std::string_view::npos
                                             ? source.size() - cursor
                                             : line_end - cursor);
        line = trim(strip_comment(line));
        if (!line.empty()) {
            bool quoted = false;
            bool escaped = false;
            std::size_t equals = std::string_view::npos;
            for (std::size_t i = 0u; i < line.size(); ++i) {
                if (quoted) {
                    if (escaped) escaped = false;
                    else if (line[i] == '\\') escaped = true;
                    else if (line[i] == '"') quoted = false;
                } else if (line[i] == '"') {
                    quoted = true;
                } else if (line[i] == '=') {
                    equals = i;
                    break;
                }
            }
            if (equals == std::string_view::npos) {
                report(ModDiagnosticCode::InvalidSyntax, line_number,
                       "expected 'key = value'");
            } else {
                const auto key = trim(line.substr(0u, equals));
                std::string decode_error;
                const auto value = decode_value(line.substr(equals + 1u), decode_error);
                if (!value) {
                    report(ModDiagnosticCode::InvalidSyntax, line_number, std::move(decode_error));
                } else if (key == "id") {
                    if (has_id) {
                        report(ModDiagnosticCode::DuplicateField, line_number,
                               "manifest id may be declared only once");
                    } else {
                        has_id = true;
                        result.manifest.id = *value;
                        if (!is_valid_mod_id(result.manifest.id)) {
                            report(ModDiagnosticCode::InvalidModId, line_number,
                                   "mod id must be a canonical lower-case package name");
                        } else {
                            result.manifest.stable_id = stable_mod_id(result.manifest.id);
                        }
                    }
                } else if (key == "version") {
                    if (has_version) {
                        report(ModDiagnosticCode::DuplicateField, line_number,
                               "manifest version may be declared only once");
                    } else {
                        has_version = true;
                        const auto parsed = ModVersion::parse(*value);
                        if (!parsed) {
                            report(ModDiagnosticCode::InvalidVersion, line_number,
                                   "version must be a valid semantic version (major.minor.patch)");
                        } else {
                            result.manifest.version = *parsed;
                        }
                    }
                } else if (key == "load_priority" || key == "priority") {
                    if (has_priority) {
                        report(ModDiagnosticCode::DuplicateField, line_number,
                               "load_priority may be declared only once");
                    } else {
                        has_priority = true;
                        const auto priority_text = trim(std::string_view{*value});
                        std::int32_t priority = 0;
                        const auto parsed = std::from_chars(priority_text.data(),
                                                            priority_text.data() + priority_text.size(),
                                                            priority);
                        if (parsed.ec != std::errc{} ||
                            parsed.ptr != priority_text.data() + priority_text.size()) {
                            report(ModDiagnosticCode::InvalidPriority, line_number,
                                   "load_priority must be a signed 32-bit integer");
                        } else {
                            result.manifest.load_priority = priority;
                        }
                    }
                } else if (key == "required" || key == "required_dependency" ||
                           key == "optional" || key == "optional_dependency" ||
                           key == "conflict" || key == "conflicts") {
                    std::string dependency_error;
                    auto dependency = parse_dependency_value(*value, dependency_error);
                    if (!dependency) {
                        const auto code = dependency_error.starts_with("invalid dependency mod id")
                                              ? ModDiagnosticCode::InvalidModId
                                              : ModDiagnosticCode::InvalidVersionRequirement;
                        report(code, line_number, std::move(dependency_error));
                    } else if (key == "required" || key == "required_dependency") {
                        result.manifest.required_dependencies.push_back(std::move(*dependency));
                    } else if (key == "optional" || key == "optional_dependency") {
                        result.manifest.optional_dependencies.push_back(std::move(*dependency));
                    } else {
                        result.manifest.conflicts.push_back(std::move(*dependency));
                    }
                } else if (key == "load_before" || key == "load_after") {
                    if (!is_valid_mod_id(*value)) {
                        report(ModDiagnosticCode::InvalidModId, line_number,
                               "ordering target must be a canonical mod id");
                    } else if (key == "load_before") {
                        result.manifest.load_before.push_back(*value);
                    } else {
                        result.manifest.load_after.push_back(*value);
                    }
                } else {
                    report(ModDiagnosticCode::UnknownField, line_number,
                           "unknown manifest field '" + std::string{key} + "'");
                }
            }
        }

        if (line_end == std::string_view::npos) break;
        cursor = line_end + 1u;
        ++line_number;
    }

    if (!has_id) report(ModDiagnosticCode::InvalidSyntax, 0u, "manifest is missing required field 'id'");
    if (!has_version) {
        report(ModDiagnosticCode::InvalidSyntax, 0u, "manifest is missing required field 'version'");
    }
    for (auto& diagnostic : result.diagnostics) {
        if (diagnostic.mod_id.empty()) diagnostic.mod_id = result.manifest.id;
    }
    sort_diagnostics(result.diagnostics);
    return result;
}

bool ModLoadPlan::ok() const noexcept { return !has_errors(diagnostics); }

ModLoadPlan build_mod_load_plan(std::span<const ModPackage> packages) {
    struct Node {
        ModPackage package;
    };

    ModLoadPlan plan;
    std::vector<Node> nodes;
    nodes.reserve(packages.size());
    for (const auto& package : packages) nodes.push_back({package});
    std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
        const auto& am = a.package.manifest;
        const auto& bm = b.package.manifest;
        if (am.id != bm.id) return am.id < bm.id;
        if (am.version.to_string() != bm.version.to_string()) {
            return am.version.to_string() < bm.version.to_string();
        }
        if (a.package.package_content_hash != b.package.package_content_hash) {
            return a.package.package_content_hash < b.package.package_content_hash;
        }
        return a.package.content_root.generic_string() < b.package.content_root.generic_string();
    });

    const auto diagnostic = [&plan](ModDiagnosticSeverity severity, ModDiagnosticCode code,
                                    const ModManifest& manifest, std::string related,
                                    std::string message) {
        plan.diagnostics.push_back({severity, code, manifest.source_name, 0u, manifest.id,
                                    std::move(related), std::move(message)});
    };

    for (auto& node : nodes) {
        auto& manifest = node.package.manifest;
        if (!is_valid_mod_id(manifest.id)) {
            diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::InvalidModId, manifest,
                       {}, "invalid canonical mod id '" + manifest.id + "'");
            continue;
        }
        const auto expected = stable_mod_id(manifest.id);
        if (manifest.stable_id != 0u && manifest.stable_id != expected) {
            diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::StableIdMismatch, manifest,
                       {}, "stored stable ID does not match canonical mod id");
        }
        manifest.stable_id = expected;

        std::map<std::string, std::string> relationship_kind;
        const auto validate_dependencies = [&](const std::vector<ModDependency>& dependencies,
                                               std::string_view kind) {
            for (const auto& dependency_value : dependencies) {
                if (!is_valid_mod_id(dependency_value.id)) {
                    diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::InvalidModId,
                               manifest, dependency_value.id,
                               "invalid " + std::string{kind} + " dependency id");
                    continue;
                }
                const auto [it, inserted] = relationship_kind.emplace(dependency_value.id,
                                                                       std::string{kind});
                if (!inserted && (kind != "conflict" || it->second == "conflict")) {
                    diagnostic(ModDiagnosticSeverity::Error,
                               ModDiagnosticCode::DuplicateRelationship, manifest,
                               dependency_value.id,
                               "duplicate or ambiguous relationship with '" +
                                   dependency_value.id + "'");
                }
            }
        };
        validate_dependencies(manifest.required_dependencies, "required");
        validate_dependencies(manifest.optional_dependencies, "optional");
        // A conflict may coexist with a dependency so invalid combinations are
        // diagnosed as a conflict when the selected package set is evaluated.
        std::set<std::string> conflict_ids;
        for (const auto& conflict : manifest.conflicts) {
            if (!is_valid_mod_id(conflict.id)) {
                diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::InvalidModId,
                           manifest, conflict.id, "invalid conflict mod id");
            } else if (!conflict_ids.insert(conflict.id).second) {
                diagnostic(ModDiagnosticSeverity::Error,
                           ModDiagnosticCode::DuplicateRelationship, manifest, conflict.id,
                           "duplicate conflict relationship with '" + conflict.id + "'");
            }
        }

        const auto validate_order_ids = [&](const std::vector<std::string>& ids,
                                            std::string_view kind) {
            std::set<std::string> seen;
            for (const auto& id : ids) {
                if (!is_valid_mod_id(id)) {
                    diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::InvalidModId,
                               manifest, id, "invalid " + std::string{kind} + " target id");
                } else if (!seen.insert(id).second) {
                    diagnostic(ModDiagnosticSeverity::Error,
                               ModDiagnosticCode::DuplicateRelationship, manifest, id,
                               "duplicate " + std::string{kind} + " target '" + id + "'");
                }
            }
        };
        validate_order_ids(manifest.load_before, "load_before");
        validate_order_ids(manifest.load_after, "load_after");
    }

    for (std::size_t i = 1u; i < nodes.size(); ++i) {
        if (nodes[i - 1u].package.manifest.id == nodes[i].package.manifest.id) {
            diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::DuplicateModId,
                       nodes[i - 1u].package.manifest, nodes[i].package.manifest.id,
                       "duplicate package registration for mod id '" +
                           nodes[i].package.manifest.id + "'");
        }
    }

    std::map<ModStableId, std::string> stable_ids;
    for (const auto& node : nodes) {
        const auto& manifest = node.package.manifest;
        if (!is_valid_mod_id(manifest.id)) continue;
        const auto [it, inserted] = stable_ids.emplace(manifest.stable_id, manifest.id);
        if (!inserted && it->second != manifest.id) {
            diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::StableIdCollision,
                       manifest, it->second,
                       "stable mod ID collision between '" + it->second + "' and '" +
                           manifest.id + "'");
        }
    }

    std::map<std::string, std::size_t, std::less<>> index_by_id;
    for (std::size_t i = 0u; i < nodes.size(); ++i) {
        index_by_id.try_emplace(nodes[i].package.manifest.id, i);
    }

    std::set<std::pair<std::size_t, std::size_t>> edges;
    const auto add_edge = [&edges](std::size_t before, std::size_t after) {
        edges.emplace(before, after);
    };

    std::set<std::pair<std::string, std::string>> conflict_pairs;
    for (std::size_t i = 0u; i < nodes.size(); ++i) {
        const auto& manifest = nodes[i].package.manifest;
        const auto apply_dependencies = [&](const std::vector<ModDependency>& dependencies,
                                            bool required) {
            for (const auto& dependency_value : dependencies) {
                const auto found = index_by_id.find(dependency_value.id);
                if (found == index_by_id.end()) {
                    if (required) {
                        diagnostic(ModDiagnosticSeverity::Error,
                                   ModDiagnosticCode::MissingRequiredDependency, manifest,
                                   dependency_value.id,
                                   "required dependency '" + dependency_value.id + "' is missing");
                    }
                    continue;
                }
                const auto& installed = nodes[found->second].package.manifest.version;
                if (!dependency_value.version.matches(installed)) {
                    const auto severity = required ? ModDiagnosticSeverity::Error
                                                   : ModDiagnosticSeverity::Warning;
                    const auto code = required ? ModDiagnosticCode::RequiredVersionMismatch
                                               : ModDiagnosticCode::OptionalVersionMismatch;
                    diagnostic(severity, code, manifest, dependency_value.id,
                               std::string{required ? "required" : "optional"} +
                                   " dependency '" + dependency_value.id + "' requires " +
                                   dependency_value.version.canonical() + ", found " +
                                   installed.to_string());
                    continue;
                }
                add_edge(found->second, i);
            }
        };
        apply_dependencies(manifest.required_dependencies, true);
        apply_dependencies(manifest.optional_dependencies, false);

        for (const auto& id : manifest.load_before) {
            if (const auto found = index_by_id.find(id); found != index_by_id.end()) {
                add_edge(i, found->second);
            }
        }
        for (const auto& id : manifest.load_after) {
            if (const auto found = index_by_id.find(id); found != index_by_id.end()) {
                add_edge(found->second, i);
            }
        }

        for (const auto& conflict : manifest.conflicts) {
            const auto found = index_by_id.find(conflict.id);
            if (found == index_by_id.end() ||
                !conflict.version.matches(nodes[found->second].package.manifest.version)) {
                continue;
            }
            auto pair = std::minmax(manifest.id, conflict.id);
            if (conflict_pairs.emplace(pair.first, pair.second).second) {
                diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::Conflict, manifest,
                           conflict.id, "selected packages '" + pair.first + "' and '" +
                                            pair.second + "' conflict");
            }
        }
    }

    std::vector<std::vector<std::size_t>> adjacency(nodes.size());
    std::vector<std::size_t> indegree(nodes.size(), 0u);
    for (const auto& [before, after] : edges) {
        adjacency[before].push_back(after);
        ++indegree[after];
    }
    for (auto& targets : adjacency) std::sort(targets.begin(), targets.end());

    struct ReadyLess {
        const std::vector<Node>* nodes = nullptr;
        bool operator()(std::size_t a, std::size_t b) const {
            const auto& am = (*nodes)[a].package.manifest;
            const auto& bm = (*nodes)[b].package.manifest;
            if (am.load_priority != bm.load_priority) return am.load_priority < bm.load_priority;
            return am.id < bm.id;
        }
    };
    std::set<std::size_t, ReadyLess> ready{ReadyLess{&nodes}};
    for (std::size_t i = 0u; i < nodes.size(); ++i) if (indegree[i] == 0u) ready.insert(i);

    std::vector<std::size_t> ordered;
    ordered.reserve(nodes.size());
    while (!ready.empty()) {
        const auto node = *ready.begin();
        ready.erase(ready.begin());
        ordered.push_back(node);
        for (const auto target : adjacency[node]) {
            --indegree[target];
            if (indegree[target] == 0u) ready.insert(target);
        }
    }

    if (ordered.size() != nodes.size()) {
        std::vector<std::int64_t> index(nodes.size(), -1);
        std::vector<std::int64_t> low(nodes.size(), -1);
        std::vector<bool> on_stack(nodes.size(), false);
        std::vector<std::size_t> stack;
        std::vector<std::vector<std::size_t>> cycles;
        std::int64_t next_index = 0;

        std::function<void(std::size_t)> visit = [&](std::size_t node) {
            index[node] = next_index;
            low[node] = next_index;
            ++next_index;
            stack.push_back(node);
            on_stack[node] = true;
            for (const auto target : adjacency[node]) {
                if (index[target] < 0) {
                    visit(target);
                    low[node] = std::min(low[node], low[target]);
                } else if (on_stack[target]) {
                    low[node] = std::min(low[node], index[target]);
                }
            }
            if (low[node] != index[node]) return;

            std::vector<std::size_t> component;
            for (;;) {
                const auto member = stack.back();
                stack.pop_back();
                on_stack[member] = false;
                component.push_back(member);
                if (member == node) break;
            }
            const bool self_cycle = component.size() == 1u &&
                                    std::binary_search(adjacency[component.front()].begin(),
                                                       adjacency[component.front()].end(),
                                                       component.front());
            if (component.size() > 1u || self_cycle) {
                std::sort(component.begin(), component.end());
                cycles.push_back(std::move(component));
            }
        };
        for (std::size_t i = 0u; i < nodes.size(); ++i) if (index[i] < 0) visit(i);
        std::sort(cycles.begin(), cycles.end(), [](const auto& a, const auto& b) {
            return a.front() < b.front();
        });
        for (const auto& cycle : cycles) {
            std::string list;
            for (const auto member : cycle) {
                if (!list.empty()) list += ", ";
                list += nodes[member].package.manifest.id;
            }
            const auto& manifest = nodes[cycle.front()].package.manifest;
            diagnostic(ModDiagnosticSeverity::Error, ModDiagnosticCode::DependencyCycle,
                       manifest, {}, "dependency/load-order cycle among: " + list);
        }
    } else {
        plan.entries.reserve(ordered.size());
        for (std::size_t load_index = 0u; load_index < ordered.size(); ++load_index) {
            const auto& package = nodes[ordered[load_index]].package;
            plan.entries.push_back({package.manifest, package.content_root,
                                    package.package_content_hash,
                                    static_cast<std::uint32_t>(load_index)});
        }
    }

    sort_diagnostics(plan.diagnostics);
    if (plan.ok()) {
        Fnv1a64 hash;
        hash_text(hash, "thunder.mod-load-plan.v1");
        hash_u64(hash, static_cast<std::uint64_t>(plan.entries.size()));
        for (const auto& entry : plan.entries) {
            hash_u32(hash, entry.load_index);
            hash_text(hash, entry.manifest.id);
            hash_u64(hash, entry.manifest.stable_id);
            hash_text(hash, entry.manifest.version.to_string());
            hash_u32(hash, static_cast<std::uint32_t>(entry.manifest.load_priority));
            hash_u64(hash, entry.package_content_hash);
            hash_manifest_relationships(hash, entry.manifest);
        }
        plan.content_hash = hash.value();
    }
    return plan;
}

} // namespace thunder
