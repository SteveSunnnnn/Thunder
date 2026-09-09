#pragma once
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>


namespace thunder {

template <typename Tag, typename Rep = std::uint32_t>
class StrongId {
public:
    using rep_type = Rep;
    static constexpr Rep invalid_value = std::numeric_limits<Rep>::max();

    constexpr StrongId() = default;
    explicit constexpr StrongId(Rep value) : value_(value) {}

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != invalid_value; }
    explicit constexpr operator bool() const noexcept { return valid(); }

    friend constexpr bool operator==(StrongId, StrongId) = default;
    friend constexpr auto operator<=>(StrongId, StrongId) = default;

private:
    Rep value_ = invalid_value;
};

struct CountryTag {};
struct AreaTag {};
struct StateTag {};
struct StateRegionTag {};
struct TradeProvinceTag {};
struct ProvinceTag {};
struct LocationTag {};
struct PopTag {};
struct MarketTag {};
struct ModifierNodeTag {};
struct GoodTag {};
struct BuildingTag {};
struct BuildingTypeTag {};
struct NeedProfileTag {};

struct CultureTag {};
struct ReligionTag {};
struct ProfessionTag {};
struct InterestGroupTag {};
struct PoliticalPartyTag {};
struct TechnologyTag {};
struct LawTag {};
struct InstitutionTag {};
struct CompanyTag {};
struct TradeRouteTag {};
struct TreatyTag {};
struct TreatyArticleTag {};
struct TreatyParticipantTag {};
struct ArmyTag {};
struct NavyTag {};
struct MigrationFlowTag {};
struct PowerBlocTag {};
struct DiplomaticPlayTag {};
struct FrontTag {};
struct BattleTag {};
struct ColonyTag {};
struct ShipDesignTag {};
struct InvestmentPoolTag {};
struct ProductionMethodTag {};
struct AssetTag {};
struct DiplomaticRelationTag {};
struct DiplomaticPlayStateTag {};
struct GovernmentTag {};
struct LawEnactmentTag {};
struct WarTag {};
struct ParliamentTag {};
struct SettlementAccountTag {};
struct BankTag {};
struct BankLoanTag {};
struct CulturalAcceptanceTag {};
struct DiplomaticSwayTag {};
struct WarGoalTag {};
struct CommanderTag {};
struct SeaZoneTag {};
struct NavalBattleTag {};

using CountryId = StrongId<CountryTag>;
using AreaId = StrongId<AreaTag>;
using StateId = StrongId<StateTag>;
using StateRegionId = StrongId<StateRegionTag>;
using TradeProvinceId = StrongId<TradeProvinceTag>;
using ProvinceId = StrongId<ProvinceTag>;
using LocationId = StrongId<LocationTag>;
using PopId = StrongId<PopTag>;
using MarketId = StrongId<MarketTag>;
using ModifierNodeId = StrongId<ModifierNodeTag>;
using GoodId = StrongId<GoodTag>;
using BuildingId = StrongId<BuildingTag>;
using BuildingTypeId = StrongId<BuildingTypeTag>;
using NeedProfileId = StrongId<NeedProfileTag>;

using CultureId = StrongId<CultureTag>;
using ReligionId = StrongId<ReligionTag>;
using ProfessionId = StrongId<ProfessionTag>;
using InterestGroupId = StrongId<InterestGroupTag>;
using PoliticalPartyId = StrongId<PoliticalPartyTag>;
using TechnologyId = StrongId<TechnologyTag>;
using LawId = StrongId<LawTag>;
using InstitutionId = StrongId<InstitutionTag>;
using CompanyId = StrongId<CompanyTag>;
using TradeRouteId = StrongId<TradeRouteTag>;
using TreatyId = StrongId<TreatyTag>;
using TreatyArticleId = StrongId<TreatyArticleTag>;
using TreatyParticipantId = StrongId<TreatyParticipantTag>;
using ArmyId = StrongId<ArmyTag>;
using NavyId = StrongId<NavyTag>;
using MigrationFlowId = StrongId<MigrationFlowTag>;
using PowerBlocId = StrongId<PowerBlocTag>;
using DiplomaticPlayId = StrongId<DiplomaticPlayTag>;
using FrontId = StrongId<FrontTag>;
using BattleId = StrongId<BattleTag>;
using ColonyId = StrongId<ColonyTag>;
using ShipDesignId = StrongId<ShipDesignTag>;
using InvestmentPoolId = StrongId<InvestmentPoolTag>;
using ProductionMethodId = StrongId<ProductionMethodTag>;
using AssetId = StrongId<AssetTag>;
using DiplomaticRelationId = StrongId<DiplomaticRelationTag>;
using DiplomaticPlayStateId = StrongId<DiplomaticPlayStateTag>;
using GovernmentId = StrongId<GovernmentTag>;
using LawEnactmentId = StrongId<LawEnactmentTag>;
using WarId = StrongId<WarTag>;
using ParliamentId = StrongId<ParliamentTag>;
using CulturalAcceptanceId = StrongId<CulturalAcceptanceTag>;
using DiplomaticSwayId = StrongId<DiplomaticSwayTag>;
using WarGoalId = StrongId<WarGoalTag>;
using CommanderId = StrongId<CommanderTag>;
using SeaZoneId = StrongId<SeaZoneTag>;
using NavalBattleId = StrongId<NavalBattleTag>;



using SettlementAccountId = StrongId<SettlementAccountTag, std::uint64_t>;
using BankId = StrongId<BankTag>;
using BankLoanId = StrongId<BankLoanTag>;

struct ConstructionProjectTag {};
using ConstructionProjectId = StrongId<ConstructionProjectTag>;

} // namespace thunder

namespace std {
template <typename Tag, typename Rep>
struct hash<thunder::StrongId<Tag, Rep>> {
    constexpr std::size_t operator()(thunder::StrongId<Tag, Rep> id) const noexcept {
        return std::hash<Rep>{}(id.value());
    }
};
} // namespace std
