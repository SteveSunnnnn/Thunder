#include "thunder/content/assets/GlTFImporter.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace thunder::assets {
namespace {

// ---------------------------------------------------------------------------
// Minimal JSON DOM (glTF manifests are well-formed JSON documents; the engine
// has no other JSON consumer, so this stays deliberately small).
// ---------------------------------------------------------------------------
struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;
struct JsonValue {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value;
    [[nodiscard]] bool is_object() const noexcept { return std::holds_alternative<JsonObject>(value); }
    [[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<JsonArray>(value); }
    [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string>(value); }
    [[nodiscard]] bool is_number() const noexcept { return std::holds_alternative<double>(value); }
    [[nodiscard]] const JsonObject& object() const { return std::get<JsonObject>(value); }
    [[nodiscard]] const JsonArray& array() const { return std::get<JsonArray>(value); }
    [[nodiscard]] const std::string& string() const { return std::get<std::string>(value); }
    [[nodiscard]] double number() const { return std::get<double>(value); }
    [[nodiscard]] bool contains(std::string_view key) const {
        return is_object() && object().count(std::string{key}) != 0u;
    }
    [[nodiscard]] const JsonValue& at(std::string_view key) const {
        const auto& o = object();
        const auto it = o.find(std::string{key});
        if (it == o.end()) throw std::runtime_error("glTF JSON missing key: " + std::string{key});
        return it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}
    [[nodiscard]] JsonValue parse_document() {
        skip_whitespace();
        auto value = parse_value(0);
        skip_whitespace();
        if (pos_ != text_.size()) throw std::runtime_error("glTF JSON trailing content");
        return value;
    }

private:
    static constexpr std::uint32_t kMaxDepth = 64u;

    void skip_whitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }
    [[nodiscard]] char peek() const {
        if (pos_ >= text_.size()) throw std::runtime_error("glTF JSON truncated");
        return text_[pos_];
    }
    void expect(char c) {
        if (peek() != c) throw std::runtime_error(std::string{"glTF JSON expected '"} + c + "'");
        ++pos_;
    }
    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    [[nodiscard]] JsonValue parse_value(std::uint32_t depth) {
        if (depth > kMaxDepth) throw std::runtime_error("glTF JSON nesting too deep");
        skip_whitespace();
        switch (peek()) {
        case '{': return parse_object(depth);
        case '[': return parse_array(depth);
        case '"': return JsonValue{parse_string()};
        case 't': literal("true"); return JsonValue{true};
        case 'f': literal("false"); return JsonValue{false};
        case 'n': literal("null"); return JsonValue{nullptr};
        default: return JsonValue{parse_number()};
        }
    }
    void literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) throw std::runtime_error("glTF JSON bad literal");
        pos_ += word.size();
    }
    [[nodiscard]] JsonValue parse_object(std::uint32_t depth) {
        expect('{');
        JsonObject object;
        skip_whitespace();
        if (consume('}')) return JsonValue{std::move(object)};
        for (;;) {
            skip_whitespace();
            auto key = parse_string();
            skip_whitespace();
            expect(':');
            object.emplace(std::move(key), parse_value(depth + 1u));
            skip_whitespace();
            if (consume(',')) continue;
            expect('}');
            break;
        }
        return JsonValue{std::move(object)};
    }
    [[nodiscard]] JsonValue parse_array(std::uint32_t depth) {
        expect('[');
        JsonArray array;
        skip_whitespace();
        if (consume(']')) return JsonValue{std::move(array)};
        for (;;) {
            array.push_back(parse_value(depth + 1u));
            skip_whitespace();
            if (consume(',')) continue;
            expect(']');
            break;
        }
        return JsonValue{std::move(array)};
    }
    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string out;
        for (;;) {
            if (pos_ >= text_.size()) throw std::runtime_error("glTF JSON unterminated string");
            const char c = text_[pos_++];
            if (c == '"') break;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) throw std::runtime_error("glTF JSON bad escape");
            const char esc = text_[pos_++];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (pos_ + 4u > text_.size()) throw std::runtime_error("glTF JSON bad \\u escape");
                std::uint32_t code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = text_[pos_++];
                    code <<= 4u;
                    if (hex >= '0' && hex <= '9') code |= static_cast<std::uint32_t>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') code |= static_cast<std::uint32_t>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') code |= static_cast<std::uint32_t>(hex - 'A' + 10);
                    else throw std::runtime_error("glTF JSON bad \\u digit");
                }
                // Encode as UTF-8 (surrogate pairs collapse to replacement char;
                // glTF keys virtually never need them).
                if (code < 0x80u) out.push_back(static_cast<char>(code));
                else if (code < 0x800u) {
                    out.push_back(static_cast<char>(0xc0u | (code >> 6u)));
                    out.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
                } else {
                    out.push_back(static_cast<char>(0xe0u | (code >> 12u)));
                    out.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3fu)));
                    out.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
                }
                break;
            }
            default: throw std::runtime_error("glTF JSON unknown escape");
            }
        }
        return out;
    }
    [[nodiscard]] double parse_number() {
        const auto start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size() &&
               ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' ||
                text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-'))
            ++pos_;
        double value = 0.0;
        const auto result = std::from_chars(text_.data() + start, text_.data() + pos_, value);
        if (result.ec != std::errc{}) throw std::runtime_error("glTF JSON bad number");
        return value;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// .glb container
// ---------------------------------------------------------------------------
constexpr std::uint32_t glb_magic = 0x46546c67u;   // 'glTF'
constexpr std::uint32_t glb_version = 2u;
constexpr std::uint32_t chunk_json = 0x4e4f534au;  // 'JSON'
constexpr std::uint32_t chunk_bin = 0x004e4942u;   // 'BIN'

struct GlbContainer {
    std::string_view json;
    std::span<const std::byte> binary;
};

[[nodiscard]] GlbContainer parse_glb_container(std::span<const std::byte> bytes) {
    if (bytes.size() < 20u) throw std::runtime_error("glb container too small");
    const auto read_u32 = [&](std::size_t offset) {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, 4);
        return value;
    };
    if (read_u32(0) != glb_magic) throw std::runtime_error("not a glTF binary (.glb) file");
    if (read_u32(4) != glb_version) throw std::runtime_error("unsupported glb version (need 2.0)");
    const auto total_length = read_u32(8);
    if (total_length > bytes.size()) throw std::runtime_error("glb total length exceeds payload");

    std::size_t offset = 12u;
    GlbContainer container;
    bool have_json = false;
    while (offset + 8u <= total_length) {
        const auto chunk_length = read_u32(offset);
        const auto chunk_type = read_u32(offset + 4u);
        offset += 8u;
        if (chunk_length > total_length - offset) throw std::runtime_error("glb chunk exceeds container");
        const auto chunk = bytes.subspan(offset, chunk_length);
        if (chunk_type == chunk_json) {
            container.json = {reinterpret_cast<const char*>(chunk.data()), chunk.size()};
            have_json = true;
        } else if (chunk_type == chunk_bin) {
            container.binary = chunk;
        }
        offset += chunk_length;
    }
    if (!have_json) throw std::runtime_error("glb missing JSON chunk");
    return container;
}

// ---------------------------------------------------------------------------
// glTF accessor decoding
// ---------------------------------------------------------------------------
struct GlTFContext {
    const JsonValue& root;
    std::span<const std::byte> binary;
};

struct AccessorRegion {
    std::span<const std::byte> data;
    std::size_t stride = 0;
};

[[nodiscard]] AccessorRegion accessor_data(const GlTFContext& ctx, const JsonValue& accessor,
                                           std::size_t component_size,
                                           std::size_t component_count) {
    if (!accessor.contains("bufferView")) throw std::runtime_error("glTF accessor without bufferView");
    if (accessor.contains("sparse")) throw std::runtime_error("glTF sparse accessors unsupported");
    const auto view_index = static_cast<std::size_t>(accessor.at("bufferView").number());
    const auto& views = ctx.root.at("bufferViews").array();
    if (view_index >= views.size()) throw std::runtime_error("glTF bufferView out of range");
    const auto& view = views[view_index];
    if (!view.is_object()) throw std::runtime_error("glTF malformed bufferView");
    const auto byte_offset = view.contains("byteOffset") ? static_cast<std::size_t>(view.at("byteOffset").number()) : 0u;
    const auto byte_length = static_cast<std::size_t>(view.at("byteLength").number());
    if (byte_offset + byte_length > ctx.binary.size()) throw std::runtime_error("glTF bufferView out of buffer");
    const auto element_stride = component_size * component_count;
    const auto stride = view.contains("byteStride")
                            ? static_cast<std::size_t>(view.at("byteStride").number())
                            : element_stride;
    if (stride < element_stride) throw std::runtime_error("glTF bufferView stride too small");
    const auto count = static_cast<std::size_t>(accessor.at("count").number());
    if (count == 0u) throw std::runtime_error("glTF accessor with zero count");
    const auto accessor_offset = accessor.contains("byteOffset")
                                     ? static_cast<std::size_t>(accessor.at("byteOffset").number())
                                     : 0u;
    const auto needed = stride * (count - 1u) + element_stride + accessor_offset;
    if (needed > byte_length) throw std::runtime_error("glTF accessor exceeds bufferView");
    return {ctx.binary.subspan(byte_offset + accessor_offset,
                               stride * (count - 1u) + element_stride),
            stride};
}

template <class T>
[[nodiscard]] T read_component(std::span<const std::byte> data, std::size_t offset,
                               bool normalized) {
    T raw = 0;
    std::memcpy(&raw, data.data() + offset, sizeof(T));
    if constexpr (std::is_same_v<T, float>) return raw;
    if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t> ||
                  std::is_same_v<T, std::uint32_t>) {
        if (normalized) {
            const double max_value = static_cast<double>((std::numeric_limits<T>::max)());
            return static_cast<T>(static_cast<double>(raw) / max_value);
        }
        return raw;
    }
    if constexpr (std::is_same_v<T, std::int8_t> || std::is_same_v<T, std::int16_t>) {
        if (normalized) {
            const double max_value = static_cast<double>((std::numeric_limits<T>::max)());
            return static_cast<T>(static_cast<double>(raw) / max_value);
        }
        return raw;
    }
}

struct ComponentReader {
    std::span<const std::byte> data;
    std::size_t component_size = 0;
    std::size_t stride = 0;
    int component_type = 0;
    bool normalized = false;

    [[nodiscard]] float scalar(std::size_t element, std::size_t component) const {
        const auto offset = element * stride + component * component_size;
        switch (component_type) {
        case 5120: { // byte
            const auto v = read_component<std::int8_t>(data, offset, normalized);
            return static_cast<float>(v);
        }
        case 5121: { // unsigned byte
            const auto v = read_component<std::uint8_t>(data, offset, normalized);
            return static_cast<float>(v);
        }
        case 5122: { // short
            const auto v = read_component<std::int16_t>(data, offset, normalized);
            return static_cast<float>(v);
        }
        case 5123: { // unsigned short
            const auto v = read_component<std::uint16_t>(data, offset, normalized);
            return static_cast<float>(v);
        }
        case 5125: { // unsigned int
            const auto v = read_component<std::uint32_t>(data, offset, normalized);
            return static_cast<float>(v);
        }
        case 5126: return read_component<float>(data, offset, normalized);
        default: throw std::runtime_error("glTF unsupported component type");
        }
    }
    // Index paths must not round-trip through float: uint32 vertex indices
    // exceed float32's 24-bit mantissa.
    [[nodiscard]] std::uint32_t uint_scalar(std::size_t element) const {
        const auto offset = element * stride;
        switch (component_type) {
        case 5120: return read_signed<std::int8_t>(offset);
        case 5121: return read_unsigned<std::uint8_t>(offset);
        case 5122: return static_cast<std::uint32_t>(read_signed<std::int16_t>(offset));
        case 5123: return read_unsigned<std::uint16_t>(offset);
        case 5125: return read_unsigned<std::uint32_t>(offset);
        default: throw std::runtime_error("glTF unsupported index component type");
        }
    }
    template <class T>
    [[nodiscard]] std::uint32_t read_unsigned(std::size_t offset) const {
        T raw = 0;
        std::memcpy(&raw, data.data() + offset, sizeof(T));
        return static_cast<std::uint32_t>(raw);
    }
    template <class T>
    [[nodiscard]] std::uint32_t read_signed(std::size_t offset) const {
        T raw = 0;
        std::memcpy(&raw, data.data() + offset, sizeof(T));
        if (raw < 0) throw std::runtime_error("glTF negative mesh index");
        return static_cast<std::uint32_t>(raw);
    }
};

[[nodiscard]] ComponentReader make_accessor_reader(const GlTFContext& ctx, std::size_t accessor_index,
                                                   std::size_t components) {
    const auto& accessors = ctx.root.at("accessors").array();
    if (accessor_index >= accessors.size()) throw std::runtime_error("glTF accessor index out of range");
    const auto& accessor = accessors[accessor_index];
    if (!accessor.is_object()) throw std::runtime_error("glTF malformed accessor");
    static const std::map<std::string, std::size_t> type_sizes{
        {"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}};
    const auto type_text = accessor.at("type").string();
    const auto type_it = type_sizes.find(type_text);
    if (type_it == type_sizes.end() || type_it->second != components)
        throw std::runtime_error("glTF accessor type mismatch: " + type_text);
    const auto component_type = static_cast<int>(accessor.at("componentType").number());
    std::size_t component_size = 0;
    switch (component_type) {
    case 5120: case 5121: component_size = 1u; break;
    case 5122: case 5123: component_size = 2u; break;
    case 5125: case 5126: component_size = 4u; break;
    default: throw std::runtime_error("glTF unsupported component type");
    }
    const auto region = accessor_data(ctx, accessor, component_size, components);
    return ComponentReader{region.data, component_size, region.stride, component_type,
                           accessor.contains("normalized") && accessor.at("normalized").number() != 0.0};
}

} // namespace

std::vector<MeshGeometry> import_glb_meshes(std::span<const std::byte> glb_bytes) {
    const auto container = parse_glb_container(glb_bytes);
    JsonParser parser{container.json};
    const auto root = parser.parse_document();
    if (!root.is_object()) throw std::runtime_error("glTF JSON root must be an object");
    if (!root.contains("meshes") || root.at("meshes").array().empty())
        throw std::runtime_error("glTF contains no meshes");
    const GlTFContext ctx{root, container.binary};

    std::vector<MeshGeometry> meshes;
    for (const auto& mesh : root.at("meshes").array()) {
        if (!mesh.is_object() || !mesh.contains("primitives"))
            throw std::runtime_error("glTF mesh without primitives");
        MeshGeometry geometry;
        for (const auto& primitive : mesh.at("primitives").array()) {
            if (!primitive.is_object()) throw std::runtime_error("glTF malformed primitive");
            if (primitive.contains("mode") && static_cast<int>(primitive.at("mode").number()) != 4)
                throw std::runtime_error("glTF only triangle-list primitives are supported");
            if (!primitive.contains("attributes"))
                throw std::runtime_error("glTF primitive without attributes");
            const auto& attributes = primitive.at("attributes");
            if (!attributes.contains("POSITION"))
                throw std::runtime_error("glTF primitive without POSITION");

            const auto position_reader = make_accessor_reader(ctx, static_cast<std::size_t>(attributes.at("POSITION").number()), 3u);
            const auto base_vertex = geometry.vertex_count();
            const auto primitive_vertices = position_reader.data.size() / position_reader.stride;
            for (std::size_t i = 0; i < primitive_vertices; ++i) {
                for (std::size_t c = 0; c < 3u; ++c)
                    geometry.positions.push_back(position_reader.scalar(i, c));
            }
            if (attributes.contains("NORMAL")) {
                const auto normal_reader = make_accessor_reader(ctx, static_cast<std::size_t>(attributes.at("NORMAL").number()), 3u);
                const auto normal_count = normal_reader.data.size() / normal_reader.stride;
                if (normal_count != primitive_vertices)
                    throw std::runtime_error("glTF NORMAL count does not match POSITION");
                // When concatenating primitives, pad earlier vertices with up.
                if (geometry.normals.size() < static_cast<std::size_t>(base_vertex) * 3u)
                    geometry.normals.resize(static_cast<std::size_t>(base_vertex) * 3u, 0.0f);
                for (std::size_t i = 0; i < normal_count; ++i) {
                    float n[3];
                    for (std::size_t c = 0; c < 3u; ++c) n[c] = normal_reader.scalar(i, c);
                    const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    for (std::size_t c = 0; c < 3u; ++c)
                        geometry.normals.push_back(length > 0.0f ? n[c] / length : (c == 1u ? 1.0f : 0.0f));
                }
            }
            if (attributes.contains("TEXCOORD_0")) {
                const auto uv_reader = make_accessor_reader(ctx, static_cast<std::size_t>(attributes.at("TEXCOORD_0").number()), 2u);
                const auto uv_count = uv_reader.data.size() / uv_reader.stride;
                if (uv_count != primitive_vertices)
                    throw std::runtime_error("glTF TEXCOORD_0 count does not match POSITION");
                if (geometry.uvs.size() < static_cast<std::size_t>(base_vertex) * 2u)
                    geometry.uvs.resize(static_cast<std::size_t>(base_vertex) * 2u, 0.0f);
                for (std::size_t i = 0; i < uv_count; ++i) {
                    geometry.uvs.push_back(uv_reader.scalar(i, 0));
                    geometry.uvs.push_back(uv_reader.scalar(i, 1));
                }
            }
            if (!primitive.contains("indices")) throw std::runtime_error("glTF primitive without indices");
            const auto index_reader = make_accessor_reader(ctx, static_cast<std::size_t>(primitive.at("indices").number()), 1u);
            const auto index_count = index_reader.data.size() / index_reader.stride;
            for (std::size_t i = 0; i < index_count; ++i)
                geometry.indices.push_back(base_vertex + index_reader.uint_scalar(i));
        }
        // Concatenation may leave primitives without normals/uvs against ones
        // with them: normalise column presence across the merged mesh.
        if (!geometry.normals.empty() && geometry.normals.size() != static_cast<std::size_t>(geometry.vertex_count()) * 3u)
            geometry.normals.resize(static_cast<std::size_t>(geometry.vertex_count()) * 3u, 0.0f);
        if (!geometry.uvs.empty() && geometry.uvs.size() != static_cast<std::size_t>(geometry.vertex_count()) * 2u)
            geometry.uvs.resize(static_cast<std::size_t>(geometry.vertex_count()) * 2u, 0.0f);
        geometry.recompute_bounds();
        meshes.push_back(std::move(geometry));
    }
    return meshes;
}

std::vector<std::byte> cook_glb_mesh_payload(std::span<const std::byte> glb_bytes) {
    auto meshes = import_glb_meshes(glb_bytes);
    if (meshes.empty()) throw std::runtime_error("glb contains no importable mesh");
    return encode_quantized_mesh(meshes.front());
}

} // namespace thunder::assets
