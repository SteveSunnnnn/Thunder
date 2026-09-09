#include "thunder/presentation/render/map/PoliticalMapPageBundle.hpp"

#include <bit>
#include <cstring>
#include <stdexcept>

namespace thunder {

PoliticalMapPageBundleView::PoliticalMapPageBundleView(std::span<const std::byte> bytes) : bytes_(bytes) {
    if (bytes.size() != raw_bytes) throw std::invalid_argument("political map page bundle must be exactly 64 KiB");
}

std::span<const std::byte> PoliticalMapPageBundleView::province_payload() const noexcept {
    return bytes_.first(province_bytes);
}

std::span<const std::byte> PoliticalMapPageBundleView::coast_payload() const noexcept {
    return bytes_.subspan(province_bytes, coast_bytes);
}

void PoliticalMapPageBundleView::decode_province(ProvinceRasterPage& out) const noexcept {
    auto dst = out.samples();
    if constexpr (std::endian::native == std::endian::little) {
        std::memcpy(dst.data(), province_payload().data(), province_bytes);
    } else {
        const auto src = province_payload();
        for (std::size_t i = 0; i < dst.size(); ++i) {
            const auto lo = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(src[i * 2u]));
            const auto hi = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(src[i * 2u + 1u]));
            dst[i] = static_cast<std::uint16_t>(lo | (hi << 8u));
        }
    }
}

void PoliticalMapPageBundleView::decode_coast(CoastDistancePage& out) const noexcept {
    auto dst = out.samples();
    if constexpr (std::endian::native == std::endian::little) {
        std::memcpy(dst.data(), coast_payload().data(), coast_bytes);
    } else {
        const auto src = coast_payload();
        for (std::size_t i = 0; i < dst.size(); ++i) {
            const auto lo = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(src[i * 2u]));
            const auto hi = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(src[i * 2u + 1u]));
            dst[i] = static_cast<std::int16_t>(lo | (hi << 8u));
        }
    }
}

} // namespace thunder
