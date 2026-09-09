#include "thunder/content/definition/ModManifest.hpp"

#include "thunder/foundation/base/Hash.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
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
} // namespace thunder
