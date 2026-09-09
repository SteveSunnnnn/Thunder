#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace thunder {

class World;

struct RecipeFlow; // defined in thunder/simulation/economy/EconomyDefinitions.hpp

enum class ConstructionKind : std::uint8_t {
    ExpandBuilding = 0,
    UpgradeProductionMethod = 1,
    ConstructMonument = 2
};

struct ConstructionProjectInit {
    CountryId country{};
    ProvinceId province{};
    BuildingId target_building{};
    ConstructionKind kind = ConstructionKind::ExpandBuilding;
    ProductionMethodId target_pm{};
    std::uint64_t monument_key_hash = 0;
    std::uint32_t total_points_required = 100;
    EconomyAmount total_cost_milli = 0;
};

struct ConstructionProjectRecord {
    ConstructionProjectId id{};
    CountryId country{};
    ProvinceId province{};
    BuildingId target_building{};
    ConstructionKind kind = ConstructionKind::ExpandBuilding;
    ProductionMethodId target_pm{};
    std::uint64_t monument_key_hash = 0;
    std::uint32_t progress_points = 0;
    std::uint32_t total_points_required = 100;
    std::uint32_t weekly_progress_ppm = 0;
    EconomyAmount total_cost_milli = 0;
    EconomyAmount paid_cost_milli = 0;
    bool paused = false;
    std::uint32_t priority = 0;

    [[nodiscard]] double progress_ratio() const noexcept {
        if (total_points_required == 0) return 1.0;
        return static_cast<double>(progress_points) / static_cast<double>(total_points_required);
    }
};

class ConstructionStore {
public:
    ConstructionProjectId enqueue(ConstructionProjectInit init);
    ConstructionProjectId enqueue_expansion(CountryId country, BuildingId building,
                                            std::uint32_t points = 100,
                                            EconomyAmount cost_milli = 0);
    ConstructionProjectId enqueue_pm_upgrade(CountryId country, BuildingId building,
                                             ProductionMethodId target_pm,
                                             std::uint32_t points = 50,
                                             EconomyAmount cost_milli = 0);
    ConstructionProjectId enqueue_monument(CountryId country, ProvinceId province,
                                           std::string_view monument_key,
                                           std::uint32_t points = 500,
                                           EconomyAmount cost_milli = 0);

    bool cancel(ConstructionProjectId id);
    bool set_paused(ConstructionProjectId id, bool paused);
    bool move_up(ConstructionProjectId id);
    bool move_down(ConstructionProjectId id);

    [[nodiscard]] std::size_t size() const noexcept { return projects_.size(); }
    [[nodiscard]] const ConstructionProjectRecord* find(ConstructionProjectId id) const noexcept;
    [[nodiscard]] std::span<const ConstructionProjectRecord> projects() const noexcept { return projects_; }
    [[nodiscard]] std::vector<ConstructionProjectId> country_queue(CountryId country) const;

    // Returns transition progress in ppm (0 to 1,000,000) for a building undergoing PM retooling
    [[nodiscard]] std::uint32_t pm_transition_progress_ppm(BuildingId building) const noexcept;

    [[nodiscard]] EconomyAmount country_weekly_construction_capacity(CountryId country, const World& world) const;
    // Weekly queue execution. The construction basket (materials per point)
    // is passed explicitly so the goods draw stays on the caller's content
    // revision: materials are drawn from the project's market inventory and
    // paid to market clearing; shortages throttle progress (Leontief).
    JobDispatchStats tick_weekly(World& world, std::span<const RecipeFlow> construction_basket);
    JobDispatchStats tick_weekly(World& world);

    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] bool validate(const World& world) const;
    void clear() noexcept;

    // Restore for save deserialization
    void restore_project(const ConstructionProjectRecord& rec);

private:
    std::vector<ConstructionProjectRecord> projects_;
    std::uint32_t next_id_ = 0;
};

} // namespace thunder
