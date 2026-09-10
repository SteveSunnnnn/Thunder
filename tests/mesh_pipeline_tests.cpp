// Asset pipeline front-end tests: glTF 2.0 Binary import, the quantized THM1
// runtime layout, and their end-to-end path through the AssetPack cooker
// conventions. These validate the head of the near-view asset pipeline
// (docs/ASSET_ACQUISITION_REQUIREMENTS.md) without a GPU.
#include "thunder/content/assets/AssetPack.hpp"
#include "thunder/content/assets/GlTFImporter.hpp"
#include "thunder/content/assets/MeshData.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace thunder;
using namespace thunder::assets;

namespace {

void put_u32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::byte>((v >> (i * 8u)) & 0xffu));
}

std::vector<std::byte> pack_glb(std::string_view json, std::span<const std::byte> bin) {
    std::vector<std::byte> glb;
    const auto json_pad = (4u - json.size() % 4u) % 4u;
    const auto bin_pad = (4u - bin.size() % 4u) % 4u;
    const auto total = 12u + 8u + json.size() + json_pad + 8u + bin.size() + bin_pad;
    put_u32(glb, 0x46546c67u);
    put_u32(glb, 2u);
    put_u32(glb, static_cast<std::uint32_t>(total));
    put_u32(glb, static_cast<std::uint32_t>(json.size() + json_pad));
    put_u32(glb, 0x4e4f534au);
    for (const char c : json) glb.push_back(static_cast<std::byte>(c));
    for (std::uint32_t i = 0; i < json_pad; ++i) glb.push_back(std::byte{0x20});
    put_u32(glb, static_cast<std::uint32_t>(bin.size() + bin_pad));
    put_u32(glb, 0x004e4942u);
    glb.insert(glb.end(), bin.begin(), bin.end());
    for (std::uint32_t i = 0; i < bin_pad; ++i) glb.push_back(std::byte{0});
    return glb;
}

// Builds a minimal but structurally complete .glb: one mesh, one triangle-list
// primitive with POSITION/NORMAL/TEXCOORD_0 and uint16 indices.
std::vector<std::byte> build_test_glb() {
    // Buffer layout: positions(3*4*4=48) + normals(4*3*4=48) + uvs(4*2*4=32)
    // + indices(6*2=12), tightly packed.
    struct Vec3 { float x, y, z; };
    const std::array<Vec3, 4> positions{{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 0.0f}, {0.0f, 2.0f, 0.0f}}};
    const std::array<Vec3, 4> normals{{{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}}};
    const std::array<float, 8> uvs{{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f}};
    const std::array<std::uint16_t, 6> indices{{0, 1, 2, 0, 2, 3}};

    std::vector<std::byte> bin;
    auto append = [&bin](const void* data, std::size_t size) {
        const auto* b = static_cast<const std::byte*>(data);
        bin.insert(bin.end(), b, b + size);
    };
    append(positions.data(), sizeof(positions));
    append(normals.data(), sizeof(normals));
    append(uvs.data(), sizeof(uvs));
    append(indices.data(), sizeof(indices));

    const std::string json = R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3
            }]
        }],
        "buffers": [{"byteLength": 140}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 48},
            {"buffer": 0, "byteOffset": 48, "byteLength": 48},
            {"buffer": 0, "byteOffset": 96, "byteLength": 32},
            {"buffer": 0, "byteOffset": 128, "byteLength": 12}
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}
        ]
    })";

    return pack_glb(json, bin);
}

void test_glb_import() {
    const auto meshes = import_glb_meshes(build_test_glb());
    assert(meshes.size() == 1u);
    const auto& mesh = meshes.front();
    assert(mesh.vertex_count() == 4u);
    assert(mesh.index_count() == 6u);
    assert(mesh.positions[0] == 0.0f && mesh.positions[1] == 0.0f);
    assert(mesh.positions[6] == 1.0f && mesh.positions[7] == 2.0f);
    assert(mesh.normals[1] == 1.0f);
    assert(mesh.uvs[2] == 1.0f);
    assert(mesh.indices[3] == 0u && mesh.indices[5] == 3u);
    assert(mesh.bounds.min[0] == 0.0f && mesh.bounds.max[0] == 1.0f);
    assert(mesh.bounds.max[1] == 2.0f);
}

void test_glb_rejects_malformed() {
    bool rejected = false;
    try {
        std::vector<std::byte> junk(64, std::byte{0});
        (void)import_glb_meshes(junk);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
}

void test_octahedral_normal_roundtrip() {
    // Encode/decode must stay within the 1/127 quantisation cone for a spread
    // of directions, including the octahedral fold seam (negative Z).
    const std::array<std::array<float, 3>, 10> directions{
        {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
         {0.577f, 0.577f, 0.577f}, {-0.577f, 0.577f, -0.577f}, {0.707f, 0.0f, -0.707f},
         {0.0f, -1.0f, 0.0f}, {-0.894f, 0.447f, 0.0f}, {0.333f, -0.667f, 0.667f}}};
    for (const auto& d : directions) {
        const auto encoded = encode_octahedral_normal(d[0], d[1], d[2]);
        const auto decoded = decode_octahedral_normal(encoded[0], encoded[1]);
        const float dot = decoded[0] * d[0] + decoded[1] * d[1] + decoded[2] * d[2];
        assert(dot > 0.995f);
        const float length = std::sqrt(decoded[0] * decoded[0] + decoded[1] * decoded[1] +
                                       decoded[2] * decoded[2]);
        assert(std::abs(length - 1.0f) < 1.0e-3f);
    }
}

void test_quantized_mesh_roundtrip() {
    MeshGeometry geometry;
    // Enough vertices to stay on uint16, plus a half-precision-sensitive UV.
    for (std::uint32_t v = 0; v < 4u; ++v) {
        geometry.positions.push_back(static_cast<float>(v) * 1.5f);
        geometry.positions.push_back(0.25f * static_cast<float>(v));
        geometry.positions.push_back(-2.0f + static_cast<float>(v));
        geometry.normals.insert(geometry.normals.end(), {0.0f, 1.0f, 0.0f});
        geometry.uvs.push_back(0.25f * static_cast<float>(v));
        geometry.uvs.push_back(0.75f - 0.25f * static_cast<float>(v));
    }
    geometry.indices = {0, 1, 2, 0, 2, 3};
    geometry.recompute_bounds();

    const auto payload = encode_quantized_mesh(geometry);
    const auto view = decode_quantized_mesh(payload);
    assert(view.vertex_count == 4u);
    assert(view.index_count == 6u);
    assert(view.has_normals && view.has_uvs);
    assert(!view.index_width_32);

    for (std::uint32_t v = 0; v < 4u; ++v) {
        const auto position = view.position(v);
        assert(position[0] == geometry.positions[v * 3u]);       // f32 exact
        assert(position[1] == geometry.positions[v * 3u + 1u]);
        assert(std::abs(position[2] - geometry.positions[v * 3u + 2u]) < 1.0e-6f);
        const auto uv = view.uv(v);
        assert(std::abs(uv[0] - geometry.uvs[v * 2u]) < 1.0e-3f);
        const auto normal = view.normal(v);
        const float dot = normal[0] * 0.0f + normal[1] * 1.0f + normal[2] * 0.0f;
        assert(dot > 0.995f);
    }
    for (std::uint32_t i = 0; i < 6u; ++i) assert(view.index(i) == geometry.indices[i]);

    // Bounds survive the trip (culling input).
    assert(view.bounds.min[2] == -2.0f);
    assert(view.bounds.max[0] == 4.5f);
}

void test_quantized_mesh_rejects_garbage() {
    bool rejected = false;
    try {
        std::vector<std::byte> junk(48, std::byte{0});
        (void)decode_quantized_mesh(junk);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);

    // Index out of vertex range must be refused at encode time.
    MeshGeometry geometry;
    geometry.positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    geometry.indices = {0, 1, 7};
    rejected = false;
    try {
        (void)encode_quantized_mesh(geometry);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_assetpack_mesh_end_to_end() {
    // glb -> cook -> AssetPack -> read -> decode: the exact cooker path.
    const auto glb = build_test_glb();
    const auto payload = cook_glb_mesh_payload(glb);
    const auto view = decode_quantized_mesh(payload);
    assert(view.vertex_count == 4u && view.index_count == 6u);

    const std::filesystem::path pack_path =
        std::filesystem::temp_directory_path() / "thunder_mesh_pipeline_test.thunderasset";
    AssetPackWriter writer;
    writer.add("arch_we_res_poor_01", AssetKind::Mesh, 0, payload);
    writer.add("arch_we_res_poor_01", AssetKind::Mesh, 1, payload);
    writer.write(pack_path);

    AssetPackReader reader;
    reader.open(pack_path);
    const auto lod0 = reader.find("arch_we_res_poor_01", 0);
    const auto lod1 = reader.find("arch_we_res_poor_01", 1);
    assert(lod0.has_value() && lod1.has_value());
    const auto bytes = reader.read(*lod0);
    const auto restored = decode_quantized_mesh(bytes);
    assert(restored.vertex_count == 4u);
    const auto position = restored.position(2);
    assert(position[0] == 1.0f && position[1] == 2.0f);

    // Far LOD is a distinct entry under the same key.
    assert(!reader.find("arch_we_res_poor_01", 2).has_value());
    std::error_code ec;
    std::filesystem::remove(pack_path, ec);
}

void test_glb_normalized_unorm8_snorm8() {
    // 3 vertices: float3 positions, int8_t (SNORM8) normals, uint8_t (UNORM8) UVs
    struct Vec3 { float x, y, z; };
    const std::array<Vec3, 3> positions{{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}};
    struct Norm8 { std::int8_t x, y, z; };
    // Test -128 clamping to -1.0f per glTF 2.0 normalization spec max(raw / 127.0, -1.0)
    const std::array<Norm8, 3> normals{{{0, 127, 0}, {-128, 0, 0}, {0, 0, 127}}};
    struct Uv8 { std::uint8_t u, v; };
    const std::array<Uv8, 3> uvs{{{0, 255}, {255, 128}, {128, 0}}};
    const std::array<std::uint16_t, 3> indices{{0, 1, 2}};

    std::vector<std::byte> bin;
    auto append = [&bin](const void* data, std::size_t size) {
        const auto* b = static_cast<const std::byte*>(data);
        bin.insert(bin.end(), b, b + size);
    };
    append(positions.data(), sizeof(positions)); // 36
    append(normals.data(), sizeof(normals));     // 9
    append(uvs.data(), sizeof(uvs));             // 6
    while (bin.size() % 2u != 0) bin.push_back(std::byte{0}); // align for uint16
    const auto idx_offset = bin.size();
    append(indices.data(), sizeof(indices));     // 6

    const std::string json = std::string(R"({
        "asset": {"version": "2.0"},
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3
            }]
        }],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 9},
            {"buffer": 0, "byteOffset": 45, "byteLength": 6},
            {"buffer": 0, "byteOffset": )") + std::to_string(idx_offset) + R"(, "byteLength": 6}
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5120, "count": 3, "type": "VEC3", "normalized": true},
            {"bufferView": 2, "componentType": 5121, "count": 3, "type": "VEC2", "normalized": true},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
        ]
    })";

    const auto glb = pack_glb(json, bin);
    const auto meshes = import_glb_meshes(glb);
    assert(meshes.size() == 1u);
    const auto& mesh = meshes.front();
    assert(mesh.vertex_count() == 3u);
    assert(mesh.index_count() == 3u);

    // Check SNORM8 normals for all 3 vertices (including -128 clamp and vertex 2)
    assert(std::abs(mesh.normals[0] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[1] - 1.0f) < 1e-4f);
    assert(std::abs(mesh.normals[2] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[3] - (-1.0f)) < 1e-4f);
    assert(std::abs(mesh.normals[4] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[5] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[6] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[7] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[8] - 1.0f) < 1e-4f);

    // Check UNORM8 UVs for all 3 vertices
    assert(std::abs(mesh.uvs[0] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.uvs[1] - 1.0f) < 1e-4f);
    assert(std::abs(mesh.uvs[2] - 1.0f) < 1e-4f);
    assert(std::abs(mesh.uvs[3] - (128.0f / 255.0f)) < 1e-4f);
    assert(std::abs(mesh.uvs[4] - (128.0f / 255.0f)) < 1e-4f);
    assert(std::abs(mesh.uvs[5] - 0.0f) < 1e-4f);
}

void test_glb_normalized_unorm16_snorm16() {
    // 3 vertices: float3 positions, int16_t (SNORM16) normals, uint16_t (UNORM16) UVs
    struct Vec3 { float x, y, z; };
    const std::array<Vec3, 3> positions{{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}};
    struct Norm16 { std::int16_t x, y, z; };
    // Test -32768 clamping to -1.0f per glTF 2.0 normalization spec max(raw / 32767.0, -1.0)
    const std::array<Norm16, 3> normals{{{0, 32767, 0}, {-32768, 0, 0}, {0, 0, 32767}}};
    struct Uv16 { std::uint16_t u, v; };
    const std::array<Uv16, 3> uvs{{{0, 65535}, {65535, 32768}, {32768, 0}}};
    const std::array<std::uint16_t, 3> indices{{0, 1, 2}};

    std::vector<std::byte> bin;
    auto append = [&bin](const void* data, std::size_t size) {
        const auto* b = static_cast<const std::byte*>(data);
        bin.insert(bin.end(), b, b + size);
    };
    append(positions.data(), sizeof(positions)); // 36
    append(normals.data(), sizeof(normals));     // 18
    append(uvs.data(), sizeof(uvs));             // 12
    append(indices.data(), sizeof(indices));     // 6

    const std::string json = R"({
        "asset": {"version": "2.0"},
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3
            }]
        }],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 18},
            {"buffer": 0, "byteOffset": 54, "byteLength": 12},
            {"buffer": 0, "byteOffset": 66, "byteLength": 6}
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5122, "count": 3, "type": "VEC3", "normalized": true},
            {"bufferView": 2, "componentType": 5123, "count": 3, "type": "VEC2", "normalized": true},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
        ]
    })";

    const auto glb = pack_glb(json, bin);
    const auto meshes = import_glb_meshes(glb);
    assert(meshes.size() == 1u);
    const auto& mesh = meshes.front();
    assert(mesh.vertex_count() == 3u);

    // Check SNORM16 normals across all 3 vertices
    assert(std::abs(mesh.normals[0] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[1] - 1.0f) < 1e-4f);
    assert(std::abs(mesh.normals[2] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[3] - (-1.0f)) < 1e-4f);
    assert(std::abs(mesh.normals[4] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[5] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[6] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[7] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.normals[8] - 1.0f) < 1e-4f);

    // Check UNORM16 UVs across all 3 vertices
    assert(std::abs(mesh.uvs[0] - 0.0f) < 1e-4f);
    assert(std::abs(mesh.uvs[1] - 1.0f) < 1e-4f);
    assert(std::abs(mesh.uvs[2] - 1.0f) < 1e-4f);
    assert(std::abs(mesh.uvs[3] - (32768.0f / 65535.0f)) < 1e-4f);
    assert(std::abs(mesh.uvs[4] - (32768.0f / 65535.0f)) < 1e-4f);
    assert(std::abs(mesh.uvs[5] - 0.0f) < 1e-4f);
}

void test_glb_interleaved_and_accessor_offset() {
    // 4 interleaved vertices sharing a single bufferView with byteStride = 32
    // Accessor offsets:
    // POSITION: offset 0 (VEC3 float)
    // NORMAL: offset 12 (VEC3 float)
    // TEXCOORD_0: offset 24 (VEC2 float)
    struct Vertex {
        float pos[3];
        float norm[3];
        float uv[2];
    };
    static_assert(sizeof(Vertex) == 32u);

    const std::array<Vertex, 4> vertices{{
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}
    }};
    const std::array<std::uint16_t, 6> indices{{0, 1, 2, 0, 2, 3}};

    std::vector<std::byte> bin;
    auto append = [&bin](const void* data, std::size_t size) {
        const auto* b = static_cast<const std::byte*>(data);
        bin.insert(bin.end(), b, b + size);
    };
    append(vertices.data(), sizeof(vertices)); // 4 * 32 = 128 bytes
    append(indices.data(), sizeof(indices));   // 12 bytes

    const std::string json = R"({
        "asset": {"version": "2.0"},
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3
            }]
        }],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 128, "byteStride": 32},
            {"buffer": 0, "byteOffset": 128, "byteLength": 12}
        ],
        "accessors": [
            {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"}
        ]
    })";

    const auto glb = pack_glb(json, bin);
    const auto meshes = import_glb_meshes(glb);
    assert(meshes.size() == 1u);
    const auto& mesh = meshes.front();
    assert(mesh.vertex_count() == 4u);
    assert(mesh.index_count() == 6u);

    // Verify all 4 vertices, especially the 4th vertex (index 3) to prove count wasn't truncated
    assert(mesh.positions[9] == 0.0f && mesh.positions[10] == 2.0f && mesh.positions[11] == 0.0f);
    assert(mesh.normals[9] == 0.0f && mesh.normals[10] == 1.0f && mesh.normals[11] == 0.0f);
    assert(mesh.uvs[6] == 0.0f && mesh.uvs[7] == 1.0f);

    // Verify bounds
    assert(mesh.bounds.min[0] == 0.0f && mesh.bounds.max[0] == 1.0f);
    assert(mesh.bounds.min[1] == 0.0f && mesh.bounds.max[1] == 2.0f);

    // End-to-end cook and decode verification for interleaved buffer
    const auto payload = cook_glb_mesh_payload(glb);
    const auto cooked = decode_quantized_mesh(payload);
    assert(cooked.vertex_count == 4u);
    assert(cooked.index_count == 6u);
    assert(cooked.has_normals && cooked.has_uvs);
    const auto v3_pos = cooked.position(3);
    assert(v3_pos[0] == 0.0f && v3_pos[1] == 2.0f && v3_pos[2] == 0.0f);
}

void test_glb_multi_primitive() {
    // 1 mesh with 2 primitives:
    // Primitive 0: triangle at X in [0, 1]
    // Primitive 1: triangle at X in [2, 3]
    struct Vec3 { float x, y, z; };
    const std::array<Vec3, 3> p0_positions{{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}};
    const std::array<std::uint16_t, 3> p0_indices{{0, 1, 2}};

    const std::array<Vec3, 3> p1_positions{{{2.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}}};
    const std::array<std::uint16_t, 3> p1_indices{{0, 1, 2}};

    std::vector<std::byte> bin;
    auto append = [&bin](const void* data, std::size_t size) {
        const auto* b = static_cast<const std::byte*>(data);
        bin.insert(bin.end(), b, b + size);
    };
    append(p0_positions.data(), sizeof(p0_positions)); // 36
    append(p0_indices.data(), sizeof(p0_indices));     // 6
    append(p1_positions.data(), sizeof(p1_positions)); // 36
    append(p1_indices.data(), sizeof(p1_indices));     // 6

    const std::string json = R"({
        "asset": {"version": "2.0"},
        "meshes": [{
            "primitives": [
                {
                    "attributes": {"POSITION": 0},
                    "indices": 1
                },
                {
                    "attributes": {"POSITION": 2},
                    "indices": 3
                }
            ]
        }],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 6},
            {"buffer": 0, "byteOffset": 42, "byteLength": 36},
            {"buffer": 0, "byteOffset": 78, "byteLength": 6}
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
        ]
    })";

    const auto glb = pack_glb(json, bin);
    const auto meshes = import_glb_meshes(glb);
    assert(meshes.size() == 1u);
    const auto& mesh = meshes.front();
    // 3 + 3 = 6 vertices
    assert(mesh.vertex_count() == 6u);
    // 3 + 3 = 6 indices
    assert(mesh.index_count() == 6u);

    // Check rebased indices: primitive 0 has {0, 1, 2}, primitive 1 has {3, 4, 5}
    assert(mesh.indices[0] == 0u && mesh.indices[1] == 1u && mesh.indices[2] == 2u);
    assert(mesh.indices[3] == 3u && mesh.indices[4] == 4u && mesh.indices[5] == 5u);

    // Check positions
    assert(mesh.positions[0] == 0.0f && mesh.positions[3] == 1.0f);
    assert(mesh.positions[9] == 2.0f && mesh.positions[12] == 3.0f);

    // Check bounds across both primitives
    assert(mesh.bounds.min[0] == 0.0f);
    assert(mesh.bounds.max[0] == 3.0f);
}

} // namespace

int main() {
    test_glb_import();
    test_glb_rejects_malformed();
    test_glb_normalized_unorm8_snorm8();
    test_glb_normalized_unorm16_snorm16();
    test_glb_interleaved_and_accessor_offset();
    test_glb_multi_primitive();
    test_octahedral_normal_roundtrip();
    test_quantized_mesh_roundtrip();
    test_quantized_mesh_rejects_garbage();
    test_assetpack_mesh_end_to_end();
    std::cout << "Mesh pipeline tests passed\n";
}
