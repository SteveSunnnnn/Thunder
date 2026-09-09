#include "thunder/simulation/grand_strategy/GrandStrategyStore.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace thunder {
namespace {

template<class Id, class T>
Id push_id(std::vector<T>& values, T record) {
    if (values.size() >= static_cast<std::size_t>(std::numeric_limits<typename Id::rep_type>::max())) {
        throw std::overflow_error("grand-strategy store id overflow");
    }
    const auto id = Id{static_cast<typename Id::rep_type>(values.size())};
    values.push_back(std::move(record));
    return id;
}

std::pair<CountryId, CountryId> canonical_pair(CountryId a, CountryId b) noexcept {
    return a.value() <= b.value() ? std::pair{a, b} : std::pair{b, a};
}

std::int32_t clamp_relation(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, -100'000, 100'000));
}

std::uint32_t clamp_ppm(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, 1'000'000u));
}

} // namespace

TechnologyId GrandStrategyStore::add_technology(TechnologyRecord r){return push_id<TechnologyId>(technologys_,r);}
LawId GrandStrategyStore::add_law(LawRecord r){return push_id<LawId>(laws_,r);}
InstitutionId GrandStrategyStore::add_institution(InstitutionRecord r){return push_id<InstitutionId>(institutions_,r);}
CompanyId GrandStrategyStore::add_company(CompanyRecord r){return push_id<CompanyId>(companys_,r);}
TradeRouteId GrandStrategyStore::add_trade_route(TradeRouteRecord r){return push_id<TradeRouteId>(trade_routes_,r);}
StrongId<struct OwnershipStakeTag> GrandStrategyStore::add_ownership_stake(OwnershipStakeRecord r){return push_id<StrongId<struct OwnershipStakeTag>>(ownership_stakes_,r);}
TreatyId GrandStrategyStore::add_treaty(TreatyRecord r){return push_id<TreatyId>(treatys_,r);}
TreatyArticleId GrandStrategyStore::add_treaty_article(TreatyArticleRecord r){return push_id<TreatyArticleId>(treaty_articles_,r);}
TreatyParticipantId GrandStrategyStore::add_treaty_participant(TreatyParticipantRecord r){return push_id<TreatyParticipantId>(treaty_participants_,r);}
ArmyId GrandStrategyStore::add_army(ArmyRecord r){return push_id<ArmyId>(armys_,r);}
NavyId GrandStrategyStore::add_navy(NavyRecord r){return push_id<NavyId>(navys_,r);}
MigrationFlowId GrandStrategyStore::add_migration_flow(MigrationFlowRecord r){return push_id<MigrationFlowId>(migration_flows_,r);}
InterestGroupId GrandStrategyStore::add_interest_group(InterestGroupRecord r){return push_id<InterestGroupId>(interest_groups_,r);}
PoliticalPartyId GrandStrategyStore::add_political_party(PoliticalPartyRecord r){return push_id<PoliticalPartyId>(political_partys_,r);}
PowerBlocId GrandStrategyStore::add_power_bloc(PowerBlocRecord r){return push_id<PowerBlocId>(power_blocs_,r);}
DiplomaticPlayId GrandStrategyStore::add_diplomatic_play(DiplomaticPlayRecord r){return push_id<DiplomaticPlayId>(diplomatic_plays_,r);}
FrontId GrandStrategyStore::add_front(FrontRecord r){return push_id<FrontId>(fronts_,r);}
BattleId GrandStrategyStore::add_battle(BattleRecord r){return push_id<BattleId>(battles_,r);}
ColonyId GrandStrategyStore::add_colony(ColonyRecord r){return push_id<ColonyId>(colonys_,r);}
ShipDesignId GrandStrategyStore::add_ship_design(ShipDesignRecord r){return push_id<ShipDesignId>(ship_designs_,r);}
InvestmentPoolId GrandStrategyStore::add_investment_pool(InvestmentPoolRecord r){return push_id<InvestmentPoolId>(investment_pools_,r);}
DiplomaticRelationId GrandStrategyStore::add_diplomatic_relation(DiplomaticRelationRecord r){return push_id<DiplomaticRelationId>(diplomatic_relations_,r);}
DiplomaticPlayStateId GrandStrategyStore::add_diplomatic_play_state(DiplomaticPlayStateRecord r){return push_id<DiplomaticPlayStateId>(diplomatic_play_states_,r);}
GovernmentId GrandStrategyStore::add_government(GovernmentRecord r){return push_id<GovernmentId>(governments_,r);}
LawEnactmentId GrandStrategyStore::add_law_enactment(LawEnactmentRecord r){return push_id<LawEnactmentId>(law_enactments_,r);}
WarId GrandStrategyStore::add_war(WarRecord r){return push_id<WarId>(wars_,r);}
ParliamentId GrandStrategyStore::add_parliament(ParliamentRecord r){return push_id<ParliamentId>(parliaments_,r);}
CulturalAcceptanceId GrandStrategyStore::add_cultural_acceptance(CulturalAcceptanceRecord r){return push_id<CulturalAcceptanceId>(cultural_acceptances_,r);}
DiplomaticSwayId GrandStrategyStore::add_diplomatic_sway(DiplomaticSwayRecord r){return push_id<DiplomaticSwayId>(diplomatic_sways_,r);}
WarGoalId GrandStrategyStore::add_war_goal(WarGoalRecord r){return push_id<WarGoalId>(war_goals_,r);}
CommanderId GrandStrategyStore::add_commander(CommanderRecord r){return push_id<CommanderId>(commanders_,r);}
SeaZoneId GrandStrategyStore::add_sea_zone(SeaZoneRecord r){return push_id<SeaZoneId>(sea_zones_,r);}
NavalBattleId GrandStrategyStore::add_naval_battle(NavalBattleRecord r){return push_id<NavalBattleId>(naval_battles_,r);}




DiplomaticRelationId GrandStrategyStore::ensure_diplomatic_relation(CountryId first, CountryId second) {
    if (!first.valid() || !second.valid() || first == second) throw std::invalid_argument("diplomatic relation requires two countries");
    const auto [a, b] = canonical_pair(first, second);
    for (std::size_t i = 0; i < diplomatic_relations_.size(); ++i) {
        const auto& record = diplomatic_relations_[i];
        if (record.first == a && record.second == b) return DiplomaticRelationId{static_cast<DiplomaticRelationId::rep_type>(i)};
    }
    return add_diplomatic_relation({a, b, 0, 500'000u, 0u});
}

std::int32_t GrandStrategyStore::relation_milli(CountryId first, CountryId second) const noexcept {
    if (!first.valid() || !second.valid() || first == second) return 0;
    const auto [a, b] = canonical_pair(first, second);
    for (const auto& record : diplomatic_relations_) {
        if (record.first == a && record.second == b) return record.relation_milli;
    }
    return 0;
}

void GrandStrategyStore::adjust_relation(CountryId first, CountryId second, std::int32_t delta_milli) {
    const auto id = ensure_diplomatic_relation(first, second);
    auto& record = diplomatic_relations_[id.value()];
    record.relation_milli = clamp_relation(static_cast<std::int64_t>(record.relation_milli) + delta_milli);
}

bool GrandStrategyStore::has_active_treaty(CountryId first, CountryId second, TreatyKind kind) const noexcept {
    if (!first.valid() || !second.valid() || first == second) return false;
    const auto [a, b] = canonical_pair(first, second);
    for (const auto& treaty : treatys_) {
        if (!treaty.active) continue;
        if (treaty.kind != kind) continue;
        const auto [ta, tb] = canonical_pair(treaty.first, treaty.second);
        if (ta == a && tb == b) return true;
        // Multilateral: check participants
        if (treaty.is_multilateral) {
            bool has_first = false, has_second = false;
            const auto tid = TreatyId{static_cast<TreatyId::rep_type>(&treaty - treatys_.data())};
            for (const auto& p : treaty_participants_) {
                if (p.treaty == tid) {
                    if (p.country == first) has_first = true;
                    if (p.country == second) has_second = true;
                }
            }
            if (has_first && has_second) return true;
        }
    }
    // Check articles as well (treaty may be Custom with multiple articles)
    for (const auto& art : treaty_articles_) if (art.active && art.kind==kind) {
        auto tid = art.treaty;
        if (!tid.valid() || static_cast<std::size_t>(tid.value())>=treatys_.size()) continue;
        const auto& tr = treatys_[tid.value()];
        if (!tr.active) continue;
        const auto [ta,tb]=canonical_pair(tr.first,tr.second);
        if ((ta==a&&tb==b)) return true;
    }
    return false;
}
bool GrandStrategyStore::has_active_treaty_article(TreatyId treaty, TreatyKind kind) const noexcept {
    if (!treaty.valid()|| static_cast<std::size_t>(treaty.value())>=treatys_.size() || !treatys_[treaty.value()].active) return false;
    for (const auto& art: treaty_articles_) if (art.active && art.treaty==treaty && art.kind==kind) return true;
    return treatys_[treaty.value()].kind==kind;
}

TreatyId GrandStrategyStore::create_treaty(CountryId first, CountryId second, TreatyKind kind,
                                             std::uint64_t article_hash) {
    return create_treaty_ex(first, second, kind, article_hash, 0, 0, 0, article_hash);
}
TreatyId GrandStrategyStore::create_treaty_ex(CountryId first, CountryId second, TreatyKind kind,
                                             std::uint64_t article_hash, std::uint64_t start_tick,
                                             std::uint64_t expiry_tick, std::int32_t obligation_milli,
                                             std::uint64_t treaty_key_hash) {
    if (!first.valid() || !second.valid() || first == second) throw std::invalid_argument("treaty requires two countries");
    if (expiry_tick!=0 && expiry_tick <= start_tick) throw std::invalid_argument("treaty expiry must be > start");
    const auto [a, b] = canonical_pair(first, second);
    for (std::size_t i = 0; i < treatys_.size(); ++i) {
        auto& treaty = treatys_[i];
        const auto [ta, tb] = canonical_pair(treaty.first, treaty.second);
        if (treaty.kind == kind && ta == a && tb == b) {
            treaty.active = true;
            treaty.article_hash = article_hash;
            treaty.start_tick = start_tick;
            treaty.expiry_tick = expiry_tick;
            treaty.obligation_milli = obligation_milli;
            if (treaty_key_hash) treaty.treaty_key_hash = treaty_key_hash;
            adjust_relation(a, b, 5'000);
            return TreatyId{static_cast<TreatyId::rep_type>(i)};
        }
    }
    TreatyRecord rec{};
    rec.first=a; rec.second=b; rec.kind=kind; rec.article_hash=article_hash; rec.active=true;
    rec.start_tick=start_tick; rec.expiry_tick=expiry_tick; rec.obligation_milli=obligation_milli;
    rec.treaty_key_hash = treaty_key_hash ? treaty_key_hash : article_hash;
    const auto id = add_treaty(rec);
    adjust_relation(a, b, 5'000);
    return id;
}
TreatyArticleId GrandStrategyStore::add_treaty_article(TreatyId treaty, TreatyKind kind, std::uint64_t article_hash, std::uint64_t payload_hash) {
    if (!treaty.valid()|| static_cast<std::size_t>(treaty.value())>=treatys_.size()) throw std::invalid_argument("invalid treaty for article");
    TreatyArticleRecord r{}; r.treaty=treaty; r.kind=kind; r.article_hash=article_hash; r.payload_hash=payload_hash; r.active=true;
    return add_treaty_article(r);
}
TreatyParticipantId GrandStrategyStore::add_treaty_participant(TreatyId treaty, CountryId country, std::uint8_t role) {
    if (!treaty.valid()|| static_cast<std::size_t>(treaty.value())>=treatys_.size()) throw std::invalid_argument("invalid treaty for participant");
    if (!country.valid()) throw std::invalid_argument("invalid participant country");
    // Deduplicate
    for (auto &p: treaty_participants_) if (p.treaty==treaty && p.country==country) { p.role=role; return TreatyParticipantId{static_cast<TreatyParticipantId::rep_type>(&p - treaty_participants_.data())}; }
    TreatyParticipantRecord rec{}; rec.treaty=treaty; rec.country=country; rec.role=role;
    auto id = add_treaty_participant(rec);
    treatys_[treaty.value()].is_multilateral = true;
    return id;
}
std::span<const TreatyArticleRecord> GrandStrategyStore::treaty_articles(TreatyId treaty) const noexcept {
    treaty_articles_scratch_.clear();
    for (auto &a: treaty_articles_) if (a.treaty==treaty) treaty_articles_scratch_.push_back(a);
    return treaty_articles_scratch_;
}
std::span<const TreatyParticipantRecord> GrandStrategyStore::treaty_participants(TreatyId treaty) const noexcept {
    treaty_participants_scratch_.clear();
    for (auto &p: treaty_participants_) if (p.treaty==treaty) treaty_participants_scratch_.push_back(p);
    return treaty_participants_scratch_;
}
void GrandStrategyStore::expire_treaties(std::uint64_t current_tick) {
    for (auto &tr: treatys_) if (tr.active && tr.expiry_tick!=0 && current_tick >= tr.expiry_tick) {
        tr.active=false;
        for (auto &art: treaty_articles_) if (art.treaty==TreatyId{static_cast<TreatyId::rep_type>(&tr - treatys_.data())}) art.active=false;
        adjust_relation(tr.first, tr.second, -2'000);
    }
}

bool GrandStrategyStore::break_treaty(TreatyId treaty) {
    if (!treaty.valid() || static_cast<std::size_t>(treaty.value()) >= treatys_.size()) return false;
    auto& record = treatys_[treaty.value()];
    if (!record.active) return false;
    record.active = false;
    for (auto &art: treaty_articles_) if (art.treaty==treaty) art.active=false;
    adjust_relation(record.first, record.second, -5'000);
    return true;
}

void GrandStrategyStore::add_investment_pool_funds(CountryId country, EconomyAmount amount_milli) {    if (amount_milli <= 0 || !country.valid()) return;
    for (auto& pool : investment_pools_) {
        if (pool.country == country) {
            pool.cash_milli = saturating_add(pool.cash_milli, amount_milli);
            return;
        }
    }
    investment_pools_.push_back({country, amount_milli, 0});
}

void GrandStrategyStore::add_investment_pool_funds(InvestmentPoolId pool, EconomyAmount amount_milli) {
    if (amount_milli <= 0 || !pool.valid()) return;
    const auto index = static_cast<std::size_t>(pool.value());
    if (index >= investment_pools_.size()) return;
    investment_pools_[index].cash_milli = saturating_add(
        investment_pools_[index].cash_milli, amount_milli);
}

EconomyAmount GrandStrategyStore::withdraw_investment_pool_funds(CountryId country, EconomyAmount amount_milli) {
    if (amount_milli <= 0 || !country.valid()) return 0;
    for (auto& pool : investment_pools_) {
        if (pool.country == country) {
            const auto taken = std::min(amount_milli, pool.cash_milli);
            pool.cash_milli -= taken;
            return taken;
        }
    }
    return 0;
}

EconomyAmount GrandStrategyStore::investment_pool_cash(CountryId country) const noexcept {
    if (!country.valid()) return 0;
    for (const auto& pool : investment_pools_) {
        if (pool.country == country) return pool.cash_milli;
    }
    return 0;
}

EconomyAmount GrandStrategyStore::company_cash(CompanyId company) const noexcept {
    if (!company.valid()) return 0;
    const auto index = static_cast<std::size_t>(company.value());
    return index < companys_.size() ? companys_[index].cash_milli : 0;
}

void GrandStrategyStore::add_company_cash(CompanyId company, EconomyAmount delta_milli) noexcept {
    if (!company.valid()) return;
    const auto index = static_cast<std::size_t>(company.value());
    if (index >= companys_.size()) return;
    companys_[index].cash_milli = saturating_add(companys_[index].cash_milli, delta_milli);
}

DiplomaticPlayId GrandStrategyStore::start_diplomatic_play(CountryId initiator, CountryId target,
                                                           std::uint64_t war_goal_hash) {
    if (!initiator.valid() || !target.valid() || initiator == target) throw std::invalid_argument("diplomatic play requires two countries");
    for (std::size_t i = 0; i < diplomatic_plays_.size(); ++i) {
        const auto& play = diplomatic_plays_[i];
        const bool same_pair = (play.initiator == initiator && play.target == target) ||
                               (play.initiator == target && play.target == initiator);
        if (same_pair && play.phase != DiplomaticPlayPhase::Resolved) return DiplomaticPlayId{static_cast<DiplomaticPlayId::rep_type>(i)};
    }
    const auto play = add_diplomatic_play({initiator, target, DiplomaticPlayPhase::Opening, war_goal_hash});
    (void)add_diplomatic_play_state({play, 0u, 0u});
    const auto relation = ensure_diplomatic_relation(initiator, target);
    auto& rel = diplomatic_relations_[relation.value()];
    rel.relation_milli = clamp_relation(static_cast<std::int64_t>(rel.relation_milli) - 20'000);
    rel.tension_ppm = clamp_ppm(static_cast<std::uint64_t>(rel.tension_ppm) + 50'000u);
    return play;
}

bool GrandStrategyStore::back_down(DiplomaticPlayId play_id, CountryId country) {
    if (!play_id.valid() || static_cast<std::size_t>(play_id.value()) >= diplomatic_plays_.size()) return false;
    auto& play = diplomatic_plays_[play_id.value()];
    if (play.phase == DiplomaticPlayPhase::War || play.phase == DiplomaticPlayPhase::Resolved) return false;
    if (country != play.initiator && country != play.target) return false;
    play.phase = DiplomaticPlayPhase::Resolved;
    adjust_relation(play.initiator, play.target, -2'500);
    return true;
}

FrontId GrandStrategyStore::create_front_for_play(DiplomaticPlayId play_id, StateId state) {
    if (!play_id.valid() || static_cast<std::size_t>(play_id.value()) >= diplomatic_plays_.size()) {
        throw std::invalid_argument("invalid diplomatic play for front");
    }
    const auto& play = diplomatic_plays_[play_id.value()];
    for (std::size_t i = 0; i < fronts_.size(); ++i) {
        const auto& front = fronts_[i];
        if (front.first == play.initiator && front.second == play.target && front.state == state) {
            return FrontId{static_cast<FrontId::rep_type>(i)};
        }
    }
    return add_front({play.initiator, play.target, state, 0});
}

DiplomaticSwayId GrandStrategyStore::sway_nation(DiplomaticPlayId play, CountryId sponsor, CountryId target_country, SwayOfferType offer, std::uint64_t payload_hash) {
    if (!play.valid() || !sponsor.valid() || !target_country.valid() || sponsor == target_country) return DiplomaticSwayId{};
    return add_diplomatic_sway({play, sponsor, target_country, offer, payload_hash, false});
}

bool GrandStrategyStore::accept_sway(DiplomaticSwayId sway) {
    if (!sway.valid() || static_cast<std::size_t>(sway.value()) >= diplomatic_sways_.size()) return false;
    auto& record = diplomatic_sways_[sway.value()];
    record.accepted = true;
    const auto rel_id = ensure_diplomatic_relation(record.sponsor, record.target_country);
    auto& rel = diplomatic_relations_[rel_id.value()];
    rel.relation_milli = clamp_relation(static_cast<std::int64_t>(rel.relation_milli) + 20'000);
    rel.trust_ppm = clamp_ppm(static_cast<std::uint64_t>(rel.trust_ppm) + 150'000u);
    return true;
}

bool GrandStrategyStore::back_down_diplomatic_play(DiplomaticPlayId play, CountryId backer) {
    if (!play.valid() || static_cast<std::size_t>(play.value()) >= diplomatic_plays_.size()) return false;
    auto& p = diplomatic_plays_[play.value()];
    if (p.phase == DiplomaticPlayPhase::Resolved) return false;
    if (backer != p.initiator && backer != p.target) return false;

    const auto winner = (backer == p.initiator) ? p.target : p.initiator;
    const auto loser = backer;
    enforce_peace_treaty(play, winner, loser);
    p.phase = DiplomaticPlayPhase::Resolved;
    return true;
}

void GrandStrategyStore::enforce_peace_treaty(DiplomaticPlayId play, CountryId winner, CountryId loser, World* world) {
    for (auto& goal : war_goals_) {
        if (goal.play == play && goal.holder == winner && goal.target == loser) {
            goal.enforced = true;
            // Territorial conquest logic closed-loop:
            if (goal.goal_type == WarGoalType::ConquerState && goal.state_target.valid() && world != nullptr) {
                // 1. Transfer state ownership to winner
                world->geography.set_state_owner(goal.state_target, winner);
                // 2. Transfer all province ownership in the state to winner
                const auto prov_states = world->geography.province_states();
                for (std::size_t pi = 0; pi < prov_states.size(); ++pi) {
                    if (prov_states[pi] == goal.state_target) {
                        world->geography.set_province_owner(ProvinceId{static_cast<ProvinceId::rep_type>(pi)}, winner);
                    }
                }
                // 3. Set occupation resistance in conquered state
                world->geography.set_state_resistance_ppm(goal.state_target, 250'000u);
            }
        }
    }
    (void)create_treaty(winner, loser, TreatyKind::PeaceTreaty, 0x5045414345u);
    const auto rel_id = ensure_diplomatic_relation(winner, loser);
    auto& rel = diplomatic_relations_[rel_id.value()];
    rel.tension_ppm = 0u;
}

void GrandStrategyStore::assign_navy_mission(NavyId navy, NavalMission mission, SeaZoneId zone) {
    if (!navy.valid() || static_cast<std::size_t>(navy.value()) >= navys_.size()) return;
    navys_[navy.value()].mission = mission;
    navys_[navy.value()].assigned_zone = zone;
}

std::uint32_t GrandStrategyStore::sea_zone_blockade_level(SeaZoneId zone) const noexcept {
    if (!zone.valid() || static_cast<std::size_t>(zone.value()) >= sea_zones_.size()) return 0u;
    return sea_zones_[zone.value()].blockade_efficiency_ppm;
}

void GrandStrategyStore::run_naval_weekly() {
    for (std::size_t zi = 0; zi < sea_zones_.size(); ++zi) {
        auto& zone = sea_zones_[zi];
        std::uint64_t blockade_power = 0u;
        for (const auto& navy : navys_) {
            if (navy.assigned_zone.value() == zi && navy.mission == NavalMission::BlockadePort) {
                blockade_power += (static_cast<std::uint64_t>(navy.sailors) * static_cast<std::uint64_t>(navy.strength_ppm)) / 1'000'000u;
            }
        }
        zone.blockade_efficiency_ppm = static_cast<std::uint32_t>(std::min<std::uint64_t>(1'000'000u, blockade_power * 100u));
    }
}



GovernmentId GrandStrategyStore::ensure_government(CountryId country) {
    for (std::size_t i = 0; i < governments_.size(); ++i) {
        if (governments_[i].country == country) return GovernmentId{static_cast<GovernmentId::rep_type>(i)};
    }
    return add_government({country, 500'000u, 0});
}

ParliamentId GrandStrategyStore::ensure_parliament(CountryId country) {
    for (std::size_t i = 0; i < parliaments_.size(); ++i) {
        if (parliaments_[i].country == country) return ParliamentId{static_cast<ParliamentId::rep_type>(i)};
    }
    return add_parliament({country, 0u, false, 208u, 208u, 100u, 100u, 0u, 0u, 1'000'000u, 0u, MigrationPolicy::OpenBorders});
}

void GrandStrategyStore::configure_parliament_rules(CountryId country, std::uint64_t law_hash, bool elections_enabled,
                                                    std::uint32_t pop_weight_ppm, std::uint32_t ig_clout_ppm,
                                                    std::uint16_t interval_weeks) {
    const auto pid = ensure_parliament(country);
    auto& parl = parliaments_[pid.value()];
    parl.power_distribution_law_hash = law_hash;
    parl.elections_enabled = elections_enabled;
    parl.pop_vote_weight_ppm = pop_weight_ppm;
    parl.ig_clout_weight_ppm = ig_clout_ppm;
    parl.election_interval_weeks = interval_weeks;
    if (parl.weeks_to_next_election > interval_weeks) {
        parl.weeks_to_next_election = interval_weeks;
    }
}

bool GrandStrategyStore::elections_enabled(CountryId country) const noexcept {
    const auto* p = parliament_for(country);
    return p ? p->elections_enabled : false;
}

void GrandStrategyStore::set_migration_policy(CountryId country, MigrationPolicy policy) {
    const auto pid = ensure_parliament(country);
    parliaments_[pid.value()].migration_policy = policy;
}

MigrationPolicy GrandStrategyStore::migration_policy(CountryId country) const noexcept {
    const auto* p = parliament_for(country);
    return p ? p->migration_policy : MigrationPolicy::OpenBorders;
}

const ParliamentRecord* GrandStrategyStore::parliament_for(CountryId country) const noexcept {
    for (const auto& p : parliaments_) {
        if (p.country == country) return &p;
    }
    return nullptr;
}

ParliamentRecord* GrandStrategyStore::parliament_for_mut(CountryId country) noexcept {
    for (auto& p : parliaments_) {
        if (p.country == country) return &p;
    }
    return nullptr;
}

void GrandStrategyStore::run_parliamentary_election(CountryId country) {
    const auto pid = ensure_parliament(country);
    auto& parl = parliaments_[pid.value()];
    if (!parl.elections_enabled) {
        parl.ruling_party_seats = parl.total_seats;
        parl.opposition_seats = 0;
        parl.weeks_to_next_election = parl.election_interval_weeks;
        return;
    }

    std::uint64_t total_weighted_votes = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> party_votes;

    for (const auto& party : political_partys_) {
        if (party.country != country) continue;

        // Dynamic weighted votes: POP votes weighted by enacted law + Interest Group clout weighted by enacted law
        const std::uint64_t pop_part = (static_cast<std::uint64_t>(party.support_ppm) * parl.pop_vote_weight_ppm) / 1'000'000u;

        std::uint64_t ig_clout_sum = 0;
        for (const auto& ig : interest_groups_) {
            if (ig.country == country && ig.key_hash == party.key_hash) {
                ig_clout_sum += ig.clout_ppm;
            }
        }
        const std::uint64_t ig_part = (ig_clout_sum * parl.ig_clout_weight_ppm) / 1'000'000u;

        const std::uint64_t votes = std::max<std::uint64_t>(1u, pop_part + ig_part);
        total_weighted_votes += votes;
        party_votes.emplace_back(party.key_hash, votes);
    }

    if (party_votes.empty() || total_weighted_votes == 0u) {
        parl.ruling_party_seats = parl.total_seats;
        parl.opposition_seats = 0;
    } else {
        std::sort(party_votes.begin(), party_votes.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        const auto ruling_seats = static_cast<std::uint32_t>((party_votes[0].second * parl.total_seats) / total_weighted_votes);
        parl.ruling_party_seats = std::max<std::uint32_t>(1u, ruling_seats);
        parl.opposition_seats = parl.total_seats - parl.ruling_party_seats;
        parl.ruling_party_hash = party_votes[0].first;
    }

    parl.weeks_to_next_election = parl.election_interval_weeks;

    const auto gov_id = ensure_government(country);
    auto& gov = governments_[gov_id.value()];
    gov.legitimacy_ppm = clamp_ppm((static_cast<std::uint64_t>(parl.ruling_party_seats) * 1'000'000u) / parl.total_seats);
}


void GrandStrategyStore::set_cultural_acceptance(CountryId country, CultureId culture, CulturalAcceptanceLevel level) {
    if (!country.valid() || !culture.valid()) return;
    for (auto& rec : cultural_acceptances_) {
        if (rec.country == country && rec.culture == culture) {
            rec.level = level;
            return;
        }
    }
    (void)add_cultural_acceptance({country, culture, level});
}

CulturalAcceptanceLevel GrandStrategyStore::cultural_acceptance(CountryId country, CultureId culture) const noexcept {
    if (!country.valid() || !culture.valid()) return CulturalAcceptanceLevel::Accepted;
    for (const auto& rec : cultural_acceptances_) {
        if (rec.country == country && rec.culture == culture) return rec.level;
    }
    return CulturalAcceptanceLevel::Accepted;
}

bool GrandStrategyStore::is_culture_accepted(CountryId country, CultureId culture) const noexcept {
    return cultural_acceptance(country, culture) >= CulturalAcceptanceLevel::Accepted;
}

ProvinceAttraction GrandStrategyStore::calculate_province_attraction(const World& world, ProvinceId province, CultureId culture) const noexcept {
    ProvinceAttraction result{};
    result.province = province;
    if (!province.valid() || static_cast<std::size_t>(province.value()) >= world.geography.province_count()) {
        return result;
    }

    const auto owner_country = world.geography.province_owner(province);
    const auto policy = owner_country.valid() ? migration_policy(owner_country) : MigrationPolicy::OpenBorders;

    if (owner_country.valid() && culture.valid()) {
        const auto acceptance = cultural_acceptance(owner_country, culture);
        if (acceptance == CulturalAcceptanceLevel::Discriminated) {
            result.discrimination_penalty = 50;
        }
    }

    if (policy == MigrationPolicy::OpenBorders) {
        result.policy_bonus = 50;
    }

    std::uint64_t total_capacity = 0;
    std::uint64_t total_employed = 0;
    std::uint64_t weighted_wage_sum = 0;
    const auto building_provinces = world.buildings.provinces();
    const auto building_levels = world.buildings.levels();
    const auto building_employees = world.buildings.employees_all();
    const auto wage_offers = world.buildings.wage_offers();

    for (std::size_t bi = 0; bi < world.buildings.size(); ++bi) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi))) continue;
        if (bi < building_provinces.size() && building_provinces[bi] == province) {
            const auto cap = static_cast<std::uint64_t>(building_levels[bi]) * 1000u;
            const auto emp = static_cast<std::uint64_t>(building_employees[bi]);
            total_capacity += cap;
            total_employed += emp;
            weighted_wage_sum += static_cast<std::uint64_t>(wage_offers[bi]) * std::max<std::uint64_t>(1u, cap);
        }
    }

    const auto vacancies = total_capacity > total_employed ? static_cast<std::uint32_t>(total_capacity - total_employed) : 0u;
    const auto avg_wage = total_capacity > 0 ? static_cast<EconomyPrice>(weighted_wage_sum / total_capacity) : 100;
    result.available_jobs = vacancies;
    result.avg_wage = avg_wage;

    std::uint64_t total_pop = 0;
    std::uint64_t unemployed_pop = 0;
    const auto pop_provinces = world.pops.provinces();
    const auto pop_populations = world.pops.populations();
    const auto pop_employed = world.pops.employed_all();

    for (std::size_t pi = 0; pi < world.pops.size(); ++pi) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
        if (pi < pop_provinces.size() && pop_provinces[pi] == province) {
            const auto p = pop_populations[pi];
            const auto e = pop_employed[pi];
            total_pop += p;
            if (p > e) unemployed_pop += (p - e);
        }
    }
    result.unemployed = static_cast<std::uint32_t>(unemployed_pop);
    result.arable_land_slots = total_pop < 50'000u ? static_cast<std::uint32_t>(50'000u - total_pop) : 0u;

    std::int64_t raw_score = 100;
    raw_score += static_cast<std::int64_t>(vacancies) / 10;
    raw_score += (static_cast<std::int64_t>(avg_wage) - 100) / 10;
    raw_score += static_cast<std::int64_t>(result.arable_land_slots) / 500;
    raw_score -= static_cast<std::int64_t>(unemployed_pop) / 50;
    raw_score -= result.discrimination_penalty;
    raw_score += result.policy_bonus;

    result.score = static_cast<std::int32_t>(std::clamp<std::int64_t>(raw_score, 0, 100'000));
    return result;
}

MigrationFlowId GrandStrategyStore::start_migration_flow(ProvinceId source, ProvinceId destination,
                                                        PopulationCount population, std::uint16_t weeks) {
    return add_migration_flow({source, destination, population, weeks});
}

MigrationFlowId GrandStrategyStore::start_mass_migration(ProvinceId source, ProvinceId destination,
                                                       PopulationCount population, std::uint16_t weeks) {
    return start_migration_flow(source, destination, population, weeks);
}

void GrandStrategyStore::update_migration_flows(World& world) {
    bool has_completed_flows = false;
    for (auto& flow : migration_flows_) {
        if (flow.population == 0u) continue;
        if (flow.weeks_remaining > 0u) --flow.weeks_remaining;
        if (flow.weeks_remaining == 0u) has_completed_flows = true;
    }
    if (!has_completed_flows) return;

    // Single-pass O(N_pops) index of the first living POP per province
    const auto prov_count = world.geography.province_count();
    std::vector<PopId> prov_first_pop(prov_count, PopId{});
    const auto pop_provinces = world.pops.provinces();
    for (std::size_t pi = 0; pi < world.pops.size(); ++pi) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
        if (pi < pop_provinces.size() && pop_provinces[pi].valid()) {
            const auto pv = pop_provinces[pi].value();
            if (pv < prov_count && !prov_first_pop[pv].valid()) {
                prov_first_pop[pv] = PopId{static_cast<PopId::rep_type>(pi)};
            }
        }
    }

    for (auto& flow : migration_flows_) {
        if (flow.population == 0u || flow.weeks_remaining > 0u) continue;
        if (!flow.source.valid() || !flow.destination.valid()) { flow.population = 0u; continue; }
        if (flow.source.value() >= prov_count || flow.destination.value() >= prov_count) { flow.population = 0u; continue; }

        const auto src_country = world.geography.province_owner(flow.source);
        const auto dst_country = world.geography.province_owner(flow.destination);

        const PopId src_pop = prov_first_pop[flow.source.value()];
        // R7: the destination may be an empty province — migrants found a new
        // POP there instead of requiring an existing cohort.
        if (!src_pop.valid()) {
            flow.population = 0u;
            continue;
        }

        const auto pop_culture = world.pops.culture(src_pop);

        // Migration policy applies to international border crossings (src_country != dst_country)
        const bool is_international = (src_country != dst_country);
        if (is_international) {
            // Exit border control
            if (src_country.valid() && migration_policy(src_country) == MigrationPolicy::ClosedBorders) {
                flow.population = 0u;
                continue;
            }
            // Entry border control
            if (dst_country.valid()) {
                const auto dst_policy = migration_policy(dst_country);
                if (dst_policy == MigrationPolicy::ClosedBorders) {
                    flow.population = 0u;
                    continue;
                }
                if (dst_policy == MigrationPolicy::MigrationControls) {
                    // Under MigrationControls, only accepted cultures can immigrate
                    if (!is_culture_accepted(dst_country, pop_culture)) {
                        flow.population = 0u;
                        continue;
                    }
                }
                // OpenBorders allows all cultures
            }
        }
        // Domestic migration (within same country) is unrestricted by international border policy

        // R7: identity-preserving settlement. Migrants merge into a destination
        // POP with matching culture/religion/profession/need-profile when one
        // exists; otherwise they found a new POP carrying the source cohort's
        // identity, literacy, qualifications, living standard and savings. The
        // source keeps a one-person remnant so the cohort stays recoverable.
        const auto src_size = world.pops.population(src_pop);
        const PopulationCount movable = src_size > 0u ? src_size - 1u : 0u;
        const auto actual_move = std::min(flow.population, movable);
        if (actual_move == 0u) { flow.population = 0u; continue; }
        const auto src_cash = std::max<EconomyAmount>(0, world.pops.cash(src_pop));
        const EconomyAmount cash_per_capita = src_size > 0u ? src_cash / static_cast<EconomyAmount>(src_size) : 0;
        const auto moved_cash = cash_per_capita * static_cast<EconomyAmount>(actual_move);
        const auto src_culture_move = world.pops.culture(src_pop);
        const auto src_religion = world.pops.religion(src_pop);
        const auto src_profession = world.pops.profession(src_pop);
        const auto src_needs = world.pops.need_profile(src_pop);
        const auto src_sol = world.pops.standard_of_living_milli(src_pop);
        const auto src_lit = world.pops.literacy_permyriad(src_pop);
        const auto src_qual = world.pops.qualification_permyriad(src_pop);
        world.pops.set_population(src_pop, src_size - actual_move);
        world.pops.add_cash(src_pop, -moved_cash);
        PopId dst_match{};
        for (std::size_t pi = 0; pi < world.pops.size(); ++pi) {
            if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
            const PopId candidate{static_cast<PopId::rep_type>(pi)};
            if (candidate == src_pop) continue;
            if (pi >= pop_provinces.size() || pop_provinces[pi] != flow.destination) continue;
            if (world.pops.culture(candidate) != src_culture_move ||
                world.pops.religion(candidate) != src_religion ||
                world.pops.profession(candidate) != src_profession ||
                world.pops.need_profile(candidate) != src_needs) continue;
            dst_match = candidate;
            break;
        }
        if (dst_match.valid()) {
            const auto dst_size = world.pops.population(dst_match);
            const auto total = static_cast<std::uint64_t>(dst_size) + actual_move;
            const auto blend = [&](std::int64_t dst_val, std::int64_t src_val) {
                return total > 0 ? static_cast<std::int32_t>(
                    (static_cast<std::int64_t>(dst_val) * dst_size +
                     static_cast<std::int64_t>(src_val) * actual_move) /
                    static_cast<std::int64_t>(total)) : static_cast<std::int32_t>(dst_val);
            };
            world.pops.set_population(dst_match, dst_size + actual_move);
            world.pops.add_cash(dst_match, moved_cash);
            world.pops.set_standard_of_living_milli(dst_match,
                blend(world.pops.standard_of_living_milli(dst_match), src_sol));
            world.pops.set_literacy_permyriad(dst_match, static_cast<std::uint16_t>(std::clamp<std::int64_t>(
                blend(world.pops.literacy_permyriad(dst_match), src_lit), 0, 10'000)));
            world.pops.set_qualification_permyriad(dst_match, static_cast<std::uint16_t>(std::clamp<std::int64_t>(
                blend(world.pops.qualification_permyriad(dst_match), src_qual), 0, 10'000)));
            // Radicals/loyalists are headcounts: the migrants carry their share.
            const auto src_rad = world.pops.radicals(src_pop);
            const auto src_loy = world.pops.loyalists(src_pop);
            if (src_size > 0u) {
                world.pops.add_radicals(dst_match, static_cast<std::int64_t>(src_rad) * actual_move / src_size);
                world.pops.add_loyalists(dst_match, static_cast<std::int64_t>(src_loy) * actual_move / src_size);
                world.pops.add_radicals(src_pop, -(static_cast<std::int64_t>(src_rad) * actual_move / src_size));
                world.pops.add_loyalists(src_pop, -(static_cast<std::int64_t>(src_loy) * actual_move / src_size));
            }
        } else {
            MarketId dst_market = world.pops.market(src_pop);
            if (flow.destination.valid() && flow.destination.value() < prov_count) {
                const auto dst_state = world.geography.province_state(flow.destination);
                if (dst_state.valid()) {
                    const auto state_market = world.geography.state_market(dst_state);
                    if (state_market.valid() &&
                        static_cast<std::size_t>(state_market.value()) < world.markets.size())
                        dst_market = state_market;
                }
            }
            PopInit init;
            init.market = dst_market;
            init.size = actual_move;
            init.need_profile = src_needs;
            init.province = flow.destination;
            init.culture = src_culture_move;
            init.religion = src_religion;
            init.profession = src_profession;
            init.literacy_permyriad = src_lit;
            init.qualification_permyriad = src_qual;
            const auto new_pop = world.pops.create(init);
            world.pops.set_standard_of_living_milli(new_pop, src_sol);
            world.pops.set_cash(new_pop, moved_cash);
        }
        flow.population = 0u;
    }
}

void GrandStrategyStore::generate_autonomous_migration_flows(World& world,
                                                             const EconomyDefinitions& definitions) {
    const auto prov_count = world.geography.province_count();
    if (prov_count == 0) return;
    // Single-pass province aggregates: unemployment pushes, vacancies pull.
    std::vector<std::uint64_t> unemployed(prov_count, 0u);
    std::vector<std::uint64_t> pop_total(prov_count, 0u);
    std::vector<std::uint64_t> vacancies(prov_count, 0u);
    const auto pop_provinces = world.pops.provinces();
    const auto pop_sizes = world.pops.populations();
    const auto pop_employed = world.pops.employed_all();
    for (std::size_t pi = 0; pi < world.pops.size(); ++pi) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
        if (pi >= pop_provinces.size() || !pop_provinces[pi].valid()) continue;
        const auto pv = pop_provinces[pi].value();
        if (pv >= prov_count) continue;
        const auto size = static_cast<std::uint64_t>(pi < pop_sizes.size() ? pop_sizes[pi] : 0u);
        const auto emp = static_cast<std::uint64_t>(pi < pop_employed.size() ? pop_employed[pi] : 0u);
        pop_total[pv] += size;
        if (size > emp) unemployed[pv] += size - emp;
    }
    const auto building_provinces = world.buildings.provinces();
    const auto building_levels = world.buildings.levels();
    const auto building_employees = world.buildings.employees_all();
    const auto type_defs = definitions.building_types();
    for (std::size_t bi = 0; bi < world.buildings.size(); ++bi) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi))) continue;
        if (bi >= building_provinces.size() || !building_provinces[bi].valid()) continue;
        const auto pv = building_provinces[bi].value();
        if (pv >= prov_count || bi >= building_levels.size()) continue;
        const auto type = bi < world.buildings.types().size()
            ? world.buildings.types()[bi] : BuildingTypeId{};
        if (!type.valid() || static_cast<std::size_t>(type.value()) >= type_defs.size()) continue;
        const auto capacity = static_cast<std::uint64_t>(type_defs[type.value()].workers_per_level) *
            building_levels[bi];
        const auto emp = static_cast<std::uint64_t>(bi < building_employees.size() ? building_employees[bi] : 0u);
        if (capacity > emp) vacancies[pv] += capacity - emp;
    }
    // Sources: unemployment share >= 10% and at least 100 idle hands.
    std::vector<std::uint32_t> sources;
    for (std::uint32_t pv = 0; pv < prov_count; ++pv) {
        if (pop_total[pv] == 0u || unemployed[pv] < 100u) continue;
        if (unemployed[pv] * 10u < pop_total[pv]) continue;
        sources.push_back(pv);
    }
    std::sort(sources.begin(), sources.end(), [&](std::uint32_t a, std::uint32_t b) {
        if (unemployed[a] != unemployed[b]) return unemployed[a] > unemployed[b];
        return a < b;
    });
    constexpr std::size_t max_sources_per_week = 16u;
    constexpr std::size_t max_new_flows_per_week = 32u;
    std::size_t created = 0;
    for (std::size_t si = 0; si < sources.size() && si < max_sources_per_week &&
         created < max_new_flows_per_week; ++si) {
        const ProvinceId src{static_cast<ProvinceId::rep_type>(sources[si])};
        const auto src_country = world.geography.province_owner(src);
        if (!src_country.valid()) continue;
        // Best same-country vacancy province (vacancies desc, id asc).
        std::uint32_t best_dst = prov_count;
        std::uint64_t best_vac = 0u;
        for (std::uint32_t pv = 0; pv < prov_count; ++pv) {
            if (pv == sources[si] || vacancies[pv] == 0u) continue;
            const ProvinceId dst{static_cast<ProvinceId::rep_type>(pv)};
            if (world.geography.province_owner(dst) != src_country) continue;
            if (vacancies[pv] > best_vac) { best_vac = vacancies[pv]; best_dst = pv; }
        }
        if (best_dst >= prov_count || best_vac == 0u) continue;
        const ProvinceId dst{static_cast<ProvinceId::rep_type>(best_dst)};
        bool duplicate = false;
        for (const auto& flow : migration_flows_) {
            if (flow.weeks_remaining > 0u && flow.source == src && flow.destination == dst) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        auto size = static_cast<PopulationCount>(std::min<std::uint64_t>(
            {unemployed[sources[si]] / 20u, 1'000u, best_vac}));
        if (size == 0u) continue;
        (void)start_migration_flow(src, dst, size, 4u);
        ++created;
    }
}



LawEnactmentId GrandStrategyStore::start_law_enactment(CountryId country, std::uint64_t law_hash) {
    if (!country.valid() || law_hash == 0u) throw std::invalid_argument("invalid law enactment parameters");
    for (std::size_t i = 0; i < law_enactments_.size(); ++i) {
        auto& record = law_enactments_[i];
        if (record.country == country && record.active) {
            record.law_hash = law_hash;
            record.progress_ppm = 0u;
            record.passed = false;
            return LawEnactmentId{static_cast<LawEnactmentId::rep_type>(i)};
        }
    }
    (void)ensure_government(country);
    return add_law_enactment({country, law_hash, 0u, 500'000u, true, false});
}

const GovernmentRecord* GrandStrategyStore::government_for(CountryId country) const noexcept {
    for (const auto& record : governments_) {
        if (record.country == country) return &record;
    }
    return nullptr;
}

WarId GrandStrategyStore::war_for_play(DiplomaticPlayId play) const noexcept {
    for (std::size_t i = 0; i < wars_.size(); ++i) {
        if (wars_[i].play == play && wars_[i].active) return WarId{static_cast<WarId::rep_type>(i)};
    }
    return WarId{};
}
} // namespace thunder
