#include "thunder/content/assets/AssetPack.hpp"
#include "thunder/content/assets/Material.hpp"
#include "thunder/presentation/render/BindlessMaterialSystem.hpp"
#include "thunder/presentation/render/GpuDrivenPipeline.hpp"
#include <array>
#include "thunder/presentation/render/PhysicalLighting.hpp"


#include <cassert>
#include <iostream>
#include <vector>

namespace thunder {

static void test_frustum_planes_culling() {
    // Construct an orthographic or perspective-like VP matrix looking down -Z
    // Identity with depth 0..1
    std::array<float, 16> vp = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    const auto frustum = FrustumPlanes::from_view_projection(vp);

    // Point inside clip box [-1, 1]
    assert(frustum.intersects_sphere(0.0f, 0.0f, 0.5f, 0.1f) == true);

    // Point far outside on X (> 1)
    assert(frustum.intersects_sphere(10.0f, 0.0f, 0.5f, 0.1f) == false);

    // Point behind near plane (< 0)
    assert(frustum.intersects_sphere(0.0f, 0.0f, -5.0f, 0.1f) == false);

    // Box test
    assert(frustum.intersects_box(-0.5f, -0.5f, 0.1f, 0.5f, 0.5f, 0.9f) == true);
    assert(frustum.intersects_box(5.0f, 5.0f, 5.0f, 10.0f, 10.0f, 10.0f) == false);

    std::cout << "[PASS] Frustum planes extraction and bounding volume intersection\n";
}

static void test_gpu_culling_pipeline_and_indirect_draws() {
    std::vector<LivingInstanceGpu> instances;
    instances.reserve(1000);

    // Create instances at various distances from origin (0, 0)
    // Camera is at (0, 0, 1000)
    for (std::uint16_t i = 0; i < 1000; ++i) {
        LivingInstanceGpu inst;
        inst.local_x_m = static_cast<std::uint16_t>(i * 50); // 0 to 49,950 m
        inst.local_y_m = 0;
        inst.local_z_half_m = 0;
        inst.scale_milli = 1000;
        instances.push_back(inst);
    }

    StrategicCameraState camera;
    camera.center = {0.0, 0.0};
    camera.altitude_m = 1000.0;

    // Standard identity-like frustum
    std::array<float, 16> vp = {
        1.0f / 50000.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f / 50000.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f / 50000.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    const auto frustum = FrustumPlanes::from_view_projection(vp);

    GpuCullingConfig config;
    config.near_lod_dist = 10'000.0f;
    config.med_lod_dist = 30'000.0f;
    config.far_lod_dist = 45'000.0f; // Instances beyond 45km will be culled

    std::array<LodMeshBinding, 3> lod_meshes = {{
        {.index_count = 300, .first_index = 0, .vertex_offset = 0},
        {.index_count = 150, .first_index = 300, .vertex_offset = 0},
        {.index_count = 50, .first_index = 450, .vertex_offset = 0}
    }};

    const auto output = GpuCullingPipeline::cull_and_generate_draws(
        instances, 0.0, 0.0, camera, frustum, config, lod_meshes);

    assert(output.total_visible_instances > 0);
    assert(output.total_culled_instances > 0);
    assert(output.total_visible_instances + output.total_culled_instances == instances.size());

    assert(!output.visible_instances_lod0.empty());
    assert(!output.visible_instances_lod1.empty());
    assert(!output.visible_instances_lod2.empty());

    assert(output.indirect_commands.size() == 3);
    assert(output.indirect_commands[0].instance_count == output.visible_instances_lod0.size());
    assert(output.indirect_commands[1].instance_count == output.visible_instances_lod1.size());
    assert(output.indirect_commands[2].instance_count == output.visible_instances_lod2.size());

    // Check non-overlapping instance offsets
    assert(output.indirect_commands[0].first_instance == 0);
    assert(output.indirect_commands[1].first_instance == output.visible_instances_lod0.size());
    assert(output.indirect_commands[2].first_instance == output.visible_instances_lod0.size() +
                                                         output.visible_instances_lod1.size());

    std::cout << "[PASS] GPU-Driven culling pipeline and indirect draw generation\n";
}

static void test_bindless_material_system() {
    BindlessMaterialSystem materials;

    const std::uint64_t tex_albedo = 0x1111222233334444ull;
    const std::uint64_t tex_normal = 0x5555666677778888ull;
    const std::uint64_t tex_orm = 0x9999aaaabbbbccccull;

    const auto slot_albedo = materials.register_texture(tex_albedo);
    const auto slot_normal = materials.register_texture(tex_normal);
    const auto slot_orm = materials.register_texture(tex_orm);

    assert(slot_albedo != 0);
    assert(slot_normal != 0);
    assert(slot_orm != 0);
    assert(slot_albedo != slot_normal);

    // Deduplication check
    assert(materials.register_texture(tex_albedo) == slot_albedo);

    PbrMaterial mat1;
    mat1.base_color = {0.8f, 0.2f, 0.2f, 1.0f};
    mat1.metallic = 0.5f;
    mat1.roughness = 0.3f;
    mat1.base_color_texture = tex_albedo;
    mat1.normal_texture = tex_normal;
    mat1.orm_texture = tex_orm;

    const auto mat1_id = materials.register_material(mat1);
    assert(materials.material_count() == 1);

    const auto& gpu_mat = materials.material_data(mat1_id);
    assert(gpu_mat.base_color_texture_index == slot_albedo);
    assert(gpu_mat.normal_texture_index == slot_normal);
    assert(gpu_mat.orm_texture_index == slot_orm);
    assert(gpu_mat.metallic == 0.5f);
    assert(gpu_mat.roughness == 0.3f);

    assert(materials.memory_bytes() > 0);
    assert(materials.checksum() != 0);

    std::cout << "[PASS] Bindless material system & descriptor indexing\n";
}

static void test_physical_lighting_and_postprocess() {
    // 1. Cascaded Shadow Maps
    const auto splits = CascadedShadowMaps::calculate_splits(0.1f, 1000.0f, 0.75f, {0.0f, 1.0f, 0.0f}, Mat4::identity());
    assert(splits.size() == 4);
    assert(splits[0].near_dist == 0.1f);
    assert(splits[0].far_dist < splits[1].far_dist);
    assert(splits[2].far_dist < splits[3].far_dist);
    assert(splits[3].far_dist <= 1000.01f);

    // 2. Atmospheric Scattering
    const auto sky_zenith = AtmosphericScattering::compute_sky_color({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 100.0f);
    assert(sky_zenith.z > 0.0f); // Blue wavelength scattering

    const auto phase_r = AtmosphericScattering::phase_rayleigh(1.0f);
    const auto phase_m = AtmosphericScattering::phase_mie(1.0f, 0.76f);
    assert(phase_r > 0.0f && phase_m > 0.0f);

    // 3. Tone Mapping Post Process (ACES Filmic)
    const Vec3 hdr_overexposed{10.0f, 5.0f, 2.0f};
    const auto ldr_mapped = ToneMappingPostProcess::aces_filmic(hdr_overexposed, 1.0f);
    assert(ldr_mapped.x <= 1.0f && ldr_mapped.x >= 0.0f);
    assert(ldr_mapped.y <= 1.0f && ldr_mapped.y >= 0.0f);
    assert(ldr_mapped.z <= 1.0f && ldr_mapped.z >= 0.0f);

    std::cout << "[PASS] Cascaded shadow maps, atmospheric scattering & ACES tone mapping\n";
}

static void test_asset_hot_reloader() {
    AssetHotReloader reloader;
    reloader.track_file("test_asset", "CMakeLists.txt");
    assert(reloader.tracked_count() == 1);

    // Initial poll without changes
    const auto changed = reloader.poll_changed_assets();
    assert(changed.empty());

    std::cout << "[PASS] Asset hot-reloader tracking and poll loop\n";
}

} // namespace thunder

int main() {
    std::cout << "Running GPU-Driven & Bindless Optimization tests...\n";
    thunder::test_frustum_planes_culling();
    thunder::test_gpu_culling_pipeline_and_indirect_draws();
    thunder::test_bindless_material_system();
    thunder::test_physical_lighting_and_postprocess();
    thunder::test_asset_hot_reloader();
    std::cout << "All GPU-Driven & Bindless Optimization tests passed successfully!\n";
    return 0;
}
