#include "thunder/runtime/save/SaveGame.hpp"
#include "thunder/runtime/save/SaveGameInternal.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/simulation/gameplay/NotificationRuntime.hpp"
#include "thunder/simulation/gameplay/OnActionRuntime.hpp"
#include "thunder/simulation/ai/UtilityAi.hpp"
#if defined(THUNDER_HAS_ZSTD)
#include <zstd.h>
#endif
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace thunder {
namespace {

using namespace save_detail;


std::uint64_t hash_bytes(std::span<const std::byte> b){Fnv1a64 h;h.add_bytes(b);return h.value();}


void encode_grand(Writer& w, const GrandStrategyStore& gs) {
#define CNT(SPAN) w.u32(static_cast<std::uint32_t>((SPAN).size()))
    CNT(gs.technologys()); for(const auto&r:gs.technologys()){wid(w,r.country);w.u64(r.key_hash);w.u32(r.progress_ppm);w.boolean(r.unlocked);}
    CNT(gs.laws()); for(const auto&r:gs.laws()){wid(w,r.country);w.u64(r.key_hash);w.boolean(r.enacted);}
    CNT(gs.institutions()); for(const auto&r:gs.institutions()){wid(w,r.country);w.u64(r.key_hash);w.u16(r.level);}
    CNT(gs.companys()); for(const auto&r:gs.companys()){wid(w,r.country);w.u64(r.key_hash);w.i64(r.cash_milli);w.u32(r.productivity_ppm);w.boolean(r.active);}
    CNT(gs.trade_routes()); for(const auto&r:gs.trade_routes()){wid(w,r.source);wid(w,r.destination);wid(w,r.good);w.i64(r.quantity_milli);w.u16(r.level);w.boolean(r.active);}
    CNT(gs.ownership_stakes()); for(const auto&r:gs.ownership_stakes()){wid(w,r.owner_country);wid(w,r.owner_company);wid(w,r.building);w.u32(r.share_ppm);}
    CNT(gs.treatys()); for(const auto&r:gs.treatys()){wid(w,r.first);wid(w,r.second);w.u8(static_cast<std::uint8_t>(r.kind));w.u64(r.article_hash);w.boolean(r.active);}
    CNT(gs.armys()); for(const auto&r:gs.armys()){wid(w,r.country);wid(w,r.location);w.u32(r.manpower);w.u32(r.organization_ppm);}
    CNT(gs.navys()); for(const auto&r:gs.navys()){wid(w,r.country);wid(w,r.location);w.u32(r.sailors);w.u32(r.strength_ppm);wid(w,r.design);}
    CNT(gs.migration_flows()); for(const auto&r:gs.migration_flows()){wid(w,r.source);wid(w,r.destination);w.u32(r.population);w.u16(r.weeks_remaining);}
    CNT(gs.interest_groups()); for(const auto&r:gs.interest_groups()){wid(w,r.country);w.u64(r.key_hash);w.u32(r.clout_ppm);w.i32(r.approval_milli);}
    CNT(gs.political_partys()); for(const auto&r:gs.political_partys()){wid(w,r.country);w.u64(r.key_hash);w.u32(r.support_ppm);}
    CNT(gs.power_blocs()); for(const auto&r:gs.power_blocs()){wid(w,r.leader);w.u64(r.key_hash);w.u32(r.cohesion_ppm);}
    CNT(gs.diplomatic_plays()); for(const auto&r:gs.diplomatic_plays()){wid(w,r.initiator);wid(w,r.target);w.u8(static_cast<std::uint8_t>(r.phase));w.u64(r.war_goal_hash);}
    CNT(gs.fronts()); for(const auto&r:gs.fronts()){wid(w,r.first);wid(w,r.second);wid(w,r.state);w.i32(r.progress_milli);}
    CNT(gs.battles()); for(const auto&r:gs.battles()){wid(w,r.front);w.u32(r.attackers);w.u32(r.defenders);w.i32(r.progress_milli);w.boolean(r.resolved);}
    CNT(gs.colonys()); for(const auto&r:gs.colonys()){wid(w,r.country);wid(w,r.province);w.u32(r.progress_ppm);}
    CNT(gs.ship_designs()); for(const auto&r:gs.ship_designs()){wid(w,r.country);w.u64(r.hull_hash);w.u64(r.modules_hash);w.u32(r.combat_power_milli);w.i64(r.build_cost_milli);}
    CNT(gs.investment_pools()); for(const auto&r:gs.investment_pools()){wid(w,r.country);w.i64(r.cash_milli);w.i64(r.weekly_contribution_milli);}
    CNT(gs.diplomatic_relations()); for(const auto&r:gs.diplomatic_relations()){wid(w,r.first);wid(w,r.second);w.i32(r.relation_milli);w.u32(r.trust_ppm);w.u32(r.tension_ppm);}
    CNT(gs.diplomatic_play_states()); for(const auto&r:gs.diplomatic_play_states()){wid(w,r.play);w.u16(r.weeks_in_phase);w.u32(r.escalation_ppm);}
    CNT(gs.governments()); for(const auto&r:gs.governments()){wid(w,r.country);w.u32(r.legitimacy_ppm);w.i32(r.stability_milli);}
    CNT(gs.law_enactments()); for(const auto&r:gs.law_enactments()){wid(w,r.country);w.u64(r.law_hash);w.u32(r.progress_ppm);w.u32(r.support_ppm);w.boolean(r.active);w.boolean(r.passed);}
    CNT(gs.wars()); for(const auto&r:gs.wars()){wid(w,r.play);wid(w,r.attacker);wid(w,r.defender);w.i32(r.war_score_milli);w.u16(r.weeks);w.boolean(r.active);}
#undef CNT
}

// The original GrandStrategyStore payload predates several authoritative
// records. Keep that compact, stable prefix intact and append this extension
// after the other v4 sections. This makes old v4/v3 readers reject the new
// section explicitly rather than silently dropping live state, while the
// current reader can restore all fields before checksum validation.
void encode_grand_extension(Writer& w, const GrandStrategyStore& gs) {
    w.u32(grand_strategy_extension_tag);

    w.u32(static_cast<std::uint32_t>(gs.treatys().size()));
    for (const auto& record : gs.treatys()) {
        w.u64(record.treaty_key_hash);
        w.u64(record.start_tick);
        w.u64(record.expiry_tick);
        w.i32(record.obligation_milli);
        w.boolean(record.is_multilateral);
    }

    w.u32(static_cast<std::uint32_t>(gs.navys().size()));
    for (const auto& record : gs.navys()) {
        w.u8(static_cast<std::uint8_t>(record.mission));
        wid(w, record.assigned_zone);
    }

    w.u32(static_cast<std::uint32_t>(gs.treaty_articles().size()));
    for (const auto& record : gs.treaty_articles()) {
        wid(w, record.treaty);
        w.u8(static_cast<std::uint8_t>(record.kind));
        w.u64(record.article_hash);
        w.u64(record.payload_hash);
        w.boolean(record.active);
    }

    w.u32(static_cast<std::uint32_t>(gs.treaty_participants().size()));
    for (const auto& record : gs.treaty_participants()) {
        wid(w, record.treaty);
        wid(w, record.country);
        w.u8(record.role);
    }

    w.u32(static_cast<std::uint32_t>(gs.parliaments().size()));
    for (const auto& record : gs.parliaments()) {
        wid(w, record.country);
        w.u64(record.power_distribution_law_hash);
        w.boolean(record.elections_enabled);
        w.u16(record.election_interval_weeks);
        w.u16(record.weeks_to_next_election);
        w.u32(record.total_seats);
        w.u32(record.ruling_party_seats);
        w.u32(record.opposition_seats);
        w.u64(record.ruling_party_hash);
        w.u32(record.pop_vote_weight_ppm);
        w.u32(record.ig_clout_weight_ppm);
        w.u8(static_cast<std::uint8_t>(record.migration_policy));
    }

    w.u32(static_cast<std::uint32_t>(gs.cultural_acceptances().size()));
    for (const auto& record : gs.cultural_acceptances()) {
        wid(w, record.country);
        wid(w, record.culture);
        w.u8(static_cast<std::uint8_t>(record.level));
    }

    w.u32(static_cast<std::uint32_t>(gs.diplomatic_sways().size()));
    for (const auto& record : gs.diplomatic_sways()) {
        wid(w, record.play);
        wid(w, record.sponsor);
        wid(w, record.target_country);
        w.u8(static_cast<std::uint8_t>(record.offer_type));
        w.u64(record.payload_hash);
        w.boolean(record.accepted);
    }

    w.u32(static_cast<std::uint32_t>(gs.war_goals().size()));
    for (const auto& record : gs.war_goals()) {
        wid(w, record.play);
        wid(w, record.holder);
        wid(w, record.target);
        w.u8(static_cast<std::uint8_t>(record.goal_type));
        wid(w, record.state_target);
        w.boolean(record.primary);
        w.boolean(record.enforced);
    }

    w.u32(static_cast<std::uint32_t>(gs.commanders().size()));
    for (const auto& record : gs.commanders()) {
        wid(w, record.country);
        wid(w, record.location);
        w.u8(static_cast<std::uint8_t>(record.trait));
        w.u8(record.skill_level);
    }

    w.u32(static_cast<std::uint32_t>(gs.sea_zones().size()));
    for (const auto& record : gs.sea_zones()) {
        w.u64(record.key_hash);
        wid(w, record.controller);
        w.u32(record.blockade_efficiency_ppm);
    }

    w.u32(static_cast<std::uint32_t>(gs.naval_battles().size()));
    for (const auto& record : gs.naval_battles()) {
        wid(w, record.zone);
        wid(w, record.attacker_navy);
        wid(w, record.defender_navy);
        w.i32(record.progress_milli);
        w.boolean(record.resolved);
    }
}

struct DecodedGrandExtensionState {
    bool present = false;
};

DecodedGrandExtensionState decode_grand_extension(Reader& r, GrandStrategyStore& gs) {
    if (r.u32() != grand_strategy_extension_tag)
        throw std::runtime_error("invalid grand-strategy extension tag");

    const auto treaty_count = r.count(max_records);
    if (treaty_count != gs.treatys().size())
        throw std::runtime_error("grand-strategy treaty extension count mismatch");
    auto treaties = gs.treatys_mut();
    for (std::size_t i = 0; i < treaties.size(); ++i) {
        treaties[i].treaty_key_hash = r.u64();
        treaties[i].start_tick = r.u64();
        treaties[i].expiry_tick = r.u64();
        treaties[i].obligation_milli = r.i32();
        treaties[i].is_multilateral = r.boolean();
    }

    const auto navy_count = r.count(max_records);
    if (navy_count != gs.navys().size())
        throw std::runtime_error("grand-strategy navy extension count mismatch");
    auto navies = gs.navys_mut();
    for (std::size_t i = 0; i < navies.size(); ++i) {
        navies[i].mission = static_cast<NavalMission>(r.u8());
        navies[i].assigned_zone = rid<SeaZoneId>(r);
    }

    const auto article_count = r.count(max_records);
    for (std::uint32_t i = 0; i < article_count; ++i) {
        const auto treaty = rid<TreatyId>(r);
        const auto kind = static_cast<TreatyKind>(r.u8());
        gs.add_treaty_article({treaty, kind, r.u64(), r.u64(), r.boolean()});
    }

    const auto participant_count = r.count(max_records);
    for (std::uint32_t i = 0; i < participant_count; ++i) {
        gs.add_treaty_participant({rid<TreatyId>(r), rid<CountryId>(r), r.u8()});
    }

    const auto parliament_count = r.count(max_records);
    for (std::uint32_t i = 0; i < parliament_count; ++i) {
        ParliamentRecord record;
        record.country = rid<CountryId>(r);
        record.power_distribution_law_hash = r.u64();
        record.elections_enabled = r.boolean();
        record.election_interval_weeks = r.u16();
        record.weeks_to_next_election = r.u16();
        record.total_seats = r.u32();
        record.ruling_party_seats = r.u32();
        record.opposition_seats = r.u32();
        record.ruling_party_hash = r.u64();
        record.pop_vote_weight_ppm = r.u32();
        record.ig_clout_weight_ppm = r.u32();
        record.migration_policy = static_cast<MigrationPolicy>(r.u8());
        gs.add_parliament(record);
    }

    const auto acceptance_count = r.count(max_records);
    for (std::uint32_t i = 0; i < acceptance_count; ++i) {
        gs.add_cultural_acceptance({rid<CountryId>(r), rid<CultureId>(r),
                                    static_cast<CulturalAcceptanceLevel>(r.u8())});
    }

    const auto sway_count = r.count(max_records);
    for (std::uint32_t i = 0; i < sway_count; ++i) {
        gs.add_diplomatic_sway({rid<DiplomaticPlayId>(r), rid<CountryId>(r), rid<CountryId>(r),
                                static_cast<SwayOfferType>(r.u8()), r.u64(), r.boolean()});
    }

    const auto war_goal_count = r.count(max_records);
    for (std::uint32_t i = 0; i < war_goal_count; ++i) {
        gs.add_war_goal({rid<DiplomaticPlayId>(r), rid<CountryId>(r), rid<CountryId>(r),
                         static_cast<WarGoalType>(r.u8()), rid<StateId>(r), r.boolean(), r.boolean()});
    }

    const auto commander_count = r.count(max_records);
    for (std::uint32_t i = 0; i < commander_count; ++i) {
        gs.add_commander({rid<CountryId>(r), rid<StateId>(r),
                          static_cast<CommanderTrait>(r.u8()), r.u8()});
    }

    const auto sea_zone_count = r.count(max_records);
    for (std::uint32_t i = 0; i < sea_zone_count; ++i) {
        gs.add_sea_zone({r.u64(), rid<CountryId>(r), r.u32()});
    }

    const auto naval_battle_count = r.count(max_records);
    for (std::uint32_t i = 0; i < naval_battle_count; ++i) {
        gs.add_naval_battle({rid<SeaZoneId>(r), rid<NavyId>(r), rid<NavyId>(r), r.i32(), r.boolean()});
    }

    return {true};
}
void decode_grand_v1(Reader& r, GrandStrategyStore& gs){
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_technology({rid<CountryId>(r),r.u64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_law({rid<CountryId>(r),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_institution({rid<CountryId>(r),r.u64(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_company({rid<CountryId>(r),r.u64(),r.i64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_trade_route({rid<MarketId>(r),rid<MarketId>(r),rid<GoodId>(r),r.i64(),r.u16(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ownership_stake({rid<CountryId>(r),rid<CompanyId>(r),rid<BuildingId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_treaty({rid<CountryId>(r),rid<CountryId>(r),static_cast<TreatyKind>(r.u8()),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_army({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_navy({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32(),rid<ShipDesignId>(r)});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_migration_flow({rid<ProvinceId>(r),rid<ProvinceId>(r),r.u32(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_interest_group({rid<CountryId>(r),r.u64(),r.u32(),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_political_party({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_power_bloc({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_play({rid<CountryId>(r),rid<CountryId>(r),static_cast<DiplomaticPlayPhase>(r.u8()),r.u64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_front({rid<CountryId>(r),rid<CountryId>(r),rid<StateId>(r),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_battle({rid<FrontId>(r),r.u32(),r.u32(),r.i32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_colony({rid<CountryId>(r),rid<ProvinceId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ship_design({rid<CountryId>(r),r.u64(),r.u64(),r.u32(),r.i64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_investment_pool({rid<CountryId>(r),r.i64(),r.i64()});
}

void decode_grand_v3(Reader& r, GrandStrategyStore& gs){
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_technology({rid<CountryId>(r),r.u64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_law({rid<CountryId>(r),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_institution({rid<CountryId>(r),r.u64(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_company({rid<CountryId>(r),r.u64(),r.i64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_trade_route({rid<MarketId>(r),rid<MarketId>(r),rid<GoodId>(r),r.i64(),r.u16(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ownership_stake({rid<CountryId>(r),rid<CompanyId>(r),rid<BuildingId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_treaty({rid<CountryId>(r),rid<CountryId>(r),static_cast<TreatyKind>(r.u8()),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_army({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_navy({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32(),rid<ShipDesignId>(r)});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_migration_flow({rid<ProvinceId>(r),rid<ProvinceId>(r),r.u32(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_interest_group({rid<CountryId>(r),r.u64(),r.u32(),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_political_party({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_power_bloc({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_play({rid<CountryId>(r),rid<CountryId>(r),static_cast<DiplomaticPlayPhase>(r.u8()),r.u64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_front({rid<CountryId>(r),rid<CountryId>(r),rid<StateId>(r),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_battle({rid<FrontId>(r),r.u32(),r.u32(),r.i32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_colony({rid<CountryId>(r),rid<ProvinceId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ship_design({rid<CountryId>(r),r.u64(),r.u64(),r.u32(),r.i64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_investment_pool({rid<CountryId>(r),r.i64(),r.i64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_relation({rid<CountryId>(r),rid<CountryId>(r),r.i32(),r.u32(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_play_state({rid<DiplomaticPlayId>(r),r.u16(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_government({rid<CountryId>(r),r.u32(),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_law_enactment({rid<CountryId>(r),r.u64(),r.u32(),r.u32(),r.boolean(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_war({rid<DiplomaticPlayId>(r),rid<CountryId>(r),rid<CountryId>(r),r.i32(),r.u16(),r.boolean()});
}


} // namespace



SaveGameBlob SaveGameCodec::encode(const World& world, const GameClock& clock,
                                   const ScriptedGameplayRuntime& gameplay, const UtilityAiEngine& ai,
                                   const NotificationRuntime& notifications,
                                   const OnActionRuntime& on_actions,
                                   std::uint64_t content_hash, std::uint64_t world_pack_hash) {
    if (!clock.validate_state()) throw std::invalid_argument("invalid game clock state");
    if (!world.currencies.validate(world.countries.size()))
        throw std::invalid_argument("invalid currency state");
    notifications.validate_state(notifications.instances(), notifications.next_instance_id(),
                                 world, clock.tick_index());
    on_actions.validate_state(on_actions.queue(), on_actions.next_invocation_id(), world);
    Writer p;
    const auto d=clock.date(); p.i32(d.year);p.u32(d.month);p.u32(d.day);p.u32(d.hour);p.u64(clock.tick_index());p.u64(clock.day_index());
    p.u32(static_cast<std::uint32_t>(world.countries.size()));
    for(std::size_t i=0;i<world.countries.size();++i){
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        p.string(world.countries.tag(id));
        p.f64(world.countries.population(id));
        p.f64(world.countries.gdp(id));
        p.f64(world.countries.treasury(id));
        p.f64(world.countries.tax_rate(id));
        const auto& tp = world.countries.tax_policy(id);
        p.i32(tp.income_tax_ppm);
        p.i32(tp.consumption_tax_ppm);
        p.i32(tp.land_tax_ppm);
        p.i32(tp.per_capita_tax_ppm);
        p.i32(tp.dividends_tax_ppm);
    }
    p.u32(static_cast<std::uint32_t>(world.markets.size()));p.u32(static_cast<std::uint32_t>(world.markets.good_count()));
    for(std::size_t mi=0;mi<world.markets.size();++mi){const MarketId m{static_cast<MarketId::rep_type>(mi)};wid(p,world.markets.owner(m));for(auto x:world.markets.price_row(m))p.i64(x);for(auto x:world.markets.supply_row(m))p.i64(x);for(auto x:world.markets.demand_row(m))p.i64(x);for(auto x:world.markets.inventory_row(m))p.i64(x);for(auto x:world.markets.shortage_row(m))p.i64(x);}
    p.u32(static_cast<std::uint32_t>(world.geography.state_count()));
    for(std::size_t i=0;i<world.geography.state_count();++i){const StateId id{static_cast<StateId::rep_type>(i)};p.string(world.geography.state_key(id));wid(p,world.geography.state_owner(id));wid(p,world.geography.state_market(id));wid(p,world.geography.state_capital(id));}
    p.u32(static_cast<std::uint32_t>(world.geography.province_count()));
    for(std::size_t i=0;i<world.geography.province_count();++i){const ProvinceId id{static_cast<ProvinceId::rep_type>(i)};p.string(world.geography.province_key(id));wid(p,world.geography.province_state(id));wid(p,world.geography.province_owner(id));wid(p,world.geography.province_market(id));p.f64(world.geography.province_center_x(id));p.f64(world.geography.province_center_y(id));p.u32(world.geography.province_areas_km2()[i]);}
    // Store dense rows even for tombstoned slots.  The liveness/allocator
    // section below restores which rows are addressable; reading raw SoA
    // columns here avoids calling accessors that intentionally reject dead
    // IDs during ordinary gameplay.
    p.u32(static_cast<std::uint32_t>(world.buildings.size()));
    const auto building_markets = world.buildings.markets();
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();
    const auto building_wages = world.buildings.wage_offers();
    const auto building_cash = world.buildings.cash_all();
    const auto building_provinces = world.buildings.provinces();
    const auto building_methods = world.buildings.production_methods();
    const auto building_employees = world.buildings.employees_all();
    const auto building_profits = world.buildings.last_profits();
    for (std::size_t i = 0; i < world.buildings.size(); ++i) {
        wid(p, building_markets[i]);
        wid(p, building_types[i]);
        p.u16(building_levels[i]);
        p.i64(building_wages[i]);
        p.i64(building_cash[i]);
        wid(p, building_provinces[i]);
        wid(p, building_methods[i]);
        p.u32(building_employees[i]);
        p.i64(building_profits[i]);
    }
    p.u32(static_cast<std::uint32_t>(world.pops.size()));
    const auto pop_markets = world.pops.markets();
    const auto pop_population = world.pops.populations();
    const auto pop_employers = world.pops.employers();
    const auto pop_need_profiles = world.pops.need_profiles();
    const auto pop_provinces = world.pops.provinces();
    const auto pop_cultures = world.pops.cultures();
    const auto pop_religions = world.pops.religions();
    const auto pop_professions = world.pops.professions();
    const auto pop_interest_groups = world.pops.interest_groups();
    const auto pop_literacy = world.pops.literacy_all();
    const auto pop_qualification = world.pops.qualifications_all();
    const auto pop_wealth = world.pops.wealth_all();
    const auto pop_political_strength = world.pops.political_strength_all();
    const auto pop_employed = world.pops.employed_all();
    const auto pop_income = world.pops.incomes();
    const auto pop_cash = world.pops.cash_all();
    const auto pop_sol = world.pops.sol_all();
    for (std::size_t i = 0; i < world.pops.size(); ++i) {
        wid(p, pop_markets[i]);
        p.u32(pop_population[i]);
        wid(p, pop_employers[i]);
        wid(p, pop_need_profiles[i]);
        wid(p, pop_provinces[i]);
        wid(p, pop_cultures[i]);
        wid(p, pop_religions[i]);
        wid(p, pop_professions[i]);
        wid(p, pop_interest_groups[i]);
        p.u16(pop_literacy[i]);
        p.u16(pop_qualification[i]);
        p.i32(pop_wealth[i]);
        p.u32(pop_political_strength[i]);
        p.u32(pop_employed[i]);
        p.i64(pop_income[i]);
        p.i64(pop_cash[i]);
        p.i32(pop_sol[i]);
    }
    encode_grand(p,world.grand_strategy);
    encode_gameplay(p, gameplay);
    encode_ai(p, ai);

    const bool has_gameplay_context_section = !gameplay.instances().empty() ||
        gameplay.next_instance_id() != 1u;
    if (has_gameplay_context_section) encode_gameplay_context_section(p, gameplay, world);
    encode_global_script_section(p, world.global_scripts, world);

    // Preserve byte-for-byte compatibility for v4 saves that predate the extension.
    // Once notification content or state exists, a tagged stable-key section is emitted.
    const bool has_notification_section = !notifications.definitions().empty() ||
        !notifications.instances().empty() || notifications.next_instance_id() != 1u;
    if (has_notification_section) encode_notification_section(p, notifications);

    const bool has_on_action_section = !on_actions.definitions().empty() ||
        !on_actions.queue().empty() || on_actions.next_invocation_id() != 1u;
    if (has_on_action_section) encode_on_action_section(p, on_actions);

    // Authoritative market monetary state is always emitted by new v4 writers.
    // Keeping it tagged and last preserves the read-only migration paths for
    // earlier v4 payloads and for the synthetic v3 compatibility fixture.
    encode_market_monetary_section(p, world.markets);
    encode_fx_section(p, world.currencies, world.countries);
    encode_financial_section(p, world);
    encode_construction_section(p, world.construction, world);
    encode_resistance_section(p, world.geography);
    encode_geography_section(p, world.geography);
    // Dense entity rows retain stable indices; SLT1 carries the allocator
    // bitmap, generations and free-list order needed for deterministic reuse.
    encode_slot_section(p, world);
    // Grand-strategy records added after the original v1/v3 payload are
    // appended after SLT1 so v3 read-only migration fixtures can still trim
    // the v4 extension tail at their historical marker.
    encode_grand_extension(p, world.grand_strategy);
    // v5: per-POP fractional growth accumulators (demographic loop state).
    encode_population_growth_section(p, world.pops);

    const auto checksum=world.checksum();
    const auto runtime_state_checksum = has_on_action_section
        ? runtime_checksum(gameplay.checksum(), ai.checksum(), clock.checksum(),
                           notifications.checksum(), on_actions.checksum())
        : has_notification_section
            ? runtime_checksum(gameplay.checksum(), ai.checksum(), clock.checksum(),
                               notifications.checksum())
            : runtime_checksum(gameplay.checksum(), ai.checksum(), clock.checksum());
    const auto payload_hash=hash_bytes(p.out);
    Writer out; out.raw(magic,sizeof(magic));out.u32(version);out.u64(content_hash);out.u64(world_pack_hash);out.u64(checksum);out.u64(runtime_state_checksum);out.u64(static_cast<std::uint64_t>(p.out.size()));out.u64(payload_hash);out.raw(p.out.data(),p.out.size());
    return {{version,content_hash,world_pack_hash,checksum,runtime_state_checksum},std::move(out.out)};
}

SaveGameBlob SaveGameCodec::encode_compressed(const World& world, const GameClock& clock,
                                              const ScriptedGameplayRuntime& gameplay,
                                              const UtilityAiEngine& ai,
                                              const NotificationRuntime& notifications,
                                              const OnActionRuntime& on_actions,
                                              std::uint64_t content_hash,
                                              std::uint64_t world_pack_hash,
                                              int compression_level) {
    auto blob = encode(world, clock, gameplay, ai, notifications, on_actions, content_hash, world_pack_hash);
#if defined(THUNDER_HAS_ZSTD)
    const std::size_t max_c_size = ZSTD_compressBound(blob.bytes.size());
    std::vector<std::byte> compressed(max_c_size);
    const std::size_t c_size = ZSTD_compress(
        compressed.data(), max_c_size,
        blob.bytes.data(), blob.bytes.size(),
        compression_level);
    if (ZSTD_isError(c_size)) {
        throw std::runtime_error("Zstd save compression failed");
    }
    compressed.resize(c_size);

    const auto payload_hash = hash_bytes(compressed);
    Writer out;
    out.raw(compressed_magic, sizeof(compressed_magic));
    out.u64(static_cast<std::uint64_t>(blob.bytes.size()));
    out.u64(static_cast<std::uint64_t>(compressed.size()));
    out.u64(payload_hash);
    out.raw(compressed.data(), compressed.size());

    blob.bytes = std::move(out.out);
    return blob;
#else
    (void)compression_level;
    return blob;
#endif
}

SaveGameMetadata SaveGameCodec::decode(std::span<const std::byte> bytes, World& world,
                                      GameClock& clock, ScriptedGameplayRuntime& gameplay,
                                      UtilityAiEngine& ai, NotificationRuntime& notifications,
                                      OnActionRuntime& on_actions,
                                      const EconomyDefinitions& definitions,
                                      std::uint64_t expected_content_hash,
                                      std::uint64_t expected_world_pack_hash) {
    if (bytes.size() >= sizeof(compressed_magic) &&
        std::memcmp(bytes.data(), compressed_magic, sizeof(compressed_magic)) == 0) {
#if defined(THUNDER_HAS_ZSTD)
        Reader cr(bytes);
        cr.need(sizeof(compressed_magic));
        cr.pos += sizeof(compressed_magic);
        const auto uncompressed_size = cr.u64();
        const auto compressed_size = cr.u64();
        const auto expected_payload_hash = cr.u64();
        if (compressed_size != bytes.size() - cr.pos) {
            throw std::runtime_error("compressed save payload size mismatch");
        }
        if (hash_bytes(bytes.subspan(cr.pos)) != expected_payload_hash) {
            throw std::runtime_error("compressed save tamper-proof checksum mismatch");
        }
        std::vector<std::byte> decompressed(static_cast<std::size_t>(uncompressed_size));
        const std::size_t d_res = ZSTD_decompress(
            decompressed.data(), decompressed.size(),
            bytes.data() + cr.pos, static_cast<std::size_t>(compressed_size));
        if (ZSTD_isError(d_res) || d_res != decompressed.size()) {
            throw std::runtime_error("Zstd save decompression failed");
        }
        return decode(decompressed, world, clock, gameplay, ai, notifications,
                      on_actions, definitions, expected_content_hash, expected_world_pack_hash);
#else
        throw std::runtime_error("Thunder built without ZSTD support; cannot decompress save");
#endif
    }
    Reader r(bytes); r.need(sizeof(magic)); if(std::memcmp(bytes.data(),magic,sizeof(magic))!=0)throw std::runtime_error("invalid Thunder save magic");r.pos+=sizeof(magic);
    const auto ver=r.u32();
    if(ver!=version && ver!=runtime_v3_version && ver!=legacy_version)throw std::runtime_error("unsupported Thunder save version");
    const auto content_hash=r.u64();const auto world_hash=r.u64();const auto expected_checksum=r.u64();
    const auto expected_runtime_checksum = ver >= runtime_v3_version ? r.u64() : 0u;
    const auto payload_size=r.u64();const auto payload_hash=r.u64();
    if (expected_content_hash != 0 && content_hash != expected_content_hash)
        throw std::runtime_error("save content hash mismatch");
    if (expected_world_pack_hash != 0 && world_hash != expected_world_pack_hash)
        throw std::runtime_error("save world-pack hash mismatch");
    if (payload_size != bytes.size() - r.pos)
        throw std::runtime_error("save payload size mismatch");
    if (hash_bytes(bytes.subspan(r.pos)) != payload_hash)
        throw std::runtime_error("save payload checksum mismatch");
    Reader p(bytes.subspan(r.pos)); World decoded; GameClock decoded_clock;
    GameDate date;date.year=p.i32();date.month=p.u32();date.day=p.u32();date.hour=p.u32();const auto ticks=p.u64(),days=p.u64();if(!GameClock::validate_state(date,ticks,days))throw std::runtime_error("invalid game clock state in save");decoded_clock.restore_state(date,ticks,days);
    const auto countries=p.count(max_countries);decoded.countries.reserve(countries);
    for(std::uint32_t i=0;i<countries;++i){
        const auto id = decoded.countries.create({p.string(),p.f64(),p.f64(),p.f64(),p.f64()});
        if (ver != legacy_version) {
            TaxPolicy tp;
            tp.income_tax_ppm = p.i32();
            tp.consumption_tax_ppm = p.i32();
            tp.land_tax_ppm = p.i32();
            tp.per_capita_tax_ppm = p.i32();
            tp.dividends_tax_ppm = p.i32();
            decoded.countries.set_tax_policy(id, tp);
        }
    }
    const auto markets=p.count(max_markets);const auto goods=p.u32();if(goods!=definitions.good_count())throw std::runtime_error("save good-count incompatible with definitions");decoded.markets.resize(markets,definitions);for(std::uint32_t mi=0;mi<markets;++mi){const MarketId m{mi};decoded.markets.set_owner(m,rid<CountryId>(p));auto prices=decoded.markets.price_row(m);auto supply=decoded.markets.supply_row(m);auto demand=decoded.markets.demand_row(m);for(auto&x:prices)x=p.i64();for(auto&x:supply)x=p.i64();for(auto&x:demand)x=p.i64();if(ver>=extension_sections_since_version){auto inventory=decoded.markets.inventory_row(m);for(auto&x:inventory)x=p.i64();auto shortage=decoded.markets.shortage_row(m);for(auto&x:shortage)x=p.i64();}}
    const auto states=p.count(max_states);decoded.geography.reserve_states(states);for(std::uint32_t i=0;i<states;++i)decoded.geography.create_state({p.string(),rid<CountryId>(p),rid<MarketId>(p),rid<ProvinceId>(p)});
    const auto provinces=p.count(max_provinces);decoded.geography.reserve_provinces(provinces);for(std::uint32_t i=0;i<provinces;++i)decoded.geography.create_province({p.string(),rid<StateId>(p),rid<CountryId>(p),rid<MarketId>(p),p.f64(),p.f64(),p.u32()});
    const auto buildings=p.count(max_buildings);decoded.buildings.reserve(buildings);for(std::uint32_t i=0;i<buildings;++i){BuildingInit init;init.market=rid<MarketId>(p);init.type=rid<BuildingTypeId>(p);init.level=p.u16();init.wage_offer_milli=p.i64();init.cash_milli=p.i64();init.province=rid<ProvinceId>(p);init.production_method=rid<ProductionMethodId>(p);const auto id=decoded.buildings.create(init);decoded.buildings.set_employees(id,p.u32());decoded.buildings.set_last_profit(id,p.i64());}
    const auto pops=p.count(max_pops);decoded.pops.reserve(pops);
    for(std::uint32_t i=0;i<pops;++i){
        PopInit init;
        init.market=rid<MarketId>(p);
        init.size=p.u32();
        init.employer=rid<BuildingId>(p);
        init.need_profile=rid<NeedProfileId>(p);
        init.province=rid<ProvinceId>(p);
        init.culture=rid<CultureId>(p);
        init.religion=rid<ReligionId>(p);
        init.profession=rid<ProfessionId>(p);
        init.interest_group=rid<InterestGroupId>(p);
        init.literacy_permyriad=p.u16();
        init.qualification_permyriad=p.u16();
        init.wealth_milli=p.i32();
        init.political_strength_milli=p.u32();
        const auto id=decoded.pops.create(init);
        decoded.pops.set_employed(id,p.u32());
        decoded.pops.set_income(id,p.i64());
        if (ver != legacy_version) {
            decoded.pops.set_cash(id,p.i64());
        }
        decoded.pops.set_standard_of_living_milli(id,p.i32());
    }
    DecodedGameplayState decoded_gameplay;
    DecodedAiState decoded_ai;
    DecodedNotificationState decoded_notifications;
    DecodedOnActionState decoded_on_actions;
    DecodedMarketMonetaryState decoded_market_monetary;
    DecodedFxState decoded_fx;
    DecodedFinancialState decoded_financial;
    DecodedConstructionState decoded_construction;
    DecodedResistanceState decoded_resistance;
    DecodedGeographyState decoded_geography;
    DecodedSlotState decoded_slots;
    DecodedGrandExtensionState decoded_grand_extension;
    DecodedGlobalScriptState decoded_global_scripts;
    DecodedPopulationGrowthState decoded_population_growth;
    if (ver == legacy_version) {
        decode_grand_v1(p, decoded.grand_strategy);
    } else {
        decode_grand_v3(p, decoded.grand_strategy);
        decoded_gameplay = decode_gameplay(p, gameplay);
        decoded_ai = decode_ai(p, ai, ver >= extension_sections_since_version);
        if (ver >= extension_sections_since_version) {
            while (!p.done()) {
                const auto tag = p.peek_u32();
                if (tag == gameplay_context_section_tag) {
                    decode_gameplay_context_section(p, decoded_gameplay);
                } else if (tag == global_script_section_tag) {
                    decode_global_script_section(p, decoded, decoded_global_scripts);
                } else if (tag == notification_section_tag) {
                    if (decoded_notifications.present)
                        throw std::runtime_error("duplicate notification extension");
                    decoded_notifications = decode_notification_section(p, notifications);
                } else if (tag == on_action_section_tag) {
                    if (decoded_on_actions.present)
                        throw std::runtime_error("duplicate on_action extension");
                    decoded_on_actions = decode_on_action_section(p, on_actions);
                } else if (tag == market_monetary_section_tag) {
                    if (decoded_market_monetary.present)
                        throw std::runtime_error("duplicate market monetary extension");
                    decoded_market_monetary = decode_market_monetary_section(
                        p, decoded.markets);
                } else if (tag == fx_section_tag) {
                    if (decoded_fx.present)
                        throw std::runtime_error("duplicate fx extension");
                    decoded_fx = decode_fx_section(
                        p, decoded.currencies, decoded.countries);
                } else if (tag == financial_section_tag) {
                    if (decoded_financial.present)
                        throw std::runtime_error("duplicate financial extension");
                    decoded_financial = decode_financial_section(p, decoded);
                } else if (tag == construction_section_tag) {
                    if (decoded_construction.present)
                        throw std::runtime_error("duplicate construction extension");
                    decoded_construction = decode_construction_section(p, decoded.construction);
                } else if (tag == resistance_section_tag) {
                    if (decoded_resistance.present)
                        throw std::runtime_error("duplicate resistance extension");
                    decoded_resistance = decode_resistance_section(p, decoded.geography);
                } else if (tag == geography_section_tag) {
                    if (decoded_geography.present)
                        throw std::runtime_error("duplicate geography extension");
                    decoded_geography = decode_geography_section(p, decoded.geography);
                } else if (tag == slot_section_tag) {
                    if (decoded_slots.present)
                        throw std::runtime_error("duplicate slot-pool extension");
                    decoded_slots = decode_slot_section(p, decoded);
                } else if (tag == grand_strategy_extension_tag) {
                    if (decoded_grand_extension.present)
                        throw std::runtime_error("duplicate grand-strategy extension");
                    decoded_grand_extension = decode_grand_extension(p, decoded.grand_strategy);
                } else if (tag == population_growth_section_tag) {
                    if (decoded_population_growth.present)
                        throw std::runtime_error("duplicate population growth extension");
                    decoded_population_growth = decode_population_growth_section(p, decoded.pops);
                } else {
                    throw std::runtime_error("unknown extension section in Thunder save");
                }
            }
        }
    }
    if(!p.done())throw std::runtime_error("trailing bytes in Thunder save");
    if (!decoded.geography.validate(decoded.countries.size(), decoded.markets.size()))
        throw std::runtime_error("invalid geography references in save");
    if (!decoded.grand_strategy.validate(decoded.countries.size(), decoded.markets.size(),
                                         decoded.geography.province_count(), decoded.geography.state_count(),
                                         decoded.buildings.size(), definitions.good_count()))
        throw std::runtime_error("invalid grand-strategy references in save");
    if (!decoded.banks.validate(decoded.countries.size(), decoded.buildings.size()))
        throw std::runtime_error("invalid banking references or balance sheet in save");
    if (!decoded.trade_policies.validate(decoded.countries.size()))
        throw std::runtime_error("invalid trade policy state in save");
    if (!decoded.currencies.validate(decoded.countries.size()))
        throw std::runtime_error("invalid currency state in save");
    if (!decoded.construction.validate(decoded))
        throw std::runtime_error("invalid construction queue state in save");
    if (!decoded.global_scripts.validate(decoded))
        throw std::runtime_error("invalid global script state in save");
    for (std::size_t i = 0; i < decoded.buildings.size(); ++i) {
        if (!decoded.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const BuildingId id{static_cast<BuildingId::rep_type>(i)};
        if (decoded.buildings.market(id).valid() &&
            decoded.buildings.market(id).value() >= decoded.markets.size())
            throw std::runtime_error("building market reference invalid");
        if (decoded.buildings.type(id).value() >= definitions.building_type_count())
            throw std::runtime_error("building type reference invalid");
        const auto pm = decoded.buildings.production_method(id);
        if (pm.valid()) {
            if (pm.value() >= definitions.production_method_count())
                throw std::runtime_error("production method reference invalid");
            if (definitions.production_method(pm).building_type != decoded.buildings.type(id))
                throw std::runtime_error("production method building-type mismatch");
        }
        if (decoded.buildings.province(id).valid() &&
            decoded.buildings.province(id).value() >= decoded.geography.province_count())
            throw std::runtime_error("building province reference invalid");
    }
    for (std::size_t i = 0; i < decoded.pops.size(); ++i) {
        if (!decoded.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const PopId id{static_cast<PopId::rep_type>(i)};
        if (decoded.pops.market(id).valid() &&
            decoded.pops.market(id).value() >= decoded.markets.size())
            throw std::runtime_error("pop market reference invalid");
        if (decoded.pops.need_profile(id).value() >= definitions.need_profile_count())
            throw std::runtime_error("pop need-profile reference invalid");
        if (decoded.pops.employer(id).valid() &&
            decoded.pops.employer(id).value() >= decoded.buildings.size())
            throw std::runtime_error("pop employer reference invalid");
        if (decoded.pops.province(id).valid() &&
            decoded.pops.province(id).value() >= decoded.geography.province_count())
            throw std::runtime_error("pop province reference invalid");
        if (decoded.pops.employer(id).valid() &&
            !decoded.buildings.slot_pool().is_index_alive(decoded.pops.employer(id).value()))
            throw std::runtime_error("pop employer references dead building");
    }
    gameplay.validate_state(decoded_gameplay.instances, decoded_gameplay.log, decoded,
                            decoded_clock.tick_index(), decoded_gameplay.next_instance_id);
    ai.validate_state(decoded_ai.actions, decoded, decoded_clock.tick_index());
    ai.validate_plan_state(decoded_ai.plans, decoded, decoded_clock.tick_index());
    notifications.validate_state(decoded_notifications.instances,
                                 decoded_notifications.next_instance_id,
                                 decoded, decoded_clock.tick_index());
    on_actions.validate_state(decoded_on_actions.queue,
                              decoded_on_actions.next_invocation_id, decoded);
    bool world_checksum_matches = false;
    if (ver == legacy_version) {
        world_checksum_matches = legacy_world_checksum_v1(decoded) == expected_checksum;
    } else if (decoded_slots.present) {
        // SLT1 makes allocator generations and free-list order authoritative;
        // never accept an older component-only digest for a save that carries
        // this state, otherwise allocator corruption could be hidden by a
        // legacy checksum collision. v5 adds the per-POP growth accumulators
        // to the pop digest, so pre-growth v4 digests stay readable.
        world_checksum_matches = decoded.checksum() == expected_checksum ||
                                 legacy_world_checksum_v4_pre_growth(decoded) == expected_checksum;
    } else if (decoded_resistance.present) {
        // v4 saves written before SLT1 have the resistance extension but do
        // not include allocator generations/free-list state in the world
        // checksum. Accept their historical digest only when the slot section
        // is absent.
        world_checksum_matches = decoded.checksum() == expected_checksum ||
                                 legacy_world_checksum_pre_resistance(decoded) == expected_checksum;
    } else if (decoded_construction.present) {
        world_checksum_matches = decoded.checksum() == expected_checksum ||
                                 legacy_world_checksum_pre_resistance(decoded) == expected_checksum;
    } else if (decoded_financial.present) {
        world_checksum_matches = legacy_world_checksum_pre_construction(decoded) == expected_checksum ||
                                 legacy_world_checksum_pre_resistance(decoded) == expected_checksum ||
                                 decoded.checksum() == expected_checksum;
    } else if (decoded_fx.present || decoded_market_monetary.present) {
        world_checksum_matches = legacy_world_checksum_pre_financial(decoded) == expected_checksum ||
                                 legacy_world_checksum_v4_pre_fx(decoded) == expected_checksum ||
                                 legacy_world_checksum_pre_resistance(decoded) == expected_checksum ||
                                 decoded.checksum() == expected_checksum;
    } else if (ver == runtime_v3_version) {
        world_checksum_matches =
            legacy_world_checksum_v3_pre_mon1(decoded) == expected_checksum ||
            legacy_world_checksum_v4_pre_mon1(decoded) == expected_checksum ||
            legacy_world_checksum_v4_pre_fx(decoded) == expected_checksum ||
            legacy_world_checksum_pre_financial(decoded) == expected_checksum ||
            legacy_world_checksum_pre_construction(decoded) == expected_checksum ||
            legacy_world_checksum_pre_resistance(decoded) == expected_checksum ||
            decoded.checksum() == expected_checksum;
    } else {
        world_checksum_matches =
            legacy_world_checksum_v4_pre_mon1(decoded) == expected_checksum ||
            legacy_world_checksum_v4_pre_fx(decoded) == expected_checksum ||
            legacy_world_checksum_pre_financial(decoded) == expected_checksum ||
            legacy_world_checksum_pre_construction(decoded) == expected_checksum ||
            legacy_world_checksum_pre_resistance(decoded) == expected_checksum ||
            decoded.checksum() == expected_checksum;
    }
    if (!world_checksum_matches)
        throw std::runtime_error("save world checksum mismatch");
    const auto gameplay_state_checksum = gameplay.checksum_state(
        decoded_gameplay.instances, decoded_gameplay.log,
        decoded_gameplay.context_section_present ? decoded_gameplay.next_instance_id : 0u);
    const auto ai_state_checksum = ver == runtime_v3_version
        ? legacy_ai_checksum_v3(ai, decoded_ai.actions)
        : ai.checksum_state(decoded_ai.actions, decoded_ai.plans);
    const auto legacy_runtime_state_checksum = runtime_checksum(gameplay_state_checksum, ai_state_checksum);
    const auto decoded_runtime_checksum = runtime_checksum(gameplay_state_checksum, ai_state_checksum,
                                                           decoded_clock.checksum());
    const auto decoded_notification_checksum = notifications.checksum_state(
        decoded_notifications.instances, decoded_notifications.next_instance_id);
    const auto extended_runtime_checksum = runtime_checksum(
        gameplay_state_checksum, ai_state_checksum, decoded_clock.checksum(),
        decoded_notification_checksum);
    const auto decoded_on_action_checksum = on_actions.checksum_state(
        decoded_on_actions.queue, decoded_on_actions.next_invocation_id);
    const auto full_runtime_checksum = runtime_checksum(
        gameplay_state_checksum, ai_state_checksum, decoded_clock.checksum(),
        decoded_notification_checksum, decoded_on_action_checksum);
    if (ver == runtime_v3_version && legacy_runtime_state_checksum != expected_runtime_checksum)
        throw std::runtime_error("save runtime checksum mismatch");
    if (ver >= extension_sections_since_version) {
        if (decoded_on_actions.present) {
            if (full_runtime_checksum != expected_runtime_checksum)
                throw std::runtime_error("save runtime checksum mismatch");
        } else if (decoded_notifications.present) {
            if (extended_runtime_checksum != expected_runtime_checksum)
                throw std::runtime_error("save runtime checksum mismatch");
        } else if (decoded_runtime_checksum != expected_runtime_checksum &&
                   legacy_runtime_state_checksum != expected_runtime_checksum) {
            throw std::runtime_error("save runtime checksum mismatch");
        }
    }

    // Commit only after the full payload, world graph and runtime states have validated.
    world = std::move(decoded);
    clock = decoded_clock;
    gameplay.restore_state(std::move(decoded_gameplay.instances), std::move(decoded_gameplay.log),
                           decoded_gameplay.context_section_present
                               ? decoded_gameplay.next_instance_id : 0u);
    ai.restore_state(std::move(decoded_ai.actions));
    ai.restore_plan_state(std::move(decoded_ai.plans));
    notifications.restore_state(std::move(decoded_notifications.instances),
                                decoded_notifications.next_instance_id);
    on_actions.restore_state(std::move(decoded_on_actions.queue),
                             decoded_on_actions.next_invocation_id);
    return {ver, content_hash, world_hash, expected_checksum,
            decoded_on_actions.present
                ? full_runtime_checksum
                : decoded_notifications.present ? extended_runtime_checksum
                                                : decoded_runtime_checksum};
}
} // namespace thunder
