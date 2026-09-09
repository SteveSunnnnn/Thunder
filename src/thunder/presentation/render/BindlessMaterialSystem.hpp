#pragma once

#include "thunder/content/assets/Material.hpp"
#include "thunder/foundation/base/Hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace thunder {

// std430 64-byte aligned GPU Material Uniform/SSBO structure
struct GpuMaterialData {
    std::uint32_t base_color_texture_index = 0;
    std::uint32_t normal_texture_index = 0;
    std::uint32_t orm_texture_index = 0;
    std::uint32_t emissive_texture_index = 0;
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 1.0f;
    float normal_scale = 1.0f;
    float alpha_cutoff = 0.5f;
    std::uint32_t flags = 0;
    std::uint32_t padding[3]{0, 0, 0};
};
static_assert(sizeof(GpuMaterialData) == 64u);

class BindlessMaterialSystem {
public:
    static constexpr std::uint32_t invalid_slot = 0xffffffffu;

    BindlessMaterialSystem();
    ~BindlessMaterialSystem() = default;

    std::uint32_t register_texture(std::uint64_t texture_asset_hash);
    bool unregister_texture(std::uint64_t texture_asset_hash);
    [[nodiscard]] std::uint32_t find_texture_slot(std::uint64_t texture_asset_hash) const noexcept;

    std::uint32_t register_material(const PbrMaterial& material);
    [[nodiscard]] const GpuMaterialData& material_data(std::uint32_t material_id) const;

    [[nodiscard]] std::span<const GpuMaterialData> gpu_materials() const noexcept {
        return gpu_materials_;
    }
    [[nodiscard]] std::size_t material_count() const noexcept { return gpu_materials_.size(); }
    [[nodiscard]] std::size_t texture_slot_count() const noexcept { return texture_slots_.size(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    std::unordered_map<std::uint64_t, std::uint32_t> texture_to_slot_;
    std::vector<std::uint64_t> texture_slots_;
    // Recycled descriptor-array indices handed out by register_texture after
    // an unregister_texture released them; keeps the bindless array compact.
    std::vector<std::uint32_t> free_slots_;
    std::vector<GpuMaterialData> gpu_materials_;
    std::vector<PbrMaterial> cpu_materials_;
};

} // namespace thunder
