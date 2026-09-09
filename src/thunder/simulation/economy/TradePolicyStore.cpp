#include "thunder/simulation/economy/TradePolicyStore.hpp"
#include <algorithm>
#include <limits>

namespace thunder {

namespace {
const TradePolicy unlimited_policy{};
}

void TradePolicyStore::resize(std::size_t country_count) {
    policies_.resize(country_count);
    used_capacity_milli_.resize(country_count, 0);
}

void TradePolicyStore::set(CountryId country, TradePolicy policy) {
    const auto index = static_cast<std::size_t>(country.value());
    if (!country.valid()) return;
    if (index >= policies_.size()) resize(index + 1u);
    policy.import_tariff_ppm = std::clamp(policy.import_tariff_ppm, 0, 1'000'000);
    policy.export_tariff_ppm = std::clamp(policy.export_tariff_ppm, 0, 1'000'000);
    policy.logistics_capacity_milli = std::max<EconomyAmount>(-1, policy.logistics_capacity_milli);
    policies_[index] = policy;
}

const TradePolicy& TradePolicyStore::get(CountryId country) const noexcept {
    const auto index = static_cast<std::size_t>(country.value());
    return country.valid() && index < policies_.size() ? policies_[index] : unlimited_policy;
}

void TradePolicyStore::begin_week() noexcept {
    std::fill(used_capacity_milli_.begin(), used_capacity_milli_.end(), EconomyAmount{0});
}

EconomyAmount TradePolicyStore::available_capacity(CountryId country) const noexcept {
    const auto index = static_cast<std::size_t>(country.value());
    if (!country.valid() || index >= policies_.size()) return std::numeric_limits<EconomyAmount>::max();
    const auto capacity = policies_[index].logistics_capacity_milli;
    if (capacity < 0) return std::numeric_limits<EconomyAmount>::max();
    return std::max<EconomyAmount>(0, saturating_sub(capacity, used_capacity_milli_[index]));
}

EconomyAmount TradePolicyStore::reserve_capacity(CountryId country, EconomyAmount requested) noexcept {
    if (requested <= 0) return 0;
    const auto index = static_cast<std::size_t>(country.value());
    if (!country.valid() || index >= policies_.size()) return requested;
    const auto capacity = policies_[index].logistics_capacity_milli;
    if (capacity < 0) return requested;
    const auto reserved = std::min(requested, available_capacity(country));
    used_capacity_milli_[index] = saturating_add(used_capacity_milli_[index], reserved);
    return reserved;
}

EconomyAmount TradePolicyStore::used_capacity(CountryId country) const noexcept {
    const auto index = static_cast<std::size_t>(country.value());
    return country.valid() && index < used_capacity_milli_.size() ? used_capacity_milli_[index] : 0;
}

bool TradePolicyStore::validate(std::size_t country_count) const noexcept {
    if (policies_.size() != used_capacity_milli_.size() || policies_.size() > country_count) return false;
    for (std::size_t i = 0; i < policies_.size(); ++i) {
        const auto& p = policies_[i];
        if (p.import_tariff_ppm < 0 || p.import_tariff_ppm > 1'000'000 ||
            p.export_tariff_ppm < 0 || p.export_tariff_ppm > 1'000'000 ||
            p.logistics_capacity_milli < -1 || used_capacity_milli_[i] < 0) return false;
        if (p.logistics_capacity_milli >= 0 && used_capacity_milli_[i] > p.logistics_capacity_milli) return false;
    }
    return true;
}

std::uint64_t TradePolicyStore::checksum() const noexcept {
    Fnv1a64 h;
    h.add(policies_.size());
    for (std::size_t i = 0; i < policies_.size(); ++i) {
        h.add(policies_[i].import_tariff_ppm);
        h.add(policies_[i].export_tariff_ppm);
        h.add(policies_[i].logistics_capacity_milli);
        h.add(used_capacity_milli_[i]);
    }
    return h.value();
}

std::size_t TradePolicyStore::memory_bytes() const noexcept {
    return sizeof(*this) + policies_.capacity() * sizeof(TradePolicy) +
        used_capacity_milli_.capacity() * sizeof(EconomyAmount);
}

} // namespace thunder
