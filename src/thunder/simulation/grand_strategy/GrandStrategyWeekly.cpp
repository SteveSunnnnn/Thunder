#include "thunder/simulation/grand_strategy/GrandStrategyStore.hpp"

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace thunder {
namespace {

std::int32_t clamp_relation(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, -100'000, 100'000));
}

std::uint32_t clamp_ppm(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, 1'000'000u));
}

[[nodiscard]] std::uint64_t effective_force(const ArmyRecord& army) noexcept {
    return (static_cast<std::uint64_t>(army.manpower) *
            static_cast<std::uint64_t>(army.organization_ppm)) / 1'000'000u;
}

} // namespace

void GrandStrategyStore::run_politics_weekly() {
    for (const auto& group : interest_groups_) (void)ensure_government(group.country);
    for (const auto& party : political_partys_) (void)ensure_government(party.country);
    for (const auto& enactment : law_enactments_) (void)ensure_government(enactment.country);

    for (auto& enactment : law_enactments_) {
        if (!enactment.active || enactment.passed) continue;
        std::uint64_t total_clout = 0u;
        std::uint64_t supporting_clout = 0u;
        for (const auto& ig : interest_groups_) {
            if (ig.country != enactment.country) continue;
            total_clout += ig.clout_ppm;
            if (ig.approval_milli >= 0) supporting_clout += ig.clout_ppm;
        }
        if (total_clout != 0u) {
            enactment.support_ppm = clamp_ppm((supporting_clout * 1'000'000u) / total_clout);
        }
        const auto advance = static_cast<std::uint32_t>((static_cast<std::uint64_t>(enactment.support_ppm) * 20'000u) / 1'000'000u);
        enactment.progress_ppm = clamp_ppm(static_cast<std::uint64_t>(enactment.progress_ppm) + std::max<std::uint32_t>(1'000u, advance));
        if (enactment.progress_ppm >= 1'000'000u) {
            enactment.passed = true;
            enactment.active = false;
            bool found = false;
            for (auto& law : laws_) {
                if (law.country == enactment.country && law.key_hash == enactment.law_hash) {
                    law.enacted = true;
                    found = true;
                    break;
                }
            }
            if (!found) (void)add_law({enactment.country, enactment.law_hash, true});

            // Interest group political satisfaction swings upon legislative victory/defeat
            for (auto& ig : interest_groups_) {
                if (ig.country != enactment.country) continue;
                if (ig.approval_milli >= 0) {
                    ig.approval_milli = std::min<std::int32_t>(100'000, ig.approval_milli + 25'000);
                } else {
                    ig.approval_milli = std::max<std::int32_t>(-100'000, ig.approval_milli - 25'000);
                }
            }
        }
    }

    for (auto& parl : parliaments_) {
        if (parl.elections_enabled) {
            if (parl.weeks_to_next_election > 0u) --parl.weeks_to_next_election;
            if (parl.weeks_to_next_election == 0u) {
                run_parliamentary_election(parl.country);
            }
        }
    }


    for (auto& gov : governments_) {
        std::uint64_t party_total = 0u;
        std::uint64_t party_max = 0u;
        for (const auto& party : political_partys_) {
            if (party.country != gov.country) continue;
            party_total += party.support_ppm;
            party_max = std::max<std::uint64_t>(party_max, party.support_ppm);
        }
        gov.legitimacy_ppm = party_total == 0u ? 500'000u :
            clamp_ppm((party_max * 1'000'000u) / party_total);

        std::int64_t weighted_approval = 0;
        std::uint64_t clout_total = 0u;
        for (const auto& group : interest_groups_) {
            if (group.country != gov.country) continue;
            weighted_approval += static_cast<std::int64_t>(group.clout_ppm) * group.approval_milli;
            clout_total += group.clout_ppm;
        }
        gov.stability_milli = clout_total == 0u ? 0 :
            static_cast<std::int32_t>(std::clamp<std::int64_t>(weighted_approval / static_cast<std::int64_t>(clout_total), -100'000, 100'000));
    }
}


void GrandStrategyStore::run_diplomacy_weekly() {
    for (std::size_t i = 0; i < diplomatic_plays_.size(); ++i) {
        auto& play = diplomatic_plays_[i];
        if (play.phase == DiplomaticPlayPhase::Resolved) continue;
        const auto play_id = DiplomaticPlayId{static_cast<DiplomaticPlayId::rep_type>(i)};
        const auto state_id = DiplomaticPlayStateId{static_cast<DiplomaticPlayStateId::rep_type>(i)};
        if (state_id.value() >= diplomatic_play_states_.size()) continue;
        auto& state = diplomatic_play_states_[state_id.value()];
        ++state.weeks_in_phase;
        state.escalation_ppm = clamp_ppm(static_cast<std::uint64_t>(state.escalation_ppm) + 25'000u);

        const auto relation_id = ensure_diplomatic_relation(play.initiator, play.target);
        auto& relation = diplomatic_relations_[relation_id.value()];

        switch (play.phase) {
            case DiplomaticPlayPhase::Opening:
                if (state.weeks_in_phase >= 2u) {
                    play.phase = DiplomaticPlayPhase::Maneuvering;
                    state.weeks_in_phase = 0u;
                    relation.relation_milli = clamp_relation(static_cast<std::int64_t>(relation.relation_milli) - 10'000);
                    relation.tension_ppm = clamp_ppm(static_cast<std::uint64_t>(relation.tension_ppm) + 100'000u);
                }
                break;
            case DiplomaticPlayPhase::Maneuvering:
                if (state.weeks_in_phase >= 3u) {
                    play.phase = DiplomaticPlayPhase::Countdown;
                    state.weeks_in_phase = 0u;
                    relation.relation_milli = clamp_relation(static_cast<std::int64_t>(relation.relation_milli) - 20'000);
                    relation.tension_ppm = clamp_ppm(static_cast<std::uint64_t>(relation.tension_ppm) + 200'000u);
                }
                break;
            case DiplomaticPlayPhase::Countdown:
                if (state.weeks_in_phase >= 3u) {
                    play.phase = DiplomaticPlayPhase::War;
                    state.weeks_in_phase = 0u;
                    (void)add_war({play_id, play.initiator, play.target, 0, 0u, true});
                    relation.relation_milli = -100'000;
                    relation.tension_ppm = 1'000'000u;
                }
                break;

            case DiplomaticPlayPhase::War: {
                const auto war = war_for_play(play_id);
                if (war.valid() && !wars_[war.value()].active) play.phase = DiplomaticPlayPhase::Resolved;
                break;
            }
            case DiplomaticPlayPhase::Resolved:
                break;
        }
    }

    // Peacetime natural diplomatic relation drift and tension decay
    for (auto& rel : diplomatic_relations_) {
        bool at_war = false;
        for (const auto& war : wars_) {
            if (war.active && ((war.attacker == rel.first && war.defender == rel.second) ||
                               (war.attacker == rel.second && war.defender == rel.first))) {
                at_war = true;
                break;
            }
        }
        if (at_war) {
            rel.relation_milli = -100'000;
            rel.tension_ppm = 1'000'000u;
            continue;
        }

        if (rel.relation_milli > 0) {
            rel.relation_milli = std::max(0, rel.relation_milli - 50);
        } else if (rel.relation_milli < 0) {
            rel.relation_milli = std::min(0, rel.relation_milli + 50);
        }
        if (rel.tension_ppm > 0) {
            rel.tension_ppm = rel.tension_ppm > 2'000u ? (rel.tension_ppm - 2'000u) : 0u;
        }
    }
}


void GrandStrategyStore::run_warfare_weekly(World* world) {
    for (auto& army : armys_) {
        if (army.organization_ppm < 1'000'000u) {
            army.organization_ppm = std::min(1'000'000u, army.organization_ppm + 25'000u);
        }
    }

    for (auto& war : wars_) {
        if (!war.active) continue;
        ++war.weeks;
        std::int64_t score_delta_sum = 0;
        std::size_t participating_fronts = 0;

        for (std::size_t front_index = 0; front_index < fronts_.size(); ++front_index) {
            auto& front = fronts_[front_index];
            const bool same = front.first == war.attacker && front.second == war.defender;
            const bool reverse = front.first == war.defender && front.second == war.attacker;
            if (!same && !reverse) continue;
            ++participating_fronts;

            std::uint64_t attacker_force = 0u;
            std::uint64_t defender_force = 0u;

            // Commander leadership presence (maintains unit cohesion and reduces organization collapse)
            bool attacker_has_leader = false;
            bool defender_has_leader = false;
            for (const auto& cmd : commanders_) {
                if (cmd.location == front.state) {
                    if (cmd.country == war.attacker) attacker_has_leader = true;
                    else if (cmd.country == war.defender) defender_has_leader = true;
                }
            }

            for (const auto& army : armys_) {
                if (army.location != front.state) continue;
                const auto eff = effective_force(army);
                if (army.country == war.attacker) {
                    attacker_force += eff;
                } else if (army.country == war.defender) {
                    defender_force += eff;
                }
            }

            // Physical frontline frontage capacity determined by corridor geography (no magic multipliers)
            TopographyType topo = TopographyType::Flatlands;
            if (world != nullptr && front.state.valid()) {
                const auto capital = world->geography.state_capital(front.state);
                if (capital.valid() && capital.value() < world->geography.province_count()) {
                    const auto compound = world->geography.province_compound_terrain(capital);
                    topo = compound.topography;
                }
            }

            // Geographic frontline combat width capacity (excess troops remain in reserve)
            const auto max_frontage = static_cast<std::uint64_t>(frontage_capacity_manpower(topo));
            attacker_force = std::min(attacker_force, max_frontage);
            defender_force = std::min(defender_force, max_frontage);

            const std::uint64_t total_force = attacker_force + defender_force;
            std::int32_t delta = 0;
            if (total_force != 0u) {
                const auto difference = static_cast<std::int64_t>(attacker_force) - static_cast<std::int64_t>(defender_force);
                delta = static_cast<std::int32_t>(std::clamp<std::int64_t>((difference * 10'000) /
                    static_cast<std::int64_t>(total_force), -10'000, 10'000));
            }
            score_delta_sum += delta;
            front.progress_milli = clamp_relation(static_cast<std::int64_t>(front.progress_milli) + delta);

            if (attacker_force != 0u && defender_force != 0u) {
                const auto front_id = FrontId{static_cast<FrontId::rep_type>(front_index)};
                BattleRecord* battle = nullptr;
                for (auto& candidate : battles_) {
                    if (candidate.front == front_id && !candidate.resolved) { battle = &candidate; break; }
                }
                if (battle == nullptr) {
                    const auto attackers = static_cast<std::uint32_t>(std::min<std::uint64_t>(attacker_force, std::numeric_limits<std::uint32_t>::max()));
                    const auto defenders = static_cast<std::uint32_t>(std::min<std::uint64_t>(defender_force, std::numeric_limits<std::uint32_t>::max()));
                    (void)add_battle({front_id, attackers, defenders, 0, false});
                    battle = &battles_.back();
                }
                battle->attackers = static_cast<std::uint32_t>(std::min<std::uint64_t>(attacker_force, std::numeric_limits<std::uint32_t>::max()));
                battle->defenders = static_cast<std::uint32_t>(std::min<std::uint64_t>(defender_force, std::numeric_limits<std::uint32_t>::max()));
                battle->progress_milli = clamp_relation(static_cast<std::int64_t>(battle->progress_milli) + static_cast<std::int64_t>(delta) * 5);
                if (battle->progress_milli >= 100'000 || battle->progress_milli <= -100'000) battle->resolved = true;
            }

            for (auto& army : armys_) {
                if (army.location != front.state || (army.country != war.attacker && army.country != war.defender)) continue;
                const auto opposing = army.country == war.attacker ? defender_force : attacker_force;
                if (opposing == 0u || army.manpower == 0u) continue;
                const auto raw_loss = std::max<std::uint64_t>(1u, opposing / 5'000u);
                const auto loss = static_cast<std::uint32_t>(std::min<std::uint64_t>(raw_loss, army.manpower));
                army.manpower -= loss;
                auto raw_org_loss = std::min<std::uint64_t>(20'000u, 5'000u + opposing / 10'000u);
                if ((army.country == war.attacker && attacker_has_leader) ||
                    (army.country == war.defender && defender_has_leader)) {
                    raw_org_loss = (raw_org_loss * 85u) / 100u;
                }
                const auto org_loss = static_cast<std::uint32_t>(std::min<std::uint64_t>(raw_org_loss, army.organization_ppm));
                army.organization_ppm -= org_loss;
            }
            // Civilian casualties & devastation in active warzones (Taiping Rebellion realism)
            if (world != nullptr && (attacker_force > 0u || defender_force > 0u)) {
                world->geography.add_state_resistance_ppm(front.state, 3'000);
                const auto province_states = world->geography.province_states();
                const auto pop_provinces = world->pops.provinces();
                const auto pop_sizes = world->pops.populations();
                const auto pop_sols = world->pops.sol_all();

                for (std::size_t pi = 0; pi < world->pops.size(); ++pi) {
                    if (!world->pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
                    if (pi < pop_provinces.size() && pop_provinces[pi].valid()) {
                        const auto prov_id = pop_provinces[pi].value();
                        if (prov_id < province_states.size() && province_states[prov_id] == front.state) {
                            const auto cur_pop = pop_sizes[pi];
                            const auto casualty = static_cast<PopulationCount>(std::max<std::uint64_t>(1u, static_cast<std::uint64_t>(cur_pop) * 15u / 10'000u));
                            if (cur_pop > casualty) {
                                world->pops.set_population(PopId{static_cast<PopId::rep_type>(pi)}, cur_pop - casualty);
                            }
                            if (pi < pop_sols.size() && pop_sols[pi] > 1'000) {
                                world->pops.set_standard_of_living_milli(PopId{static_cast<PopId::rep_type>(pi)}, pop_sols[pi] - 80);
                            }
                        }
                    }
                }
            }
        }

        if (participating_fronts != 0u) {
            const auto average = score_delta_sum / static_cast<std::int64_t>(participating_fronts);
            war.war_score_milli = clamp_relation(static_cast<std::int64_t>(war.war_score_milli) + average);
        }
        if (war.war_score_milli >= 100'000 || war.war_score_milli <= -100'000 || war.weeks >= 260u) {
            war.active = false;
            if (war.play.valid() && static_cast<std::size_t>(war.play.value()) < diplomatic_plays_.size()) {
                diplomatic_plays_[war.play.value()].phase = DiplomaticPlayPhase::Resolved;
                const auto winner = war.war_score_milli >= 0 ? war.attacker : war.defender;
                const auto loser = war.war_score_milli >= 0 ? war.defender : war.attacker;
                enforce_peace_treaty(war.play, winner, loser, world);
            }
        }
    }
}

void GrandStrategyStore::run_military_weekly(World& world) {
    const auto province_owners = world.geography.province_owners();
    const auto pop_provinces = world.pops.provinces();
    const auto pop_sizes = world.pops.populations();

    for (auto& army : armys_) {
        if (!army.country.valid()) continue;
        const auto ci = static_cast<std::size_t>(army.country.value());
        if (ci >= world.countries.size()) continue;

        constexpr std::uint32_t target_manpower = 25'000u;
        const auto treasury = world.countries.treasury_milli(army.country);
        const bool solvent = !world.countries.is_in_default(army.country) && treasury > 0;

        // 1. Military upkeep and wages (proportional to army manpower)
        const auto wage_per_soldier_milli = 10LL;
        const auto weekly_wage_cost = static_cast<EconomyAmount>(army.manpower) * wage_per_soldier_milli;
        if (treasury >= weekly_wage_cost) {
            world.countries.add_treasury_milli(army.country, -weekly_wage_cost);
        } else {
            army.organization_ppm = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(army.organization_ppm) * 920'000u) / 1'000'000u);
            const auto deserters = static_cast<std::uint32_t>(static_cast<std::uint64_t>(army.manpower) * 25u / 10'000u);
            if (army.manpower > deserters) army.manpower -= deserters;
            continue;
        }

        // 2. Continuous Reinforcements & Enlistment Pipeline from domestic POPs
        if (solvent && army.manpower < target_manpower) {
            const auto deficit = target_manpower - army.manpower;
            const auto max_recruits = std::min<std::uint32_t>(deficit, 400u);

            PopulationCount recruits_gathered = 0;
            for (std::size_t pi = 0; pi < world.pops.size() && recruits_gathered < max_recruits; ++pi) {
                if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
                if (pi < pop_provinces.size() && pop_provinces[pi].valid()) {
                    const auto prov_id = pop_provinces[pi].value();
                    if (prov_id < province_owners.size() && province_owners[prov_id] == army.country) {
                        const auto pop_size = pop_sizes[pi];
                        if (pop_size > 50u) {
                            const auto to_recruit = std::min<PopulationCount>(
                                static_cast<PopulationCount>(max_recruits - recruits_gathered),
                                static_cast<PopulationCount>(pop_size / 300u + 1u));
                            world.pops.set_population(PopId{static_cast<PopId::rep_type>(pi)}, pop_size - to_recruit);
                            recruits_gathered += to_recruit;
                        }
                    }
                }
            }

            if (recruits_gathered > 0 && army.manpower > 0) {
                const auto old_force = static_cast<std::uint64_t>(army.manpower) * army.organization_ppm;
                const auto recruit_force = static_cast<std::uint64_t>(recruits_gathered) * 500'000u;
                army.manpower += recruits_gathered;
                army.organization_ppm = static_cast<std::uint32_t>((old_force + recruit_force) / army.manpower);
            } else if (recruits_gathered > 0) {
                army.manpower += recruits_gathered;
                army.organization_ppm = 500'000u;
            }
        }

        // 3. Training & Drilling Organization Recovery
        if (solvent && army.organization_ppm < 1'000'000u) {
            army.organization_ppm = std::min<std::uint32_t>(1'000'000u, army.organization_ppm + 20'000u);
        }
    }
}


void GrandStrategyStore::run_institutions_weekly(World& world) {
    for (const auto& inst : institutions_) {
        if (!inst.country.valid() || inst.level == 0) continue;
        const auto ci = static_cast<std::size_t>(inst.country.value());
        if (ci >= world.countries.size()) continue;

        const auto pop = world.countries.population(inst.country);
        const auto base_cost = 5'000LL;
        const auto scale = static_cast<EconomyAmount>(std::max<double>(1.0, 1.0 + pop / 500'000.0));
        const auto weekly_cost = inst.level * base_cost * scale;

        const auto treasury = world.countries.treasury_milli(inst.country);
        if (treasury >= weekly_cost) {
            world.countries.add_treasury_milli(inst.country, -weekly_cost);
        } else {
            if (treasury > 0) world.countries.add_treasury_milli(inst.country, -treasury);
            world.countries.add_prestige(inst.country, -0.05);
        }
    }
}

void GrandStrategyStore::run_tech_spread_weekly(World& world, std::uint64_t weekly_tick) {
    (void)world;
    // Keep the roll fresh every week, exactly as ResearchSystem does: a pinned
    // week comes from the GameClock, otherwise the internal counter advances.
    if (weekly_tick == automatic_weekly_tick) {
        if (weekly_ticks_ != automatic_weekly_tick) ++weekly_ticks_;
    } else {
        weekly_ticks_ = weekly_tick;
    }
    for (std::size_t ti = 0; ti < technologys_.size(); ++ti) {
        auto& tech = technologys_[ti];
        if (tech.unlocked || !tech.country.valid()) continue;

        bool has_advanced_partner = false;
        CountryId partner{};
        for (const auto& other_tech : technologys_) {
            if (other_tech.key_hash == tech.key_hash && other_tech.unlocked && other_tech.country.valid() && other_tech.country != tech.country) {
                if (has_active_treaty(tech.country, other_tech.country, TreatyKind::TradeAgreement) ||
                    has_active_treaty(tech.country, other_tech.country, TreatyKind::Alliance) ||
                    has_active_treaty(tech.country, other_tech.country, TreatyKind::InvestmentRights) ||
                    relation_milli(tech.country, other_tech.country) >= 25'000) {
                    has_advanced_partner = true;
                    partner = other_tech.country;
                    break;
                }
            }
        }

        if (has_advanced_partner) {
            // Probabilistic roll (e.g. 25% chance per week). The week counter
            // must be part of the sample: a failed roll changes no state, so
            // without it the hash is identical next week and every technology
            // that lost the first roll stays locked out forever.
            Fnv1a64 roll_hash;
            roll_hash.add(weekly_ticks_);
            roll_hash.add(static_cast<std::uint32_t>(tech.country.value()));
            roll_hash.add(tech.key_hash);
            roll_hash.add(static_cast<std::uint32_t>(partner.value()));
            roll_hash.add(tech.progress_ppm);
            const auto roll = static_cast<std::uint32_t>(roll_hash.value() % 1'000'000u);
            if (roll >= 250'000u) continue;

            // Gradual but historically visible progress: 8000 ppm per weekly
            // success at a 25% chance is ~2000 ppm/week expected, so a spread
            // technology completes in roughly a decade (V3 order), not the
            // five centuries the old 150 ppm step implied.
            tech.progress_ppm = std::min<std::uint32_t>(1'000'000u, tech.progress_ppm + 8'000u);
            if (tech.progress_ppm >= 1'000'000u) {
                tech.unlocked = true;
            }
        }
    }
}

void GrandStrategyStore::run_state_resistance_weekly(World& world) {
    const auto state_count = world.geography.state_count();
    if (state_count == 0) return;

    const auto pop_provinces = world.pops.provinces();
    const auto pop_sols = world.pops.sol_all();
    const auto pop_radicals = world.pops.radicals_all();
    const auto pop_loyalists = world.pops.loyalists_all();
    const auto pop_population = world.pops.populations();
    const auto province_states = world.geography.province_states();
    const auto pop_size = world.pops.size();

    // Single-pass bucket aggregation: O(POPs + States)
    std::vector<std::int64_t> sol_sum(state_count, 0);
    std::vector<std::uint64_t> pop_count(state_count, 0u);
    std::vector<std::uint64_t> radical_sum(state_count, 0u);
    std::vector<std::uint64_t> loyalist_sum(state_count, 0u);

    for (std::size_t pi = 0; pi < pop_size; ++pi) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
        if (pi < pop_provinces.size() && pop_provinces[pi].valid()) {
            const auto prov_id = pop_provinces[pi].value();
            if (prov_id < province_states.size()) {
                const auto state = province_states[prov_id];
                if (state.valid() && static_cast<std::size_t>(state.value()) < state_count && pi < pop_sols.size()) {
                    const auto size = static_cast<std::uint64_t>(pop_population[pi]);
                    sol_sum[state.value()] += pop_sols[pi] * static_cast<std::int64_t>(size);
                    pop_count[state.value()] += size;
                    radical_sum[state.value()] += pop_radicals[pi];
                    loyalist_sum[state.value()] += pop_loyalists[pi];
                }
            }
        }
    }

    // Dynamic State Infrastructure & Market Access (MAPI) calculation
    // Infrastructure Load from Buildings in each state
    std::vector<std::uint32_t> infra_load(state_count, 0u);
    const auto building_provinces = world.buildings.provinces();
    const auto building_levels = world.buildings.levels();
    for (std::size_t bi = 0; bi < world.buildings.size(); ++bi) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi))) continue;
        const auto prov = building_provinces[bi];
        if (!prov.valid() || prov.value() >= province_states.size()) continue;
        const auto state = province_states[prov.value()];
        if (!state.valid() || static_cast<std::size_t>(state.value()) >= state_count) continue;
        infra_load[state.value()] += static_cast<std::uint32_t>(building_levels[bi]) * 2u;
    }

    for (std::size_t si = 0; si < state_count; ++si) {
        const StateId state{static_cast<StateId::rep_type>(si)};
        const auto owner = world.geography.state_owner(state);
        if (!owner.valid() || static_cast<std::size_t>(owner.value()) >= world.countries.size()) continue;

        const auto count = pop_count[si];
        const auto avg_sol = count > 0 ? sol_sum[si] / static_cast<std::int64_t>(count) : 10'000;

        // Update MAPI: Market Access Price Impact based on state infrastructure
        const auto infra_capacity = world.geography.state_infrastructure(state);
        world.geography.set_state_infrastructure_load(state, infra_load[si]);
        const auto load = std::max<std::uint32_t>(1u, infra_load[si]);
        const auto mapi = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(1'000'000u, (static_cast<std::uint64_t>(infra_capacity) * 1'000'000u) / load));
        world.geography.set_state_market_access_ppm(state, mapi);

        // Radicalism & Loyalist State Resistance Dynamics
        if (count > 0) {
            const auto radical_ratio_ppm = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(1'000'000u, (radical_sum[si] * 1'000'000u) / count));
            const auto loyalist_ratio_ppm = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(1'000'000u, (loyalist_sum[si] * 1'000'000u) / count));

            // Radicals push up state resistance (turmoil)
            if (radical_ratio_ppm > 50'000u) {
                const auto add_res = static_cast<std::int32_t>((radical_ratio_ppm * 5'000u) / 1'000'000u);
                world.geography.add_state_resistance_ppm(state, add_res);
            }
            // Loyalists pacify state resistance
            if (loyalist_ratio_ppm > 50'000u) {
                const auto sub_res = static_cast<std::int32_t>((loyalist_ratio_ppm * 3'000u) / 1'000'000u);
                world.geography.add_state_resistance_ppm(state, -sub_res);
            }
        }

        if (avg_sol >= 9'000 && !world.countries.is_in_default(owner)) {
            world.geography.add_state_resistance_ppm(state, -1'000);
        } else if (avg_sol < 6'000 || world.countries.is_in_default(owner)) {
            world.geography.add_state_resistance_ppm(state, 1'000);
        }
    }
}

void GrandStrategyStore::run_weekly_reference_tick() {
    for (auto& record : migration_flows_) if (record.weeks_remaining > 0u) --record.weeks_remaining;
    for (auto& record : colonys_) record.progress_ppm = std::min<std::uint32_t>(1'000'000u, record.progress_ppm + 500u);
    run_politics_weekly();
    run_diplomacy_weekly();
    run_warfare_weekly();
    run_naval_weekly();
}

void GrandStrategyStore::run_weekly_reference_tick(World& world,
                                                   bool include_legacy_technology_spread,
                                                   std::uint64_t weekly_tick) {
    // Resolve due flows in the world-aware tick. The previous implementation
    // only decremented the timer, so the normal ThunderEngine path never moved
    // population when a migration completed.
    update_migration_flows(world);
    for (auto& record : colonys_) {
        if (record.progress_ppm < 1'000'000u) {
            record.progress_ppm = std::min<std::uint32_t>(1'000'000u, record.progress_ppm + 500u);
            if (record.progress_ppm >= 1'000'000u && record.country.valid() && record.province.valid()) {
                // Colony completed: incorporate province into colonial power's domain
                world.geography.set_province_owner(record.province, record.country);
                const auto prov_state = world.geography.province_state(record.province);
                if (prov_state.valid() && !world.geography.state_owner(prov_state).valid()) {
                    world.geography.set_state_owner(prov_state, record.country);
                }
            }
        }
    }
    run_politics_weekly();
    run_institutions_weekly(world);
    if (include_legacy_technology_spread) run_tech_spread_weekly(world, weekly_tick);
    run_state_resistance_weekly(world);
    run_military_weekly(world);
    run_diplomacy_weekly();
    run_warfare_weekly(&world);
    run_naval_weekly();
}

} // namespace thunder
