#include "thunder/content/assets/Material.hpp"

#include "thunder/foundation/base/Hash.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace thunder {
namespace {
constexpr std::array<char, 8> magic{{'T','H','U','N','D','R','M','T'}};
constexpr std::uint32_t version = 1;
constexpr std::uint32_t valid_flags =
    static_cast<std::uint32_t>(MaterialFlags::AlphaMasked) |
    static_cast<std::uint32_t>(MaterialFlags::DoubleSided) |
    static_cast<std::uint32_t>(MaterialFlags::Unlit) |
    static_cast<std::uint32_t>(MaterialFlags::ReceivesSnow);

template <class T>
void put(std::ostream& output, T value) {
    std::uint64_t bits = 0;
    if constexpr (std::is_same_v<T, float>) {
        bits = std::bit_cast<std::uint32_t>(value);
    } else {
        static_assert(std::is_integral_v<T>);
        bits = static_cast<std::uint64_t>(value);
    }
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        output.put(static_cast<char>((bits >> (index * 8u)) & 0xffu));
    }
    if (!output) {
        throw std::runtime_error("material write failed");
    }
}

template <class T>
T get(std::istream& input) {
    std::uint64_t bits = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        const int value = input.get();
        if (value < 0) {
            throw std::runtime_error("truncated material");
        }
        bits |= static_cast<std::uint64_t>(static_cast<unsigned char>(value)) << (index * 8u);
    }
    if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    } else {
        static_assert(std::is_integral_v<T>);
        return static_cast<T>(bits);
    }
}
} // namespace

void PbrMaterial::validate() const {
    const auto finite = [](float value) { return std::isfinite(value); };
    for (const float value : base_color) {
        if (!finite(value) || value < 0.0f || value > 1.0f) {
            throw std::invalid_argument("material base color must be finite UNORM");
        }
    }
    for (const float value : emissive) {
        if (!finite(value) || value < 0.0f || value > 64.0f) {
            throw std::invalid_argument("material emissive outside supported range");
        }
    }
    if (!finite(metallic) || metallic < 0.0f || metallic > 1.0f ||
        !finite(roughness) || roughness < 0.02f || roughness > 1.0f ||
        !finite(normal_scale) || normal_scale < 0.0f || normal_scale > 4.0f ||
        !finite(alpha_cutoff) || alpha_cutoff < 0.0f || alpha_cutoff > 1.0f) {
        throw std::invalid_argument("invalid PBR material scalar");
    }
    if ((static_cast<std::uint32_t>(flags) & ~valid_flags) != 0u) {
        throw std::invalid_argument("unknown material flag");
    }
}

std::uint64_t PbrMaterial::checksum() const noexcept {
    Fnv1a64 hash;
    for (const float value : base_color) hash.add(value);
    for (const float value : emissive) hash.add(value);
    hash.add(metallic);
    hash.add(roughness);
    hash.add(normal_scale);
    hash.add(alpha_cutoff);
    hash.add(static_cast<std::uint32_t>(flags));
    hash.add(base_color_texture);
    hash.add(normal_texture);
    hash.add(orm_texture);
    hash.add(emissive_texture);
    return hash.value();
}

void PbrMaterial::write(const std::filesystem::path& path) const {
    validate();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create material");
    }
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    put(output, version);
    put(output, checksum());
    for (const float value : base_color) put(output, value);
    for (const float value : emissive) put(output, value);
    put(output, metallic);
    put(output, roughness);
    put(output, normal_scale);
    put(output, alpha_cutoff);
    put(output, static_cast<std::uint32_t>(flags));
    put(output, base_color_texture);
    put(output, normal_texture);
    put(output, orm_texture);
    put(output, emissive_texture);
}

PbrMaterial PbrMaterial::read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open material");
    }
    std::array<char, 8> got{};
    input.read(got.data(), static_cast<std::streamsize>(got.size()));
    if (got != magic) {
        throw std::runtime_error("invalid material magic");
    }
    if (get<std::uint32_t>(input) != version) {
        throw std::runtime_error("unsupported material version");
    }
    const auto expected = get<std::uint64_t>(input);
    PbrMaterial material;
    for (float& value : material.base_color) value = get<float>(input);
    for (float& value : material.emissive) value = get<float>(input);
    material.metallic = get<float>(input);
    material.roughness = get<float>(input);
    material.normal_scale = get<float>(input);
    material.alpha_cutoff = get<float>(input);
    material.flags = static_cast<MaterialFlags>(get<std::uint32_t>(input));
    material.base_color_texture = get<std::uint64_t>(input);
    material.normal_texture = get<std::uint64_t>(input);
    material.orm_texture = get<std::uint64_t>(input);
    material.emissive_texture = get<std::uint64_t>(input);
    char extra = 0;
    if (input.get(extra)) {
        throw std::runtime_error("trailing material bytes");
    }
    material.validate();
    if (material.checksum() != expected) {
        throw std::runtime_error("material checksum mismatch");
    }
    return material;
}

} // namespace thunder
