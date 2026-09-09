#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace thunder {

struct AiStrategyParameters {
    std::int32_t aggression_weight_ppm = 500'000;
    std::int32_t industrial_focus_ppm = 600'000;
    std::int32_t colonial_focus_ppm = 300'000;
    std::int32_t balance_of_power_sensitivity_ppm = 750'000;
    std::int32_t debt_tolerance_ppm = 250'000;
};

struct AiBuildingProposal {
    ProvinceId province{};
    std::uint32_t building_type_hash = 0;
    std::int32_t priority_score = 0;
    std::int32_t estimated_roi_ppm = 0;
};

struct AiCoalitionProposal {
    CountryId target_hegemon{};
    std::vector<CountryId> proposed_members;
    std::int32_t collective_threat_score = 0;
    bool is_activated = false;
};

class StrategicAiPlanner {
public:
    StrategicAiPlanner() = default;

    void set_country_strategy(CountryId country, const AiStrategyParameters& params);
    [[nodiscard]] const AiStrategyParameters& get_country_strategy(CountryId country) const noexcept;

    // Evaluates building investment opportunities based on market price shortages and ROI
    [[nodiscard]] std::vector<AiBuildingProposal> evaluate_construction_queue(
        CountryId country,
        std::span<const ProvinceId> owned_provinces,
        std::span<const std::pair<std::uint32_t, std::int32_t>> market_shortages) const;

    // Evaluates anti-hegemonic coalitions when a major power exceeds threat thresholds
    [[nodiscard]] std::optional<AiCoalitionProposal> evaluate_balance_of_power_coalition(
        CountryId observer,
        CountryId potential_threat,
        std::int32_t threat_infamy,
        std::int32_t threat_military_power,
        std::span<const std::pair<CountryId, std::int32_t>> neighboring_powers) const;

private:
    std::vector<std::pair<CountryId, AiStrategyParameters>> strategies_;
    static inline const AiStrategyParameters default_params_{};
};

} // namespace thunder
