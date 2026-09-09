#pragma once

#include "thunder/presentation/render/map/CoastDistancePage.hpp"
#include "thunder/presentation/render/map/ProvinceRasterPage.hpp"

#include <cstddef>
#include <span>

namespace thunder {

// On-disk/GPU upload bundle layout for one virtual political-map page.
// Keeping both layers together guarantees identical residency generations.
class PoliticalMapPageBundleView {
public:
    static constexpr std::size_t province_bytes = sizeof(ProvinceRasterPage::Storage);
    static constexpr std::size_t coast_bytes = sizeof(CoastDistancePage::Storage);
    static constexpr std::size_t raw_bytes = province_bytes + coast_bytes;

    explicit PoliticalMapPageBundleView(std::span<const std::byte> bytes);

    [[nodiscard]] std::span<const std::byte> province_payload() const noexcept;
    [[nodiscard]] std::span<const std::byte> coast_payload() const noexcept;
    void decode_province(ProvinceRasterPage& out) const noexcept;
    void decode_coast(CoastDistancePage& out) const noexcept;

private:
    std::span<const std::byte> bytes_;
};

static_assert(PoliticalMapPageBundleView::raw_bytes == 64u * 1024u);

} // namespace thunder
