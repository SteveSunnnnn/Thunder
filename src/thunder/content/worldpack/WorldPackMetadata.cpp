#include "thunder/content/worldpack/WorldPackMetadata.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace thunder {
namespace {

std::string_view as_text(std::span<const std::byte> payload) {
    if (payload.empty()) throw std::runtime_error("empty world metadata chunk");
    return {reinterpret_cast<const char*>(payload.data()), payload.size()};
}

std::size_t value_start(std::string_view text, std::string_view key) {
    const auto quoted = std::string{"\""} + std::string{key} + "\"";
    const auto key_pos = text.find(quoted);
    if (key_pos == std::string_view::npos) throw std::runtime_error("world metadata is missing '" + std::string{key} + "'");
    const auto colon = text.find(':', key_pos + quoted.size());
    if (colon == std::string_view::npos) throw std::runtime_error("world metadata key has no value: " + std::string{key});
    auto pos = colon + 1u;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) ++pos;
    return pos;
}

double number(std::string_view text, std::string_view key) {
    const auto pos = value_start(text, key);
    std::string value{text.substr(pos)};
    char* end = nullptr;
    const auto parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(parsed)) throw std::runtime_error("invalid numeric world metadata value: " + std::string{key});
    return parsed;
}

std::uint32_t uint32_value(std::string_view text, std::string_view key) {
    const auto parsed = number(text, key);
    if (parsed < 0.0 || parsed > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) || std::floor(parsed) != parsed)
        throw std::runtime_error("invalid unsigned world metadata value: " + std::string{key});
    return static_cast<std::uint32_t>(parsed);
}

std::uint32_t optional_uint32_value(std::string_view text, std::string_view key,
                                    std::uint32_t fallback) {
    if (text.find(std::string{"\""} + std::string{key} + "\"") == std::string_view::npos)
        return fallback;
    return uint32_value(text, key);
}

std::int32_t int32_value(std::string_view text, std::string_view key) {
    const auto parsed = number(text, key);
    if (parsed < static_cast<double>(std::numeric_limits<std::int32_t>::min()) || parsed > static_cast<double>(std::numeric_limits<std::int32_t>::max()) || std::floor(parsed) != parsed)
        throw std::runtime_error("invalid signed world metadata value: " + std::string{key});
    return static_cast<std::int32_t>(parsed);
}

std::array<double, 4> array4(std::string_view text, std::string_view key) {
    auto pos = value_start(text, key);
    if (pos >= text.size() || text[pos] != '[') throw std::runtime_error("world metadata array expected: " + std::string{key});
    std::array<double, 4> result{};
    for (auto& item : result) {
        ++pos;
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n' || text[pos] == ',')) ++pos;
        std::string value{text.substr(pos)};
        char* end = nullptr;
        item = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || !std::isfinite(item)) throw std::runtime_error("invalid world metadata bounds: " + std::string{key});
        pos += static_cast<std::size_t>(end - value.c_str());
    }
    return result;
}

bool boolean(std::string_view text, std::string_view key) {
    const auto pos = value_start(text, key);
    if (text.substr(pos, 4u) == "true") return true;
    if (text.substr(pos, 5u) == "false") return false;
    throw std::runtime_error("invalid boolean world metadata value: " + std::string{key});
}

std::string string_value(std::string_view text, std::string_view key) {
    const auto pos = value_start(text, key);
    if (pos >= text.size() || text[pos] != '"') throw std::runtime_error("invalid string world metadata value: " + std::string{key});
    const auto end = text.find('"', pos + 1u);
    if (end == std::string_view::npos) throw std::runtime_error("unterminated world metadata string: " + std::string{key});
    return std::string{text.substr(pos + 1u, end - pos - 1u)};
}

} // namespace

bool WorldPackMetadata::valid() const noexcept {
    if (schema_version == 0u || projection.empty() || page_size == 0u || clip_levels == 0u ||
        clip_levels > 65'535u || base_page_count_x == 0u || base_page_count_y == 0u)
        return false;
    if (base_page_count_x > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        base_page_count_y > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return false;
    if (!(base_page_world_size_m > 0.0) || !std::isfinite(base_page_world_size_m)) return false;
    for (const auto value : bounds_wgs84) if (!std::isfinite(value)) return false;
    for (const auto value : bounds_world_m) if (!std::isfinite(value)) return false;
    if (bounds_wgs84[0] < -180.0 || bounds_wgs84[2] > 180.0 ||
        bounds_wgs84[1] < -90.0 || bounds_wgs84[3] > 90.0)
        return false;
    if (projection == "mercator" &&
        (bounds_wgs84[1] < -85.05113 || bounds_wgs84[3] > 85.05113)) return false;
    if ((projection == "gall_stereographic" || projection == "gall-stereographic" || projection == "gall") &&
        (bounds_wgs84[1] < -89.99 || bounds_wgs84[3] > 89.99)) return false;
    if (horizontal_wrap && std::abs((bounds_wgs84[2] - bounds_wgs84[0]) - 360.0) > 1.0e-6)
        return false;
    return bounds_wgs84[0] < bounds_wgs84[2] && bounds_wgs84[1] < bounds_wgs84[3]
        && bounds_world_m[0] < bounds_world_m[2] && bounds_world_m[1] < bounds_world_m[3];
}

std::uint32_t WorldPackMetadata::page_count_x(std::uint32_t level) const noexcept {
    if (level >= clip_levels) return 0u;
    const auto divisor = std::uint64_t{1} << std::min(level, 31u);
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(base_page_count_x) + divisor - 1u) / divisor);
}

std::uint32_t WorldPackMetadata::page_count_y(std::uint32_t level) const noexcept {
    if (level >= clip_levels) return 0u;
    const auto divisor = std::uint64_t{1} << std::min(level, 31u);
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(base_page_count_y) + divisor - 1u) / divisor);
}

std::string world_pack_metadata_json(const WorldPackMetadata& metadata) {
    if (!metadata.valid()) throw std::invalid_argument("cannot encode invalid world metadata");
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"schema_version\": " << metadata.schema_version << ",\n"
        << "  \"projection\": \"" << metadata.projection << "\",\n"
        << "  \"bounds_wgs84\": [" << metadata.bounds_wgs84[0] << ", " << metadata.bounds_wgs84[1] << ", " << metadata.bounds_wgs84[2] << ", " << metadata.bounds_wgs84[3] << "],\n"
        << "  \"bounds_world_m\": [" << metadata.bounds_world_m[0] << ", " << metadata.bounds_world_m[1] << ", " << metadata.bounds_world_m[2] << ", " << metadata.bounds_world_m[3] << "],\n"
        << "  \"horizontal_wrap\": " << (metadata.horizontal_wrap ? "true" : "false") << ",\n"
        << "  \"page_size\": " << metadata.page_size << ",\n"
        << "  \"base_page_world_size_m\": " << metadata.base_page_world_size_m << ",\n"
        << "  \"clip_levels\": " << metadata.clip_levels << ",\n"
        << "  \"page_origin_x\": " << metadata.page_origin_x << ",\n"
        << "  \"page_origin_y\": " << metadata.page_origin_y << ",\n"
        << "  \"base_page_count_x\": " << metadata.base_page_count_x << ",\n"
        << "  \"base_page_count_y\": " << metadata.base_page_count_y << ",\n"
        << "  \"province_count\": " << metadata.province_count << ",\n"
        << "  \"state_count\": " << metadata.state_count << ",\n"
        << "  \"country_count\": " << metadata.country_count << ",\n"
        << "  \"area_count\": " << metadata.area_count << ",\n"
        << "  \"trade_province_count\": " << metadata.trade_province_count << ",\n"
        << "  \"location_count\": " << metadata.location_count << ",\n"
        << "  \"sea_count\": " << metadata.sea_count << ",\n"
        << "  \"lake_count\": " << metadata.lake_count << ",\n"
        << "  \"authored_hub_count\": " << metadata.authored_hub_count << "\n"
        << "}\n";
    return out.str();
}

WorldPackMetadata parse_world_pack_metadata(std::span<const std::byte> payload) {
    const auto text = as_text(payload);
    WorldPackMetadata metadata;
    metadata.schema_version = uint32_value(text, "schema_version");
    metadata.projection = string_value(text, "projection");
    metadata.bounds_wgs84 = array4(text, "bounds_wgs84");
    metadata.bounds_world_m = array4(text, "bounds_world_m");
    metadata.horizontal_wrap = boolean(text, "horizontal_wrap");
    metadata.page_size = uint32_value(text, "page_size");
    metadata.base_page_world_size_m = number(text, "base_page_world_size_m");
    metadata.clip_levels = uint32_value(text, "clip_levels");
    metadata.page_origin_x = int32_value(text, "page_origin_x");
    metadata.page_origin_y = int32_value(text, "page_origin_y");
    metadata.base_page_count_x = uint32_value(text, "base_page_count_x");
    metadata.base_page_count_y = uint32_value(text, "base_page_count_y");
    metadata.province_count = uint32_value(text, "province_count");
    metadata.state_count = uint32_value(text, "state_count");
    metadata.country_count = uint32_value(text, "country_count");
    metadata.area_count = optional_uint32_value(text, "area_count", 0u);
    metadata.trade_province_count = optional_uint32_value(text, "trade_province_count", 0u);
    metadata.location_count = optional_uint32_value(text, "location_count", metadata.province_count);
    metadata.sea_count = uint32_value(text, "sea_count");
    metadata.lake_count = uint32_value(text, "lake_count");
    metadata.authored_hub_count = uint32_value(text, "authored_hub_count");
    if (!metadata.valid()) throw std::runtime_error("invalid world metadata contract");
    return metadata;
}

} // namespace thunder
