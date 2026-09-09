#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/world/GeographyStore.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace thunder {

class ResearchSystem;

enum class DiplomaticPlayPhase : std::uint8_t { Opening, Maneuvering, Countdown, War, Resolved };
enum class TreatyKind : std::uint8_t { Alliance, DefensivePact, MilitaryAccess, TransitRights, InvestmentRights, TradeAgreement, CustomsUnion, Custom, PeaceTreaty };


struct TechnologyRecord { CountryId country{}; std::uint64_t key_hash=0; std::uint32_t progress_ppm=0; bool unlocked=false; };
struct LawRecord { CountryId country{}; std::uint64_t key_hash=0; bool enacted=false; };
struct InstitutionRecord { CountryId country{}; std::uint64_t key_hash=0; std::uint16_t level=0; };
struct CompanyRecord { CountryId country{}; std::uint64_t key_hash=0; EconomyAmount cash_milli=0; std::uint32_t productivity_ppm=1'000'000; bool active=true; };
struct TradeRouteRecord { MarketId source{}; MarketId destination{}; GoodId good{}; EconomyAmount quantity_milli=0; std::uint16_t level=1; bool active=true; };
struct OwnershipStakeRecord { CountryId owner_country{}; CompanyId owner_company{}; BuildingId building{}; std::uint32_t share_ppm=0; };
struct TreatyRecord {
    CountryId first{}; CountryId second{}; TreatyKind kind=TreatyKind::Custom; std::uint64_t article_hash=0; bool active=true;
    // Extended treaty content: multilateral + timed + obligations (all data-driven, hashes refer to external script definitions)
    std::uint64_t treaty_key_hash=0; // stable content key (e.g. script_stable_key("treaty.custom_union"))
    std::uint64_t start_tick=0; std::uint64_t expiry_tick=0; // 0 = indefinite, else must be > start
    std::int32_t obligation_milli=0; // generic obligation value (reparations, subsidy) in milli, 0 = none
    bool is_multilateral=false;
};
// Per-treaty articles: a treaty can carry multiple typed articles (trade, access, etc.)
struct TreatyArticleRecord {
    TreatyId treaty{};
    TreatyKind kind=TreatyKind::Custom;
    std::uint64_t article_hash=0; // specific content key within treaty
    std::uint64_t payload_hash=0; // obligation payload (e.g. state transfer, market open)
    bool active=true;
};
struct TreatyParticipantRecord {
    TreatyId treaty{};
    CountryId country{};
    std::uint8_t role=0; // 0 member, 1 leader/guarantor
};
struct ArmyRecord { CountryId country{}; StateId location{}; std::uint32_t manpower=0; std::uint32_t organization_ppm=1'000'000; };
enum class NavalMission : std::uint8_t {

    None = 0,
    Patrol = 1,
    RaidSupplyLines = 2,
    BlockadePort = 3,
    FleetInterception = 4
};

struct NavyRecord {
    CountryId country{};
    StateId location{};
    std::uint32_t sailors = 0;
    std::uint32_t strength_ppm = 1'000'000;
    ShipDesignId design{};
    NavalMission mission = NavalMission::None;
    SeaZoneId assigned_zone{};
};

struct SeaZoneRecord {
    std::uint64_t key_hash = 0;
    CountryId controller{};
    std::uint32_t blockade_efficiency_ppm = 0;
};

struct NavalBattleRecord {
    SeaZoneId zone{};
    NavyId attacker_navy{};
    NavyId defender_navy{};
    std::int32_t progress_milli = 0;
    bool resolved = false;
};

struct MigrationFlowRecord { ProvinceId source{}; ProvinceId destination{}; PopulationCount population=0; std::uint16_t weeks_remaining=0; };
struct InterestGroupRecord { CountryId country{}; std::uint64_t key_hash=0; std::uint32_t clout_ppm=0; std::int32_t approval_milli=0; };
struct PoliticalPartyRecord { CountryId country{}; std::uint64_t key_hash=0; std::uint32_t support_ppm=0; };
struct PowerBlocRecord { CountryId leader{}; std::uint64_t key_hash=0; std::uint32_t cohesion_ppm=1'000'000; };
struct DiplomaticPlayRecord { CountryId initiator{}; CountryId target{}; DiplomaticPlayPhase phase=DiplomaticPlayPhase::Opening; std::uint64_t war_goal_hash=0; };
struct FrontRecord { CountryId first{}; CountryId second{}; StateId state{}; std::int32_t progress_milli=0; };
struct BattleRecord { FrontId front{}; std::uint32_t attackers=0; std::uint32_t defenders=0; std::int32_t progress_milli=0; bool resolved=false; };
struct ColonyRecord { CountryId country{}; ProvinceId province{}; std::uint32_t progress_ppm=0; };
struct ShipDesignRecord { CountryId country{}; std::uint64_t hull_hash=0; std::uint64_t modules_hash=0; std::uint32_t combat_power_milli=0; EconomyAmount build_cost_milli=0; };
struct InvestmentPoolRecord { CountryId country{}; EconomyAmount cash_milli=0; EconomyAmount weekly_contribution_milli=0; };

// Operational strategy-system state. These records turn the earlier generic
// skeletons into deterministic state machines while keeping the data-oriented
// storage model used by save/checksum code.
struct DiplomaticRelationRecord {
    CountryId first{};
    CountryId second{};
    std::int32_t relation_milli = 0;       // -100000 hostile .. +100000 friendly
    std::uint32_t trust_ppm = 500'000;
    std::uint32_t tension_ppm = 0;
};
struct DiplomaticPlayStateRecord {
    DiplomaticPlayId play{};
    std::uint16_t weeks_in_phase = 0;
    std::uint32_t escalation_ppm = 0;
};
struct GovernmentRecord {
    CountryId country{};
    std::uint32_t legitimacy_ppm = 500'000;
    std::int32_t stability_milli = 0;
};
struct LawEnactmentRecord {
    CountryId country{};
    std::uint64_t law_hash = 0;
    std::uint32_t progress_ppm = 0;
    std::uint32_t support_ppm = 500'000;
    bool active = true;
    bool passed = false;
};
struct WarRecord {
    DiplomaticPlayId play{};
    CountryId attacker{};
    CountryId defender{};
    std::int32_t war_score_milli = 0;
    std::uint16_t weeks = 0;
    bool active = true;
};

enum class MigrationPolicy : std::uint8_t {
    ClosedBorders = 0,    // 闭关锁国: 封锁国界，彻底禁止跨国移民出入
    MigrationControls = 1,// 移民管制: 仅允许被接纳文化/同盟国人口跨国移民
    OpenBorders = 2       // 自由移民: 允许一切文化人口自由跨国移民，提供吸引力加成
};

enum class CulturalAcceptanceLevel : std::uint8_t {
    Discriminated = 0,    // 歧视文化: 无法跨国移民入籍（在移民管制下），省份吸引力受惩罚
    Accepted = 1,         // 接纳文化: 享有完全跨国移民权与平等公民待遇
    Primary = 2           // 主体文化: 国家核心主体文化
};

struct CulturalAcceptanceRecord {
    CountryId country{};
    CultureId culture{};
    CulturalAcceptanceLevel level = CulturalAcceptanceLevel::Discriminated;
};

struct ParliamentRecord {
    CountryId country{};
    std::uint64_t power_distribution_law_hash = 0;
    bool elections_enabled = false;
    std::uint16_t election_interval_weeks = 208;
    std::uint16_t weeks_to_next_election = 208;
    std::uint32_t total_seats = 100;
    std::uint32_t ruling_party_seats = 100;
    std::uint32_t opposition_seats = 0;
    std::uint64_t ruling_party_hash = 0;
    std::uint32_t pop_vote_weight_ppm = 1'000'000;
    std::uint32_t ig_clout_weight_ppm = 0;
    MigrationPolicy migration_policy = MigrationPolicy::OpenBorders;
};



enum class WarGoalType : std::uint8_t {
    ConquerState = 0,
    LiberateCountry = 1,
    OpenMarket = 2,
    RegimeChange = 3,
    WarReparations = 4
};

enum class SwayOfferType : std::uint8_t {
    DiplomaticObligation = 0,
    ConcessionTreaty = 1,
    StateTransfer = 2
};

enum class UnitType : std::uint8_t {
    LineInfantry = 0,
    Cavalry = 1,
    Artillery = 2,
    Skirmisher = 3
};

enum class CommanderTrait : std::uint8_t {
    None = 0,
    DefensiveMaster = 1,
    OffensiveExpert = 2,
    LogisticsMaster = 3,
    AggressivePusher = 4
};

struct DiplomaticSwayRecord {
    DiplomaticPlayId play{};
    CountryId sponsor{};
    CountryId target_country{};
    SwayOfferType offer_type = SwayOfferType::DiplomaticObligation;
    std::uint64_t payload_hash = 0;
    bool accepted = false;
};

struct WarGoalRecord {
    DiplomaticPlayId play{};
    CountryId holder{};
    CountryId target{};
    WarGoalType goal_type = WarGoalType::ConquerState;
    StateId state_target{};
    bool primary = true;
    bool enforced = false;
};

struct CommanderRecord {
    CountryId country{};
    StateId location{};
    CommanderTrait trait = CommanderTrait::None;
    std::uint8_t skill_level = 1;
};

struct ProvinceAttraction {
    ProvinceId province{};
    std::int32_t score = 0;
    std::uint32_t available_jobs = 0;
    std::uint32_t unemployed = 0;
    std::uint32_t arable_land_slots = 0;
    EconomyPrice avg_wage = 0;
    std::int32_t discrimination_penalty = 0;
    std::int32_t policy_bonus = 0;
};

class World;

class GrandStrategyStore {
public:
    /// Sentinel meaning "derive the week from the internal counter". Callers
    /// that own a GameClock (ThunderEngine) should pass the real week instead so
    /// the RNG stream stays aligned across save/load.
    static constexpr std::uint64_t automatic_weekly_tick =
        std::numeric_limits<std::uint64_t>::max();

#define THUNDER_GS_ADD(NAME, ID, TYPE) ID add_##NAME(TYPE record); [[nodiscard]] std::span<const TYPE> NAME##s() const noexcept { return NAME##s_; }
    THUNDER_GS_ADD(technology, TechnologyId, TechnologyRecord)
    THUNDER_GS_ADD(law, LawId, LawRecord)
    THUNDER_GS_ADD(institution, InstitutionId, InstitutionRecord)
    THUNDER_GS_ADD(company, CompanyId, CompanyRecord)
    THUNDER_GS_ADD(trade_route, TradeRouteId, TradeRouteRecord)
    THUNDER_GS_ADD(ownership_stake, StrongId<struct OwnershipStakeTag>, OwnershipStakeRecord)
    THUNDER_GS_ADD(treaty, TreatyId, TreatyRecord)
    THUNDER_GS_ADD(treaty_article, TreatyArticleId, TreatyArticleRecord)
    THUNDER_GS_ADD(treaty_participant, TreatyParticipantId, TreatyParticipantRecord)
    THUNDER_GS_ADD(army, ArmyId, ArmyRecord)
    THUNDER_GS_ADD(navy, NavyId, NavyRecord)
    THUNDER_GS_ADD(migration_flow, MigrationFlowId, MigrationFlowRecord)
    THUNDER_GS_ADD(interest_group, InterestGroupId, InterestGroupRecord)
    THUNDER_GS_ADD(political_party, PoliticalPartyId, PoliticalPartyRecord)
    THUNDER_GS_ADD(power_bloc, PowerBlocId, PowerBlocRecord)
    THUNDER_GS_ADD(diplomatic_play, DiplomaticPlayId, DiplomaticPlayRecord)
    THUNDER_GS_ADD(front, FrontId, FrontRecord)
    THUNDER_GS_ADD(battle, BattleId, BattleRecord)
    THUNDER_GS_ADD(colony, ColonyId, ColonyRecord)
    THUNDER_GS_ADD(ship_design, ShipDesignId, ShipDesignRecord)
    THUNDER_GS_ADD(investment_pool, InvestmentPoolId, InvestmentPoolRecord)
    THUNDER_GS_ADD(diplomatic_relation, DiplomaticRelationId, DiplomaticRelationRecord)
    THUNDER_GS_ADD(diplomatic_play_state, DiplomaticPlayStateId, DiplomaticPlayStateRecord)
    THUNDER_GS_ADD(government, GovernmentId, GovernmentRecord)
    THUNDER_GS_ADD(law_enactment, LawEnactmentId, LawEnactmentRecord)
    THUNDER_GS_ADD(war, WarId, WarRecord)
    THUNDER_GS_ADD(parliament, ParliamentId, ParliamentRecord)
    THUNDER_GS_ADD(cultural_acceptance, CulturalAcceptanceId, CulturalAcceptanceRecord)
    THUNDER_GS_ADD(diplomatic_sway, DiplomaticSwayId, DiplomaticSwayRecord)
    THUNDER_GS_ADD(war_goal, WarGoalId, WarGoalRecord)
    THUNDER_GS_ADD(commander, CommanderId, CommanderRecord)
    THUNDER_GS_ADD(sea_zone, SeaZoneId, SeaZoneRecord)
    THUNDER_GS_ADD(naval_battle, NavalBattleId, NavalBattleRecord)
#undef THUNDER_GS_ADD

    [[nodiscard]] std::span<ArmyRecord> armys_mut() noexcept { return armys_; }
    [[nodiscard]] std::span<NavyRecord> navys_mut() noexcept { return navys_; }
    [[nodiscard]] std::span<TreatyRecord> treatys_mut() noexcept { return treatys_; }
    [[nodiscard]] std::span<TradeRouteRecord> trade_routes_mut() noexcept { return trade_routes_; }
    [[nodiscard]] std::span<InstitutionRecord> institutions_mut() noexcept { return institutions_; }
    [[nodiscard]] std::span<TechnologyRecord> technologys_mut() noexcept { return technologys_; }
    [[nodiscard]] std::span<DiplomaticRelationRecord> diplomatic_relations_mut() noexcept { return diplomatic_relations_; }
    [[nodiscard]] std::span<FrontRecord> fronts_mut() noexcept { return fronts_; }
    [[nodiscard]] std::span<WarRecord> wars_mut() noexcept { return wars_; }

    void run_institutions_weekly(World& world);
    void run_tech_spread_weekly(World& world, std::uint64_t weekly_tick = automatic_weekly_tick);
    void run_state_resistance_weekly(World& world);





    DiplomaticRelationId ensure_diplomatic_relation(CountryId first, CountryId second);
    [[nodiscard]] std::int32_t relation_milli(CountryId first, CountryId second) const noexcept;
    void adjust_relation(CountryId first, CountryId second, std::int32_t delta_milli);
    [[nodiscard]] bool has_active_treaty(CountryId first, CountryId second, TreatyKind kind) const noexcept;
    [[nodiscard]] bool has_active_treaty_article(TreatyId treaty, TreatyKind kind) const noexcept;
    TreatyId create_treaty(CountryId first, CountryId second, TreatyKind kind, std::uint64_t article_hash = 0);
    TreatyId create_treaty_ex(CountryId first, CountryId second, TreatyKind kind, std::uint64_t article_hash,
                              std::uint64_t start_tick, std::uint64_t expiry_tick, std::int32_t obligation_milli, std::uint64_t treaty_key_hash);
    TreatyArticleId add_treaty_article(TreatyId treaty, TreatyKind kind, std::uint64_t article_hash, std::uint64_t payload_hash);
    TreatyParticipantId add_treaty_participant(TreatyId treaty, CountryId country, std::uint8_t role = 0);
    [[nodiscard]] std::span<const TreatyArticleRecord> treaty_articles(TreatyId treaty) const noexcept;
    [[nodiscard]] std::span<const TreatyParticipantRecord> treaty_participants(TreatyId treaty) const noexcept;
    bool break_treaty(TreatyId treaty);
    void expire_treaties(std::uint64_t current_tick);

    void add_investment_pool_funds(CountryId country, EconomyAmount amount_milli);
    void add_investment_pool_funds(InvestmentPoolId pool, EconomyAmount amount_milli);
    // R11: company operating account. Companies receive their ownership share
    // of building dividends and spend it on their own expansions.
    [[nodiscard]] std::span<CompanyRecord> companys_mut() noexcept { return companys_; }
    [[nodiscard]] EconomyAmount company_cash(CompanyId company) const noexcept;
    void add_company_cash(CompanyId company, EconomyAmount delta_milli) noexcept;
    // Withdraws up to amount_milli from the country's investment pool and
    // returns what was actually taken (0 when no pool exists). Construction
    // and building-credit draws go through this so pool spending stays
    // bounded by real accumulated funds.
    [[nodiscard]] EconomyAmount withdraw_investment_pool_funds(CountryId country, EconomyAmount amount_milli);
    [[nodiscard]] EconomyAmount investment_pool_cash(CountryId country) const noexcept;

    DiplomaticPlayId start_diplomatic_play(CountryId initiator, CountryId target, std::uint64_t war_goal_hash);
    bool back_down(DiplomaticPlayId play, CountryId country);
    FrontId create_front_for_play(DiplomaticPlayId play, StateId state);

    DiplomaticSwayId sway_nation(DiplomaticPlayId play, CountryId sponsor, CountryId target_country, SwayOfferType offer, std::uint64_t payload_hash = 0);
    bool accept_sway(DiplomaticSwayId sway);
    bool back_down_diplomatic_play(DiplomaticPlayId play, CountryId backer);
    void enforce_peace_treaty(DiplomaticPlayId play, CountryId winner, CountryId loser, World* world = nullptr);

    void assign_navy_mission(NavyId navy, NavalMission mission, SeaZoneId zone);
    [[nodiscard]] std::uint32_t sea_zone_blockade_level(SeaZoneId zone) const noexcept;

    LawEnactmentId start_law_enactment(CountryId country, std::uint64_t law_hash);
    [[nodiscard]] const GovernmentRecord* government_for(CountryId country) const noexcept;

    void configure_parliament_rules(CountryId country, std::uint64_t law_hash, bool elections_enabled,
                                    std::uint32_t pop_weight_ppm = 1'000'000u, std::uint32_t ig_clout_ppm = 0u,
                                    std::uint16_t interval_weeks = 208u);
    [[nodiscard]] bool elections_enabled(CountryId country) const noexcept;
    void set_migration_policy(CountryId country, MigrationPolicy policy);
    [[nodiscard]] MigrationPolicy migration_policy(CountryId country) const noexcept;

    [[nodiscard]] const ParliamentRecord* parliament_for(CountryId country) const noexcept;
    ParliamentRecord* parliament_for_mut(CountryId country) noexcept;
    void run_parliamentary_election(CountryId country);


    void set_cultural_acceptance(CountryId country, CultureId culture, CulturalAcceptanceLevel level);
    [[nodiscard]] CulturalAcceptanceLevel cultural_acceptance(CountryId country, CultureId culture) const noexcept;
    [[nodiscard]] bool is_culture_accepted(CountryId country, CultureId culture) const noexcept;

    [[nodiscard]] ProvinceAttraction calculate_province_attraction(const World& world, ProvinceId province, CultureId culture = CultureId{}) const noexcept;
    MigrationFlowId start_migration_flow(ProvinceId source, ProvinceId destination, PopulationCount population, std::uint16_t weeks = 4);
    MigrationFlowId start_mass_migration(ProvinceId source, ProvinceId destination, PopulationCount population, std::uint16_t weeks = 8);
    void update_migration_flows(World& world);
    // R7: weekly autonomous domestic migration. Unemployment-heavy provinces
    // push small flows toward same-country vacancy provinces (capped, stable
    // order). Runs after update_migration_flows so new flows age next week.
    void generate_autonomous_migration_flows(World& world, const EconomyDefinitions& definitions);


    void run_military_weekly(World& world);
    void run_weekly_reference_tick();
    void run_weekly_reference_tick(World& world,
                                   bool include_legacy_technology_spread = true,
                                   std::uint64_t weekly_tick = automatic_weekly_tick);
    [[nodiscard]] bool validate(std::size_t countries, std::size_t markets, std::size_t provinces,
                                std::size_t states, std::size_t buildings, std::size_t goods) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    friend class ResearchSystem;

    GovernmentId ensure_government(CountryId country);
    ParliamentId ensure_parliament(CountryId country);
    void run_politics_weekly();
    void run_diplomacy_weekly();
    void run_warfare_weekly(World* world = nullptr);
    void run_naval_weekly();
    [[nodiscard]] WarId war_for_play(DiplomaticPlayId play) const noexcept;

    std::vector<TechnologyRecord> technologys_;
    std::vector<LawRecord> laws_;
    std::vector<InstitutionRecord> institutions_;
    std::vector<CompanyRecord> companys_;
    std::vector<TradeRouteRecord> trade_routes_;
    std::vector<OwnershipStakeRecord> ownership_stakes_;
    std::vector<TreatyRecord> treatys_;
    std::vector<TreatyArticleRecord> treaty_articles_;
    std::vector<TreatyParticipantRecord> treaty_participants_;
    mutable std::vector<TreatyArticleRecord> treaty_articles_scratch_;
    mutable std::vector<TreatyParticipantRecord> treaty_participants_scratch_;
    std::vector<ArmyRecord> armys_;
    std::vector<NavyRecord> navys_;
    std::vector<MigrationFlowRecord> migration_flows_;
    std::vector<InterestGroupRecord> interest_groups_;
    std::vector<PoliticalPartyRecord> political_partys_;
    std::vector<PowerBlocRecord> power_blocs_;
    std::vector<DiplomaticPlayRecord> diplomatic_plays_;
    std::vector<FrontRecord> fronts_;
    std::vector<BattleRecord> battles_;
    std::vector<ColonyRecord> colonys_;
    std::vector<ShipDesignRecord> ship_designs_;
    std::vector<InvestmentPoolRecord> investment_pools_;
    std::vector<DiplomaticRelationRecord> diplomatic_relations_;
    std::vector<DiplomaticPlayStateRecord> diplomatic_play_states_;
    std::vector<GovernmentRecord> governments_;
    std::vector<LawEnactmentRecord> law_enactments_;
    std::vector<WarRecord> wars_;
    std::vector<ParliamentRecord> parliaments_;
    std::vector<CulturalAcceptanceRecord> cultural_acceptances_;
    std::vector<DiplomaticSwayRecord> diplomatic_sways_;
    std::vector<WarGoalRecord> war_goals_;
    std::vector<CommanderRecord> commanders_;
    std::vector<SeaZoneRecord> sea_zones_;
    std::vector<NavalBattleRecord> naval_battles_;

    /// Week counter feeding the technology-spread roll. Mirrors
    /// ResearchSystem::weekly_ticks_: ThunderEngine pins it to the persisted
    /// GameClock week so a reloaded save keeps drawing new samples, and the
    /// automatic fallback keeps advancing it for callers that omit it.
    std::uint64_t weekly_ticks_ = 0u;
};

} // namespace thunder
