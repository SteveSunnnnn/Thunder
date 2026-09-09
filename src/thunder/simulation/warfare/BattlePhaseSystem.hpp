#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace thunder {

enum class BattlePhase : std::uint8_t {
    Reconnaissance = 0,
    ArtilleryPreparation = 1,
    MainAssault = 2,
    SkirmishFlanking = 3,
    PursuitOrRetreat = 4,
    Concluded = 5
};

struct TacticCard {
    std::string name;
    std::int32_t offense_mod_ppm = 0;
    std::int32_t defense_mod_ppm = 0;
    std::int32_t casualty_inflict_ppm = 0;
};

struct BattleState {
    uint64_t battle_id = 0;
    ProvinceId location{};
    CountryId attacker{};
    CountryId defender{};
    std::int32_t attacker_manpower = 10000;
    std::int32_t defender_manpower = 10000;
    std::int32_t attacker_morale_ppm = 1'000'000;
    std::int32_t defender_morale_ppm = 1'000'000;
    BattlePhase phase = BattlePhase::Reconnaissance;
    std::uint32_t phase_days_elapsed = 0;
    TacticCard attacker_tactic;
    TacticCard defender_tactic;
    bool is_concluded = false;
};

class BattlePhaseSystem {
public:
    BattlePhaseSystem() = default;

    // Advances battle daily simulation through tactical phases
    void advance_battle_day(BattleState& battle, float attacker_supply, float defender_supply);

    // Data-driven tactic card selection
    [[nodiscard]] static TacticCard select_tactic(BattlePhase phase, bool is_attacker);
};

} // namespace thunder
