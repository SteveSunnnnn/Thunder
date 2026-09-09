#include "thunder/simulation/warfare/BattlePhaseSystem.hpp"
#include <algorithm>

namespace thunder {

TacticCard BattlePhaseSystem::select_tactic(BattlePhase phase, bool is_attacker) {
    switch (phase) {
    case BattlePhase::Reconnaissance:
        return {"Probing Screen", 100'000, 100'000, 20'000};
    case BattlePhase::ArtilleryPreparation:
        return is_attacker ? TacticCard{"Creeping Barrage", 300'000, -50'000, 150'000}
                           : TacticCard{"Counter-Battery Fire", 50'000, 250'000, 100'000};
    case BattlePhase::MainAssault:
        return is_attacker ? TacticCard{"Bayonet Charge", 400'000, -100'000, 250'000}
                           : TacticCard{"Entrenched Defense", -50'000, 500'000, 180'000};
    case BattlePhase::SkirmishFlanking:
        return {"Envelopment Maneuver", 250'000, 150'000, 120'000};
    case BattlePhase::PursuitOrRetreat:
    default:
        return {"Fighting Withdrawal", -100'000, 300'000, 50'000};
    }
}

void BattlePhaseSystem::advance_battle_day(BattleState& b, float attacker_supply, float defender_supply) {
    if (b.is_concluded) return;

    b.phase_days_elapsed++;

    // Select tactics if day 1 of new phase
    if (b.phase_days_elapsed == 1) {
        b.attacker_tactic = select_tactic(b.phase, true);
        b.defender_tactic = select_tactic(b.phase, false);
    }

    // Apply daily casualties & morale impact modulated by logistics
    const float atk_base = static_cast<float>(b.attacker_tactic.casualty_inflict_ppm + 50'000);
    const float atk_units = static_cast<float>(b.attacker_manpower / 1000);
    const std::int32_t atk_inflict = static_cast<std::int32_t>((atk_base * attacker_supply * atk_units) / 1000.0f);

    const float def_base = static_cast<float>(b.defender_tactic.casualty_inflict_ppm + 50'000);
    const float def_units = static_cast<float>(b.defender_manpower / 1000);
    const std::int32_t def_inflict = static_cast<std::int32_t>((def_base * defender_supply * def_units) / 1000.0f);

    b.defender_manpower = std::max(0, b.defender_manpower - atk_inflict);
    b.attacker_manpower = std::max(0, b.attacker_manpower - def_inflict);

    b.attacker_morale_ppm = std::max(0, b.attacker_morale_ppm - 40'000 - static_cast<std::int32_t>((1.0f - attacker_supply) * 100'000));
    b.defender_morale_ppm = std::max(0, b.defender_morale_ppm - 35'000 - static_cast<std::int32_t>((1.0f - defender_supply) * 100'000));

    // Check conclusion
    if (b.attacker_morale_ppm == 0 || b.defender_morale_ppm == 0 ||
        b.attacker_manpower == 0 || b.defender_manpower == 0) {
        b.phase = BattlePhase::Concluded;
        b.is_concluded = true;
        return;
    }

    // Phase transition after 3 days
    if (b.phase_days_elapsed >= 3) {
        b.phase_days_elapsed = 0;
        b.phase = static_cast<BattlePhase>(static_cast<std::uint8_t>(b.phase) + 1);
        // SkirmishFlanking advances TO PursuitOrRetreat, which still has to be
        // fought for its own 3 days. Testing `>= PursuitOrRetreat` concluded
        // the battle the moment that phase was entered, so the phase and its
        // tactic card were never simulated.
        if (b.phase >= BattlePhase::Concluded) {
            b.phase = BattlePhase::Concluded;
            b.is_concluded = true;
        }
    }
}

} // namespace thunder
