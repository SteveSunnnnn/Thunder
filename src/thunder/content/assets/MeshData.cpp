#include "thunder/content/assets/MeshData.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace thunder::assets {
namespace {

constexpr std::uint32_t mesh_header_bytes = 44u; // magic4 + counts/flags12 + width/pad4 + bounds24
constexpr std::uint32_t position_bytes = 12u;
constexpr std::uint32_t normal_bytes = 2u;
constexpr std::uint32_t uv_bytes = 4u;

template <class T>
T read_le(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset + sizeof(T) > bytes.size()) throw std::runtime_error("quantized mesh payload truncated");
    T value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    if constexpr (sizeof(T) > 1) {
        // Fixed little-endian decode independent of host byte order.
        if constexpr (std::is_same_v<T, std::uint32_t>) {
            const auto raw = value;
            value = 0;
            for (std::size_t i = 0; i < 4; ++i)
                value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(raw >> (i * 8u))) << (i * 8u);
        } else if constexpr (std::is_same_v<T, float>) {
            static_assert(sizeof(float) == 4u);
            std::uint32_t raw = 0;
            std::memcpy(&raw, bytes.data() + offset, 4);
            std::uint32_t le = 0;
            for (std::size_t i = 0; i < 4; ++i)
                le |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(raw >> (i * 8u))) << (i * 8u);
            std::memcpy(&value, &le, 4);
        }
    }
    return value;
}

template <class T>
void write_le(std::vector<std::byte>& out, T value) {
    if constexpr (std::is_same_v<T, float>) {
        std::uint32_t raw = 0;
        std::memcpy(&raw, &value, 4);
        for (std::size_t i = 0; i < 4; ++i)
            out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(raw >> (i * 8u))));
    } else {
        for (std::size_t i = 0; i < sizeof(T); ++i)
            out.push_back(static_cast<std::byte>(static_cast<std::uint64_t>(value) >> (i * 8u) & 0xffu));
    }
}

// IEEE 754 half-precision <-> single conversion (no denormal subtleties needed
// for texture coordinates; values beyond half range clamp to half inf/0).
float half_to_float(std::uint16_t h) noexcept {
    const auto sign = static_cast<std::uint32_t>((h & 0x8000u) >> 15u);
    const auto exponent = static_cast<std::uint32_t>((h & 0x7c00u) >> 10u);
    const auto mantissa = static_cast<std::uint32_t>(h & 0x03ffu);
    std::uint32_t bits = 0;
    if (exponent == 0u) {
        if (mantissa == 0u) {
            bits = sign << 31u;
        } else {
            // Subnormal half: normalise.
            std::uint32_t e = 0;
            auto m = mantissa;
            while ((m & 0x0400u) == 0u) {
                m <<= 1u;
                ++e;
            }
            bits = (sign << 31u) | ((127u - 15u - e + 1u) << 23u) | ((m & 0x03ffu) << 13u);
        }
    } else if (exponent == 31u) {
        bits = (sign << 31u) | (0xffu << 23u) | (mantissa << 13u);
    } else {
        bits = (sign << 31u) | ((exponent - 15u + 127u) << 23u) | (mantissa << 13u);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, 4);
    return out;
}

std::uint16_t float_to_half(float f) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    const auto sign = static_cast<std::uint16_t>((bits >> 16u) & 0x8000u);
    const auto exponent = static_cast<std::int32_t>((bits >> 23u) & 0xffu);
    const auto mantissa = bits & 0x007fffffu;
    std::uint16_t out = 0;
    if (exponent == 255u) {
        out = static_cast<std::uint16_t>(sign | 0x7c00u | (mantissa != 0u ? 1u : 0u));
    } else {
        const auto e = static_cast<std::int32_t>(exponent) - 127 + 15;
        if (e >= 31) {
            out = static_cast<std::uint16_t>(sign | 0x7c00u);
        } else if (e <= 0) {
            out = sign; // Map subnormal-range floats to zero (UV precision only).
        } else {
            out = static_cast<std::uint16_t>(sign | (static_cast<std::uint16_t>(e) << 10u) |
                                             static_cast<std::uint16_t>(mantissa >> 13u));
        }
    }
    return out;
}

} // namespace

void MeshGeometry::recompute_bounds() noexcept {
    if (positions.empty()) {
        bounds = MeshBounds{};
        return;
    }
    auto mn = std::array{positions[0], positions[1], positions[2]};
    auto mx = mn;
    for (std::size_t i = 3; i + 2 < positions.size(); i += 3) {
        for (int axis = 0; axis < 3; ++axis) {
            mn[static_cast<std::size_t>(axis)] = std::min(mn[static_cast<std::size_t>(axis)], positions[i + static_cast<std::size_t>(axis)]);
            mx[static_cast<std::size_t>(axis)] = std::max(mx[static_cast<std::size_t>(axis)], positions[i + static_cast<std::size_t>(axis)]);
        }
    }
    bounds.min = mn;
    bounds.max = mx;
}

std::array<std::int8_t, 2> encode_octahedral_normal(float x, float y, float z) noexcept {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0f) return {0, 0};
    float nx = x / length;
    float ny = y / length;
    float nz = z / length;
    float px = nx;
    float py = ny;
    const float abs_sum = std::fabs(px) + std::fabs(py) + std::fabs(nz);
    if (abs_sum > 0.0f) {
        px /= abs_sum;
        py /= abs_sum;
    }
    if (nz < 0.0f) {
        const float folded_x = (1.0f - std::fabs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
        const float folded_y = (1.0f - std::fabs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
        px = folded_x;
        py = folded_y;
    }
    const auto quantize = [](float v) -> std::int8_t {
        const auto scaled = static_cast<std::int32_t>(std::lround(v * 127.0f));
        return static_cast<std::int8_t>(std::clamp<std::int32_t>(scaled, -127, 127));
    };
    return {quantize(px), quantize(py)};
}

std::array<float, 3> decode_octahedral_normal(std::int8_t x, std::int8_t y) noexcept {
    float px = static_cast<float>(x) / 127.0f;
    float py = static_cast<float>(y) / 127.0f;
    float pz = 1.0f - std::fabs(px) - std::fabs(py);
    float nx = px;
    float ny = py;
    float nz = pz;
    if (nz < 0.0f) {
        const float unfolded_x = (1.0f - std::fabs(py)) * (nx >= 0.0f ? 1.0f : -1.0f);
        const float unfolded_y = (1.0f - std::fabs(px)) * (ny >= 0.0f ? 1.0f : -1.0f);
        nx = unfolded_x;
        ny = unfolded_y;
    }
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length > 0.0f) {
        nx /= length;
        ny /= length;
        nz /= length;
    }
    return {nx, ny, nz};
}

std::vector<std::byte> encode_quantized_mesh(const MeshGeometry& geometry) {
    const auto vertex_count = geometry.vertex_count();
    const auto index_count = geometry.index_count();
    if (vertex_count == 0u || index_count == 0u)
        throw std::invalid_argument("quantized mesh requires vertices and triangle indices");
    if (index_count % 3u != 0u)
        throw std::invalid_argument("quantized mesh requires triangle-list indices");
    for (const auto index : geometry.indices)
        if (index >= vertex_count) throw std::invalid_argument("mesh index exceeds vertex count");

    const bool has_normals = geometry.normals.size() == geometry.positions.size();
    const bool has_uvs = geometry.uvs.size() == static_cast<std::size_t>(vertex_count) * 2u;
    const bool index32 = vertex_count > 65535u;

    std::vector<std::byte> out;
    out.reserve(mesh_header_bytes + vertex_count * (position_bytes + normal_bytes + uv_bytes) +
                index_count * (index32 ? 4u : 2u));
    for (const char c : mesh_payload_magic) out.push_back(static_cast<std::byte>(c));
    write_le<std::uint32_t>(out, vertex_count);
    write_le<std::uint32_t>(out, index_count);
    write_le<std::uint32_t>(out, (has_normals ? mesh_flag_has_normals : 0u) |
                                    (has_uvs ? mesh_flag_has_uvs : 0u));
    write_le<std::uint8_t>(out, index32 ? 4u : 2u);
    write_le<std::uint8_t>(out, std::uint8_t{0});
    write_le<std::uint16_t>(out, std::uint16_t{0});
    for (const auto v : geometry.bounds.min) write_le<float>(out, v);
    for (const auto v : geometry.bounds.max) write_le<float>(out, v);

    for (std::uint32_t v = 0; v < vertex_count; ++v) {
        for (int axis = 0; axis < 3; ++axis)
            write_le<float>(out, geometry.positions[static_cast<std::size_t>(v) * 3u +
                                                   static_cast<std::size_t>(axis)]);
    }
    if (has_normals) {
        for (std::uint32_t v = 0; v < vertex_count; ++v) {
            const auto oct = encode_octahedral_normal(
                geometry.normals[static_cast<std::size_t>(v) * 3u],
                geometry.normals[static_cast<std::size_t>(v) * 3u + 1u],
                geometry.normals[static_cast<std::size_t>(v) * 3u + 2u]);
            out.push_back(static_cast<std::byte>(oct[0]));
            out.push_back(static_cast<std::byte>(oct[1]));
        }
    }
    if (has_uvs) {
        for (std::uint32_t v = 0; v < vertex_count; ++v) {
            const auto u = float_to_half(geometry.uvs[static_cast<std::size_t>(v) * 2u]);
            const auto v_coord = float_to_half(geometry.uvs[static_cast<std::size_t>(v) * 2u + 1u]);
            out.push_back(static_cast<std::byte>(u & 0xffu));
            out.push_back(static_cast<std::byte>((u >> 8u) & 0xffu));
            out.push_back(static_cast<std::byte>(v_coord & 0xffu));
            out.push_back(static_cast<std::byte>((v_coord >> 8u) & 0xffu));
        }
    }
    for (const auto index : geometry.indices) {
        if (index32) write_le<std::uint32_t>(out, index);
        else write_le<std::uint16_t>(out, static_cast<std::uint16_t>(index));
    }
    return out;
}

QuantizedMeshView decode_quantized_mesh(std::span<const std::byte> payload) {
    if (payload.size() < mesh_header_bytes) throw std::runtime_error("mesh payload too small");
    for (int i = 0; i < 4; ++i)
        if (std::to_integer<char>(payload[static_cast<std::size_t>(i)]) != mesh_payload_magic[i])
            throw std::runtime_error("invalid mesh payload magic");

    QuantizedMeshView view;
    view.payload = payload;
    view.vertex_count = read_le<std::uint32_t>(payload, 4);
    view.index_count = read_le<std::uint32_t>(payload, 8);
    const auto flags = read_le<std::uint32_t>(payload, 12);
    const auto index_width = std::to_integer<std::uint8_t>(payload[16]);
    view.has_normals = (flags & mesh_flag_has_normals) != 0u;
    view.has_uvs = (flags & mesh_flag_has_uvs) != 0u;
    if (index_width != 2u && index_width != 4u) throw std::runtime_error("invalid mesh index width");
    view.index_width_32 = index_width == 4u;
    if (view.vertex_count == 0u || view.index_count == 0u || view.index_count % 3u != 0u)
        throw std::runtime_error("invalid mesh counts");
    for (int a = 0; a < 3; ++a) {
        view.bounds.min[static_cast<std::size_t>(a)] = read_le<float>(payload, 20u + static_cast<std::uint32_t>(a) * 4u);
        view.bounds.max[static_cast<std::size_t>(a)] = read_le<float>(payload, 32u + static_cast<std::uint32_t>(a) * 4u);
    }

    const std::uint64_t positions_end = mesh_header_bytes +
        static_cast<std::uint64_t>(view.vertex_count) * position_bytes;
    const std::uint64_t normals_end = positions_end +
        (view.has_normals ? static_cast<std::uint64_t>(view.vertex_count) * normal_bytes : 0u);
    const std::uint64_t uvs_end = normals_end +
        (view.has_uvs ? static_cast<std::uint64_t>(view.vertex_count) * uv_bytes : 0u);
    const std::uint64_t indices_end = uvs_end +
        static_cast<std::uint64_t>(view.index_count) * index_width;
    if (indices_end > payload.size()) throw std::runtime_error("mesh payload truncated");
    return view;
}

std::array<float, 3> QuantizedMeshView::position(std::uint32_t vertex) const noexcept {
    const auto offset = mesh_header_bytes + static_cast<std::uint64_t>(vertex) * position_bytes;
    return {read_le<float>(payload, offset), read_le<float>(payload, offset + 4u),
            read_le<float>(payload, offset + 8u)};
}

std::array<float, 3> QuantizedMeshView::normal(std::uint32_t vertex) const noexcept {
    const auto offset = mesh_header_bytes + static_cast<std::uint64_t>(vertex_count) * position_bytes +
                        static_cast<std::uint64_t>(vertex) * normal_bytes;
    const auto sx = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(payload[offset]));
    const auto sy = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(payload[offset + 1u]));
    return decode_octahedral_normal(sx, sy);
}

std::array<float, 2> QuantizedMeshView::uv(std::uint32_t vertex) const noexcept {
    const auto offset = mesh_header_bytes + static_cast<std::uint64_t>(vertex_count) * position_bytes +
                        (has_normals ? static_cast<std::uint64_t>(vertex_count) * normal_bytes : 0u) +
                        static_cast<std::uint64_t>(vertex) * uv_bytes;
    const auto u = read_le<std::uint16_t>(payload, offset);
    const auto v = read_le<std::uint16_t>(payload, offset + 2u);
    return {half_to_float(u), half_to_float(v)};
}

std::uint32_t QuantizedMeshView::index(std::uint32_t i) const noexcept {
    const auto offset = mesh_header_bytes + static_cast<std::uint64_t>(vertex_count) * position_bytes +
                        (has_normals ? static_cast<std::uint64_t>(vertex_count) * normal_bytes : 0u) +
                        (has_uvs ? static_cast<std::uint64_t>(vertex_count) * uv_bytes : 0u) +
                        static_cast<std::uint64_t>(i) * (index_width_32 ? 4u : 2u);
    if (index_width_32) return read_le<std::uint32_t>(payload, offset);
    return static_cast<std::uint32_t>(read_le<std::uint16_t>(payload, offset));
}

} // namespace thunder::assets
