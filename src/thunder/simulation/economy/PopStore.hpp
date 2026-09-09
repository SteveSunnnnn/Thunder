#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include "thunder/foundation/memory/SlotPool.hpp"
#include "thunder/foundation/memory/SoaCompactor.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct PopInit {
    MarketId market;
    PopulationCount size = 0;
    BuildingId employer{};
    NeedProfileId need_profile{};
    ProvinceId province{};
    CultureId culture{};
    ReligionId religion{};
    ProfessionId profession{};
    InterestGroupId interest_group{};
    std::uint16_t literacy_permyriad = 0;
    std::uint16_t qualification_permyriad = 0;
    std::int32_t wealth_milli = 0;
    std::uint32_t political_strength_milli = 0;
    PopulationCount radicals = 0;
    PopulationCount loyalists = 0;
};

// Physically isolated SoA hot data scanned weekly by EconomySystem (40 bytes/row)
struct PopHotData {
    std::vector<MarketId> markets;
    std::vector<PopulationCount> population;
    std::vector<PopulationCount> employed;
    std::vector<BuildingId> employers;
    std::vector<NeedProfileId> need_profiles;
    std::vector<EconomyAmount> income_milli;
    std::vector<EconomyAmount> cash_milli;    // Persistent wealth/savings balance
    std::vector<std::int32_t> sol_milli;

    void reserve(std::size_t count);
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
};

// Out-of-line cold data for social, cultural, political and demographic states
struct PopColdData {
    std::vector<ProvinceId> provinces;
    std::vector<CultureId> cultures;
    std::vector<ReligionId> religions;
    std::vector<ProfessionId> professions;
    std::vector<InterestGroupId> interest_groups;
    std::vector<std::uint16_t> literacy_permyriad;
    std::vector<std::uint16_t> qualification_permyriad;
    std::vector<std::int32_t> wealth_milli;
    std::vector<std::uint32_t> political_strength_milli;
    std::vector<PopulationCount> radicals;
    std::vector<PopulationCount> loyalists;
    // Fractional persons carried between weekly growth passes (milli-persons).
    // Accumulating the remainder keeps small POPs growing at the exact weekly
    // rate instead of rounding to zero; deterministic, no RNG.
    std::vector<std::int32_t> growth_progress_milli;

    void reserve(std::size_t count);
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
};

class PopStore {
public:
    void reserve(std::size_t count);
    PopId create(PopInit init);
    void destroy(PopId id);
    CompactionMap compact();

    [[nodiscard]] std::size_t size() const noexcept { return hot_.markets.size(); }
    [[nodiscard]] MarketId market(PopId id) const;
    [[nodiscard]] ProvinceId province(PopId id) const;
    [[nodiscard]] PopulationCount population(PopId id) const;
    [[nodiscard]] PopulationCount employed(PopId id) const;
    [[nodiscard]] BuildingId employer(PopId id) const;
    [[nodiscard]] NeedProfileId need_profile(PopId id) const;
    [[nodiscard]] EconomyAmount income(PopId id) const;
    [[nodiscard]] EconomyAmount cash(PopId id) const;
    [[nodiscard]] std::int32_t standard_of_living_milli(PopId id) const;
    [[nodiscard]] CultureId culture(PopId id) const;
    [[nodiscard]] ReligionId religion(PopId id) const;
    [[nodiscard]] ProfessionId profession(PopId id) const;
    [[nodiscard]] InterestGroupId interest_group(PopId id) const;
    [[nodiscard]] std::uint16_t literacy_permyriad(PopId id) const;
    [[nodiscard]] std::uint16_t qualification_permyriad(PopId id) const;
    [[nodiscard]] std::int32_t wealth_milli(PopId id) const;
    [[nodiscard]] std::uint32_t political_strength_milli(PopId id) const;
    [[nodiscard]] PopulationCount radicals(PopId id) const;
    [[nodiscard]] PopulationCount loyalists(PopId id) const;
    [[nodiscard]] std::int32_t growth_progress_milli(PopId id) const;

    void set_market(PopId id, MarketId value);
    void set_province(PopId id, ProvinceId value);
    void set_population(PopId id, PopulationCount value);
    void set_employer(PopId id, BuildingId value);
    void set_employed(PopId id, PopulationCount value);
    void set_income(PopId id, EconomyAmount value);
    void set_cash(PopId id, EconomyAmount value);
    void add_cash(PopId id, EconomyAmount delta);
    void set_standard_of_living_milli(PopId id, std::int32_t value);
    void set_culture(PopId id, CultureId value);
    void set_religion(PopId id, ReligionId value);
    void set_profession(PopId id, ProfessionId value);
    void set_interest_group(PopId id, InterestGroupId value);
    void set_literacy_permyriad(PopId id, std::uint16_t value);
    void set_qualification_permyriad(PopId id, std::uint16_t value);
    void set_wealth_milli(PopId id, std::int32_t value);
    void set_political_strength_milli(PopId id, std::uint32_t value);
    void set_radicals(PopId id, PopulationCount value);
    void set_loyalists(PopId id, PopulationCount value);
    void add_radicals(PopId id, std::int64_t delta);
    void add_loyalists(PopId id, std::int64_t delta);

    [[nodiscard]] std::uint64_t market_membership_revision() const noexcept { return market_membership_revision_; }
    [[nodiscard]] std::uint64_t province_membership_revision() const noexcept { return province_membership_revision_; }
    [[nodiscard]] std::span<const MarketId> markets() const noexcept { return hot_.markets; }
    [[nodiscard]] std::span<const ProvinceId> provinces() const noexcept { return cold_.provinces; }
    [[nodiscard]] std::span<const PopulationCount> populations() const noexcept { return hot_.population; }
    [[nodiscard]] std::span<PopulationCount> populations_mut() noexcept { return hot_.population; }
    [[nodiscard]] std::span<PopulationCount> employed_mut() noexcept { return hot_.employed; }

    [[nodiscard]] std::span<const PopulationCount> employed_all() const noexcept { return hot_.employed; }
    [[nodiscard]] std::span<const BuildingId> employers() const noexcept { return hot_.employers; }
    [[nodiscard]] std::span<BuildingId> employers_mut() noexcept { return hot_.employers; }
    [[nodiscard]] std::span<const NeedProfileId> need_profiles() const noexcept { return hot_.need_profiles; }
    [[nodiscard]] std::span<EconomyAmount> incomes_mut() noexcept { return hot_.income_milli; }
    [[nodiscard]] std::span<const EconomyAmount> incomes() const noexcept { return hot_.income_milli; }
    [[nodiscard]] std::span<EconomyAmount> cash_mut() noexcept { return hot_.cash_milli; }
    [[nodiscard]] std::span<const EconomyAmount> cash_all() const noexcept { return hot_.cash_milli; }
    [[nodiscard]] std::span<std::int32_t> sol_mut() noexcept { return hot_.sol_milli; }
    [[nodiscard]] std::span<const std::int32_t> sol_all() const noexcept { return hot_.sol_milli; }
    [[nodiscard]] std::span<const CultureId> cultures() const noexcept { return cold_.cultures; }
    [[nodiscard]] std::span<const ReligionId> religions() const noexcept { return cold_.religions; }
    [[nodiscard]] std::span<const ProfessionId> professions() const noexcept { return cold_.professions; }
    [[nodiscard]] std::span<const InterestGroupId> interest_groups() const noexcept { return cold_.interest_groups; }
    [[nodiscard]] std::span<const std::uint16_t> literacy_all() const noexcept { return cold_.literacy_permyriad; }
    [[nodiscard]] std::span<const std::uint16_t> qualifications_all() const noexcept { return cold_.qualification_permyriad; }
    [[nodiscard]] std::span<std::uint16_t> qualifications_mut() noexcept { return cold_.qualification_permyriad; }
    [[nodiscard]] std::span<const std::int32_t> wealth_all() const noexcept { return cold_.wealth_milli; }
    [[nodiscard]] std::span<const std::uint32_t> political_strength_all() const noexcept { return cold_.political_strength_milli; }
    [[nodiscard]] std::span<const PopulationCount> radicals_all() const noexcept { return cold_.radicals; }
    [[nodiscard]] std::span<PopulationCount> radicals_mut() noexcept { return cold_.radicals; }
    [[nodiscard]] std::span<const PopulationCount> loyalists_all() const noexcept { return cold_.loyalists; }
    [[nodiscard]] std::span<PopulationCount> loyalists_mut() noexcept { return cold_.loyalists; }
    [[nodiscard]] std::span<const std::int32_t> growth_progress_all() const noexcept { return cold_.growth_progress_milli; }
    [[nodiscard]] std::span<std::int32_t> growth_progress_mut() noexcept { return cold_.growth_progress_milli; }

    [[nodiscard]] const PopHotData& hot_data() const noexcept { return hot_; }
    [[nodiscard]] const PopColdData& cold_data() const noexcept { return cold_; }
    [[nodiscard]] std::size_t hot_memory_bytes() const noexcept { return hot_.memory_bytes(); }
    [[nodiscard]] std::size_t cold_memory_bytes() const noexcept { return cold_.memory_bytes(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return hot_memory_bytes() + cold_memory_bytes(); }
    [[nodiscard]] std::uint64_t checksum() const noexcept;

    [[nodiscard]] SlotPool& slot_pool() noexcept { return slot_pool_; }
    [[nodiscard]] const SlotPool& slot_pool() const noexcept { return slot_pool_; }

private:
    [[nodiscard]] std::size_t index(PopId id) const;
    PopHotData hot_;
    PopColdData cold_;
    SlotPool slot_pool_;
    std::uint64_t market_membership_revision_ = 0u;
    std::uint64_t province_membership_revision_ = 0u;
};

} // namespace thunder
