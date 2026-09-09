#include "thunder/simulation/grand_strategy/GrandStrategyStore.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace thunder {
namespace {

template<class Id>
bool ref_ok(Id id, std::size_t size) noexcept {
    return !id.valid() || static_cast<std::size_t>(id.value()) < size;
}

[[maybe_unused]] void hash_record(Fnv1a64& h, const TechnologyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.progress_ppm);h.add(r.unlocked);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const LawRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.enacted);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const InstitutionRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.level);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const CompanyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.cash_milli);h.add(r.productivity_ppm);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TradeRouteRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.good.value());h.add(r.quantity_milli);h.add(r.level);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const OwnershipStakeRecord& r){h.add(r.owner_country.value());h.add(r.owner_company.value());h.add(r.building.value());h.add(r.share_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TreatyRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(static_cast<std::uint8_t>(r.kind));h.add(r.article_hash);h.add(r.active);h.add(r.treaty_key_hash);h.add(r.start_tick);h.add(r.expiry_tick);h.add(r.obligation_milli);h.add(r.is_multilateral);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TreatyArticleRecord& r){h.add(r.treaty.value());h.add(static_cast<std::uint8_t>(r.kind));h.add(r.article_hash);h.add(r.payload_hash);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TreatyParticipantRecord& r){h.add(r.treaty.value());h.add(r.country.value());h.add(r.role);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ArmyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.manpower);h.add(r.organization_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const NavyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.sailors);h.add(r.strength_ppm);h.add(r.design.value());h.add(static_cast<std::uint8_t>(r.mission));h.add(r.assigned_zone.value());}
[[maybe_unused]] void hash_record(Fnv1a64& h, const MigrationFlowRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.population);h.add(r.weeks_remaining);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const InterestGroupRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.clout_ppm);h.add(r.approval_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const PoliticalPartyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.support_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const PowerBlocRecord& r){h.add(r.leader.value());h.add(r.key_hash);h.add(r.cohesion_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticPlayRecord& r){h.add(r.initiator.value());h.add(r.target.value());h.add(static_cast<std::uint8_t>(r.phase));h.add(r.war_goal_hash);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const FrontRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(r.state.value());h.add(r.progress_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const BattleRecord& r){h.add(r.front.value());h.add(r.attackers);h.add(r.defenders);h.add(r.progress_milli);h.add(r.resolved);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ColonyRecord& r){h.add(r.country.value());h.add(r.province.value());h.add(r.progress_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ShipDesignRecord& r){h.add(r.country.value());h.add(r.hull_hash);h.add(r.modules_hash);h.add(r.combat_power_milli);h.add(r.build_cost_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const InvestmentPoolRecord& r){h.add(r.country.value());h.add(r.cash_milli);h.add(r.weekly_contribution_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticRelationRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(r.relation_milli);h.add(r.trust_ppm);h.add(r.tension_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticPlayStateRecord& r){h.add(r.play.value());h.add(r.weeks_in_phase);h.add(r.escalation_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const GovernmentRecord& r){h.add(r.country.value());h.add(r.legitimacy_ppm);h.add(r.stability_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const LawEnactmentRecord& r){h.add(r.country.value());h.add(r.law_hash);h.add(r.progress_ppm);h.add(r.support_ppm);h.add(r.active);h.add(r.passed);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const WarRecord& r){h.add(r.play.value());h.add(r.attacker.value());h.add(r.defender.value());h.add(r.war_score_milli);h.add(r.weeks);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ParliamentRecord& r){h.add(r.country.value());h.add(r.power_distribution_law_hash);h.add(r.elections_enabled);h.add(static_cast<std::uint8_t>(r.migration_policy));h.add(r.election_interval_weeks);h.add(r.weeks_to_next_election);h.add(r.total_seats);h.add(r.ruling_party_seats);h.add(r.opposition_seats);h.add(r.ruling_party_hash);h.add(r.pop_vote_weight_ppm);h.add(r.ig_clout_weight_ppm);}

[[maybe_unused]] void hash_record(Fnv1a64& h, const CulturalAcceptanceRecord& r){h.add(r.country.value());h.add(r.culture.value());h.add(static_cast<std::uint8_t>(r.level));}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticSwayRecord& r){h.add(r.play.value());h.add(r.sponsor.value());h.add(r.target_country.value());h.add(static_cast<std::uint8_t>(r.offer_type));h.add(r.payload_hash);h.add(r.accepted);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const WarGoalRecord& r){h.add(r.play.value());h.add(r.holder.value());h.add(r.target.value());h.add(static_cast<std::uint8_t>(r.goal_type));h.add(r.state_target.value());h.add(r.primary);h.add(r.enforced);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const CommanderRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(static_cast<std::uint8_t>(r.trait));h.add(r.skill_level);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const SeaZoneRecord& r){h.add(r.key_hash);h.add(r.controller.value());h.add(r.blockade_efficiency_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const NavalBattleRecord& r){h.add(r.zone.value());h.add(r.attacker_navy.value());h.add(r.defender_navy.value());h.add(r.progress_milli);h.add(r.resolved);}

template<class T>
[[maybe_unused]] void hash_vec(Fnv1a64& h, const std::vector<T>& values) {
    h.add(values.size());
    for (const auto& record : values) hash_record(h, record);
}



} // namespace

bool GrandStrategyStore::validate(std::size_t countries, std::size_t markets, std::size_t provinces,
                                   std::size_t states, std::size_t buildings, std::size_t goods) const noexcept {
    for (const auto& r : technologys_) if (!ref_ok(r.country, countries) || r.progress_ppm > 1'000'000u) return false;
    for (const auto& r : laws_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : institutions_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : companys_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : trade_routes_) if (!ref_ok(r.source, markets) || !ref_ok(r.destination, markets) || !ref_ok(r.good, goods)) return false;
    for (const auto& r : ownership_stakes_) if (!ref_ok(r.owner_country, countries) || !ref_ok(r.owner_company, companys_.size()) || !ref_ok(r.building, buildings) || r.share_ppm > 1'000'000u) return false;
    for (std::size_t ti = 0; ti < treatys_.size(); ++ti) {
        const auto& r = treatys_[ti];
        if (!ref_ok(r.first, countries) || !ref_ok(r.second, countries) ||
            !r.first.valid() || !r.second.valid() || r.first == r.second ||
            static_cast<std::uint8_t>(r.kind) > static_cast<std::uint8_t>(TreatyKind::PeaceTreaty))
            return false;
        if (r.expiry_tick!=0 && r.expiry_tick <= r.start_tick) return false;
        if (r.is_multilateral && treaty_participants(
                TreatyId{static_cast<TreatyId::rep_type>(ti)}).size() < 2u)
            return false;
    }
    for (const auto& r : treaty_articles_) {
        if (!ref_ok(r.treaty, treatys_.size()) ||
            static_cast<std::uint8_t>(r.kind) > static_cast<std::uint8_t>(TreatyKind::PeaceTreaty)) return false;
    }
    for (const auto& r : treaty_participants_) if (!ref_ok(r.treaty, treatys_.size()) || !ref_ok(r.country, countries)) return false;
    for (const auto& r : armys_) if (!ref_ok(r.country, countries) || !ref_ok(r.location, states) || r.organization_ppm > 1'000'000u) return false;
    for (const auto& r : navys_) {
        if (!ref_ok(r.country, countries) || !ref_ok(r.location, states) ||
            !ref_ok(r.design, ship_designs_.size()) ||
            static_cast<std::uint8_t>(r.mission) > static_cast<std::uint8_t>(NavalMission::FleetInterception) ||
            r.strength_ppm > 1'000'000u) return false;
        // A newly restored/constructed navy may be idle before a sea zone is
        // assigned.  Once a mission is selected it must point at a real zone;
        // an explicitly stored zone is always checked even for an idle fleet.
        if (r.assigned_zone.valid() && !ref_ok(r.assigned_zone, sea_zones_.size())) return false;
        if (r.mission != NavalMission::None && !r.assigned_zone.valid()) return false;
    }
    for (const auto& r : migration_flows_) if (!ref_ok(r.source, provinces) || !ref_ok(r.destination, provinces)) return false;
    for (const auto& r : interest_groups_) if (!ref_ok(r.country, countries) || r.clout_ppm > 1'000'000u) return false;
    for (const auto& r : political_partys_) if (!ref_ok(r.country, countries) || r.support_ppm > 1'000'000u) return false;
    for (const auto& power_bloc : power_blocs_) if (!ref_ok(power_bloc.leader, countries) || power_bloc.cohesion_ppm > 1'000'000u) return false;
    for (const auto& r : diplomatic_plays_) if (!ref_ok(r.initiator, countries) || !ref_ok(r.target, countries) ||
        static_cast<std::uint8_t>(r.phase) > static_cast<std::uint8_t>(DiplomaticPlayPhase::Resolved)) return false;
    for (const auto& r : fronts_) if (!ref_ok(r.first, countries) || !ref_ok(r.second, countries) || !ref_ok(r.state, states) || r.progress_milli < -100'000 || r.progress_milli > 100'000) return false;
    for (const auto& r : battles_) if (!ref_ok(r.front, fronts_.size()) || r.progress_milli < -100'000 || r.progress_milli > 100'000) return false;
    for (const auto& r : colonys_) if (!ref_ok(r.country, countries) || !ref_ok(r.province, provinces) || r.progress_ppm > 1'000'000u) return false;
    for (const auto& r : ship_designs_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : investment_pools_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : diplomatic_relations_) {
        if (!ref_ok(r.first, countries) || !ref_ok(r.second, countries) || r.first == r.second ||
            r.relation_milli < -100'000 || r.relation_milli > 100'000 || r.trust_ppm > 1'000'000u || r.tension_ppm > 1'000'000u) return false;
    }
    for (const auto& r : diplomatic_play_states_) if (!ref_ok(r.play, diplomatic_plays_.size()) || r.escalation_ppm > 1'000'000u) return false;
    for (const auto& r : governments_) if (!ref_ok(r.country, countries) || r.legitimacy_ppm > 1'000'000u || r.stability_milli < -100'000 || r.stability_milli > 100'000) return false;
    for (const auto& r : law_enactments_) if (!ref_ok(r.country, countries) || r.progress_ppm > 1'000'000u || r.support_ppm > 1'000'000u) return false;
    for (const auto& r : wars_) if (!ref_ok(r.play, diplomatic_plays_.size()) || !ref_ok(r.attacker, countries) || !ref_ok(r.defender, countries) || r.war_score_milli < -100'000 || r.war_score_milli > 100'000) return false;
    for (const auto& r : parliaments_) {
        if (!ref_ok(r.country, countries) || r.total_seats == 0u ||
            r.ruling_party_seats > r.total_seats || r.opposition_seats > r.total_seats ||
            static_cast<std::uint64_t>(r.ruling_party_seats) + r.opposition_seats != r.total_seats ||
            r.pop_vote_weight_ppm > 1'000'000u || r.ig_clout_weight_ppm > 1'000'000u ||
            static_cast<std::uint8_t>(r.migration_policy) > static_cast<std::uint8_t>(MigrationPolicy::OpenBorders)) return false;
    }
    for (const auto& r : cultural_acceptances_) if (!ref_ok(r.country, countries) ||
        static_cast<std::uint8_t>(r.level) > static_cast<std::uint8_t>(CulturalAcceptanceLevel::Primary)) return false;
    for (const auto& r : diplomatic_sways_) if (!ref_ok(r.play, diplomatic_plays_.size()) ||
        !ref_ok(r.sponsor, countries) || !ref_ok(r.target_country, countries) ||
        static_cast<std::uint8_t>(r.offer_type) > static_cast<std::uint8_t>(SwayOfferType::StateTransfer)) return false;
    for (const auto& r : war_goals_) {
        if (!ref_ok(r.play, diplomatic_plays_.size()) || !ref_ok(r.holder, countries) ||
            !ref_ok(r.target, countries) ||
            static_cast<std::uint8_t>(r.goal_type) > static_cast<std::uint8_t>(WarGoalType::WarReparations)) return false;
        // Only territorial conquest requires a state target.  Other goals
        // (reparations, market access, regime change) intentionally carry an
        // empty target, while any non-empty target is still range-checked.
        if (r.state_target.valid() && !ref_ok(r.state_target, states)) return false;
        if (r.goal_type == WarGoalType::ConquerState && !r.state_target.valid()) return false;
    }
    for (const auto& r : commanders_) if (!ref_ok(r.country, countries) || !ref_ok(r.location, states) ||
        static_cast<std::uint8_t>(r.trait) > static_cast<std::uint8_t>(CommanderTrait::AggressivePusher)) return false;
    for (const auto& r : sea_zones_) if ((r.controller.valid() && !ref_ok(r.controller, countries)) ||
        r.blockade_efficiency_ppm > 1'000'000u) return false;
    for (const auto& r : naval_battles_) if (!ref_ok(r.zone, sea_zones_.size()) ||
        !ref_ok(r.attacker_navy, navys_.size()) || !ref_ok(r.defender_navy, navys_.size()) ||
        r.progress_milli < -100'000 || r.progress_milli > 100'000) return false;
    return true;
}

std::uint64_t GrandStrategyStore::checksum() const noexcept {
    Fnv1a64 h;
    hash_vec(h, technologys_); hash_vec(h, laws_); hash_vec(h, institutions_); hash_vec(h, companys_);
    hash_vec(h, trade_routes_); hash_vec(h, ownership_stakes_); hash_vec(h, treatys_); hash_vec(h, treaty_articles_); hash_vec(h, treaty_participants_); hash_vec(h, armys_);
    hash_vec(h, navys_); hash_vec(h, migration_flows_); hash_vec(h, interest_groups_); hash_vec(h, political_partys_);
    hash_vec(h, power_blocs_); hash_vec(h, diplomatic_plays_); hash_vec(h, fronts_); hash_vec(h, battles_);
    hash_vec(h, colonys_); hash_vec(h, ship_designs_); hash_vec(h, investment_pools_); hash_vec(h, diplomatic_relations_);
    hash_vec(h, diplomatic_play_states_); hash_vec(h, governments_); hash_vec(h, law_enactments_); hash_vec(h, wars_);
    hash_vec(h, parliaments_); hash_vec(h, cultural_acceptances_);
    hash_vec(h, diplomatic_sways_); hash_vec(h, war_goals_); hash_vec(h, commanders_);
    hash_vec(h, sea_zones_); hash_vec(h, naval_battles_);
    return h.value();
}

std::size_t GrandStrategyStore::memory_bytes() const noexcept {
    return technologys_.capacity()*sizeof(TechnologyRecord)+laws_.capacity()*sizeof(LawRecord)+
        institutions_.capacity()*sizeof(InstitutionRecord)+companys_.capacity()*sizeof(CompanyRecord)+
        trade_routes_.capacity()*sizeof(TradeRouteRecord)+ownership_stakes_.capacity()*sizeof(OwnershipStakeRecord)+
        treatys_.capacity()*sizeof(TreatyRecord)+treaty_articles_.capacity()*sizeof(TreatyArticleRecord)+treaty_participants_.capacity()*sizeof(TreatyParticipantRecord)+armys_.capacity()*sizeof(ArmyRecord)+navys_.capacity()*sizeof(NavyRecord)+
        migration_flows_.capacity()*sizeof(MigrationFlowRecord)+interest_groups_.capacity()*sizeof(InterestGroupRecord)+
        political_partys_.capacity()*sizeof(PoliticalPartyRecord)+power_blocs_.capacity()*sizeof(PowerBlocRecord)+
        diplomatic_plays_.capacity()*sizeof(DiplomaticPlayRecord)+fronts_.capacity()*sizeof(FrontRecord)+
        battles_.capacity()*sizeof(BattleRecord)+colonys_.capacity()*sizeof(ColonyRecord)+ship_designs_.capacity()*sizeof(ShipDesignRecord)+
        investment_pools_.capacity()*sizeof(InvestmentPoolRecord)+diplomatic_relations_.capacity()*sizeof(DiplomaticRelationRecord)+
        diplomatic_play_states_.capacity()*sizeof(DiplomaticPlayStateRecord)+governments_.capacity()*sizeof(GovernmentRecord)+
        law_enactments_.capacity()*sizeof(LawEnactmentRecord)+wars_.capacity()*sizeof(WarRecord)+
        parliaments_.capacity()*sizeof(ParliamentRecord)+cultural_acceptances_.capacity()*sizeof(CulturalAcceptanceRecord)+
        diplomatic_sways_.capacity()*sizeof(DiplomaticSwayRecord)+war_goals_.capacity()*sizeof(WarGoalRecord)+
        commanders_.capacity()*sizeof(CommanderRecord)+
        sea_zones_.capacity()*sizeof(SeaZoneRecord)+naval_battles_.capacity()*sizeof(NavalBattleRecord);
}

} // namespace thunder
