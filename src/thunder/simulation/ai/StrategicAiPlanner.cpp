#include "thunder/simulation/ai/StrategicAiPlanner.hpp"
#include <algorithm>

namespace thunder {

void StrategicAiPlanner::set_country_strategy(CountryId country, const AiStrategyParameters& params) {
    for (auto& [cid, st] : strategies_) {
        if (cid == country) {
            st = params;
            return;
        }
    }
    strategies_.push_back({country, params});
}

const AiStrategyParameters& StrategicAiPlanner::get_country_strategy(CountryId country) const noexcept {
    for (const auto& [cid, st] : strategies_) {
        if (cid == country) return st;
    }
    return default_params_;
}

std::vector<AiBuildingProposal> StrategicAiPlanner::evaluate_construction_queue(
    CountryId country,
    std::span<const ProvinceId> owned_provinces,
    std::span<const std::pair<std::uint32_t, std::int32_t>> market_shortages) const {
    std::vector<AiBuildingProposal> proposals;
    if (owned_provinces.empty() || market_shortages.empty()) return proposals;

    const auto& strat = get_country_strategy(country);

    for (const auto& [good_hash, shortage_units] : market_shortages) {
        if (shortage_units <= 0) continue;

        AiBuildingProposal prop;
        prop.province = owned_provinces[0];
        prop.building_type_hash = good_hash ^ 0x9e3779b9u;
        prop.estimated_roi_ppm = std::min(1'000'000, shortage_units * 10'000);
        prop.priority_score = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(prop.estimated_roi_ppm) * strat.industrial_focus_ppm) / 1'000'000
        );

        proposals.push_back(prop);
    }

    std::sort(proposals.begin(), proposals.end(), [](const auto& a, const auto& b) {
        return a.priority_score > b.priority_score;
    });
    return proposals;
}

std::optional<AiCoalitionProposal> StrategicAiPlanner::evaluate_balance_of_power_coalition(
    CountryId observer,
    CountryId potential_threat,
    std::int32_t threat_infamy,
    std::int32_t threat_military_power,
    std::span<const std::pair<CountryId, std::int32_t>> neighboring_powers) const {
    if (observer == potential_threat) return std::nullopt;

    const auto& strat = get_country_strategy(observer);

    // Infamy threat threshold modulated by script sensitivity parameter
    constexpr std::int32_t base_infamy_threshold = 25;
    const std::int32_t threat_weight = (threat_infamy * strat.balance_of_power_sensitivity_ppm) / 1'000'000;

    if (threat_weight < base_infamy_threshold) return std::nullopt;

    AiCoalitionProposal coalition;
    coalition.target_hegemon = potential_threat;
    coalition.proposed_members.push_back(observer);
    std::int32_t collective_power = 0;

    for (const auto& [neighbor, mil_power] : neighboring_powers) {
        if (neighbor == potential_threat || neighbor == observer) continue;
        coalition.proposed_members.push_back(neighbor);
        collective_power += mil_power;
    }

    coalition.collective_threat_score = threat_weight * 1000;
    coalition.is_activated = (collective_power >= threat_military_power * 8 / 10);

    return coalition;
}

} // namespace thunder
