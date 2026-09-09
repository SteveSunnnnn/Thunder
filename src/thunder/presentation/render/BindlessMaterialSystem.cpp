#include "thunder/presentation/render/BindlessMaterialSystem.hpp"

#include <stdexcept>

namespace thunder {

BindlessMaterialSystem::BindlessMaterialSystem() {
    // Slot 0 is reserved for white/default 1x1 fallback texture
    texture_slots_.push_back(0u);
    texture_to_slot_[0u] = 0u;
}

std::uint32_t BindlessMaterialSystem::register_texture(std::uint64_t texture_asset_hash) {
    if (texture_asset_hash == 0) return 0u;
    const auto it = texture_to_slot_.find(texture_asset_hash);
    if (it != texture_to_slot_.end()) {
        return it->second;
    }

    std::uint32_t slot = 0;
    if (!free_slots_.empty()) {
        slot = free_slots_.back();
        free_slots_.pop_back();
        texture_slots_[slot] = texture_asset_hash;
    } else {
        slot = static_cast<std::uint32_t>(texture_slots_.size());
        texture_slots_.push_back(texture_asset_hash);
    }
    texture_to_slot_[texture_asset_hash] = slot;
    return slot;
}

bool BindlessMaterialSystem::unregister_texture(std::uint64_t texture_asset_hash) {
    if (texture_asset_hash == 0) return false;
    const auto it = texture_to_slot_.find(texture_asset_hash);
    if (it == texture_to_slot_.end()) return false;
    const auto slot = it->second;
    texture_to_slot_.erase(it);
    // Point the dead descriptor at the white fallback (hash 0) and recycle the
    // index so the descriptor array stops growing and never aliases a live
    // texture from a later registration.
    texture_slots_[slot] = 0u;
    free_slots_.push_back(slot);
    return true;
}

std::uint32_t BindlessMaterialSystem::find_texture_slot(std::uint64_t texture_asset_hash) const noexcept {
    if (texture_asset_hash == 0) return 0u;
    const auto it = texture_to_slot_.find(texture_asset_hash);
    return it != texture_to_slot_.end() ? it->second : 0u;
}

std::uint32_t BindlessMaterialSystem::register_material(const PbrMaterial& material) {
    const auto mat_id = static_cast<std::uint32_t>(gpu_materials_.size());
    cpu_materials_.push_back(material);

    GpuMaterialData gpu_mat;
    gpu_mat.base_color_texture_index = register_texture(material.base_color_texture);
    gpu_mat.normal_texture_index = register_texture(material.normal_texture);
    gpu_mat.orm_texture_index = register_texture(material.orm_texture);
    gpu_mat.emissive_texture_index = register_texture(material.emissive_texture);

    gpu_mat.base_color = material.base_color;
    gpu_mat.metallic = material.metallic;
    gpu_mat.roughness = material.roughness;
    gpu_mat.normal_scale = material.normal_scale;
    gpu_mat.alpha_cutoff = material.alpha_cutoff;
    gpu_mat.flags = static_cast<std::uint32_t>(material.flags);

    gpu_materials_.push_back(gpu_mat);
    return mat_id;
}

const GpuMaterialData& BindlessMaterialSystem::material_data(std::uint32_t material_id) const {
    if (material_id >= gpu_materials_.size()) {
        throw std::out_of_range("invalid material_id in BindlessMaterialSystem");
    }
    return gpu_materials_[material_id];
}

std::size_t BindlessMaterialSystem::memory_bytes() const noexcept {
    return sizeof(*this) +
           texture_to_slot_.bucket_count() * sizeof(void*) +
           texture_to_slot_.size() * (sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(void*)) +
           texture_slots_.capacity() * sizeof(std::uint64_t) +
           gpu_materials_.capacity() * sizeof(GpuMaterialData) +
           cpu_materials_.capacity() * sizeof(PbrMaterial);
}

std::uint64_t BindlessMaterialSystem::checksum() const noexcept {
    Fnv1a64 h;
    h.add(static_cast<std::uint64_t>(gpu_materials_.size()));
    for (const auto& mat : gpu_materials_) {
        h.add(mat.base_color_texture_index);
        h.add(mat.normal_texture_index);
        h.add(mat.orm_texture_index);
        h.add(mat.emissive_texture_index);
        for (const auto c : mat.base_color) h.add(c);
        h.add(mat.metallic);
        h.add(mat.roughness);
        h.add(mat.normal_scale);
        h.add(mat.alpha_cutoff);
        h.add(mat.flags);
    }
    return h.value();
}

} // namespace thunder
