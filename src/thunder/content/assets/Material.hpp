#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace thunder {

enum class MaterialFlags : std::uint32_t {
    None = 0,
    AlphaMasked = 1u << 0u,
    DoubleSided = 1u << 1u,
    Unlit = 1u << 2u,
    ReceivesSnow = 1u << 3u,
};

[[nodiscard]] constexpr MaterialFlags operator|(MaterialFlags lhs, MaterialFlags rhs) noexcept {
    return static_cast<MaterialFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

struct PbrMaterial {
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float metallic = 0.0f;
    float roughness = 1.0f;
    float normal_scale = 1.0f;
    float alpha_cutoff = 0.5f;
    MaterialFlags flags = MaterialFlags::None;
    std::uint64_t base_color_texture = 0;
    std::uint64_t normal_texture = 0;
    std::uint64_t orm_texture = 0;
    std::uint64_t emissive_texture = 0;

    [[nodiscard]] std::uint64_t checksum() const noexcept;
    void validate() const;
    void write(const std::filesystem::path& path) const;
    [[nodiscard]] static PbrMaterial read(const std::filesystem::path& path);

    friend bool operator==(const PbrMaterial&, const PbrMaterial&) = default;
};

} // namespace thunder
