#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/economy/EconomySystem.hpp"
#include "thunder/simulation/economy/BuildingStore.hpp"
#include "thunder/simulation/economy/PopStore.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/runtime/save/AsyncSavePipeline.hpp"
#include "thunder/runtime/save/SaveGame.hpp"
#include <array>
#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/simulation/gameplay/NotificationRuntime.hpp"
#include "thunder/simulation/gameplay/OnActionRuntime.hpp"
#include "thunder/simulation/ai/UtilityAi.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace thunder;

static void test_pop_building_cold_hot_separation() {
    PopStore pops;
    BuildingStore buildings;

    pops.reserve(100);
    buildings.reserve(100);

    const auto pop_id = pops.create({
        .market = MarketId{1},
        .size = 5000u,
        .employer = BuildingId{2},
        .need_profile = NeedProfileId{3},
        .province = ProvinceId{4},
        .culture = CultureId{5},
        .religion = ReligionId{6},
        .profession = ProfessionId{7},
        .interest_group = InterestGroupId{8},
        .literacy_permyriad = 4500u,
        .qualification_permyriad = 2000u,
        .wealth_milli = 12500,
        .political_strength_milli = 3000
    });

    const auto building_id = buildings.create({
        .market = MarketId{1},
        .type = BuildingTypeId{2},
        .level = 3u,
        .wage_offer_milli = 1200,
        .cash_milli = 50000,
        .province = ProvinceId{4},
        .production_method = ProductionMethodId{5}
    });

    // Check sizes
    assert(pops.size() == 1);
    assert(buildings.size() == 1);

    // Check hot data access
    assert(pops.market(pop_id) == MarketId{1});
    assert(pops.population(pop_id) == 5000u);
    assert(pops.employer(pop_id) == BuildingId{2});
    assert(pops.need_profile(pop_id) == NeedProfileId{3});

    // Check cold data access
    assert(pops.province(pop_id) == ProvinceId{4});
    assert(pops.culture(pop_id) == CultureId{5});
    assert(pops.religion(pop_id) == ReligionId{6});
    assert(pops.profession(pop_id) == ProfessionId{7});
    assert(pops.interest_group(pop_id) == InterestGroupId{8});
    assert(pops.literacy_permyriad(pop_id) == 4500u);

    // Check building hot/cold access
    assert(buildings.market(building_id) == MarketId{1});
    assert(buildings.type(building_id) == BuildingTypeId{2});
    assert(buildings.level(building_id) == 3u);
    assert(buildings.wage_offer(building_id) == 1200);
    assert(buildings.province(building_id) == ProvinceId{4});

    // Check memory separation
    assert(pops.hot_memory_bytes() > 0);
    assert(pops.cold_memory_bytes() > 0);
    assert(pops.hot_memory_bytes() < pops.memory_bytes());
    assert(buildings.hot_memory_bytes() > 0);
    assert(buildings.cold_memory_bytes() > 0);
    assert(buildings.hot_memory_bytes() < buildings.memory_bytes());

    // Check checksum computation
    assert(pops.checksum() != 0);
    assert(buildings.checksum() != 0);

    std::cout << "[PASS] PopStore & BuildingStore hot/cold data physical separation\n";
}

static void test_async_save_pipeline() {
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1000});
    const auto cloth = definitions.add_good({"cloth", 2000});
    const std::array<NeedFlow, 2> needs{{{grain, 5000}, {cloth, 1500}}};
    const auto need_prof = definitions.add_need_profile("peasant", needs);

    const std::array<RecipeFlow, 0> none{};
    const std::array<RecipeFlow, 1> farm_out{{{grain, 10000}}};
    const auto farm_type = definitions.add_building_type("farm", 1000, none, farm_out);

    World world;
    world.countries.create({"GBR", 100000.0, 5000.0, 1000.0, 0.20});
    world.markets.resize(1, definitions);
    world.markets.set_owner(MarketId{0}, CountryId{0});

    const auto building = world.buildings.create({
        .market = MarketId{0},
        .type = farm_type,
        .level = 2,
        .wage_offer_milli = 1000,
        .cash_milli = 10000
    });

    world.pops.create({
        .market = MarketId{0},
        .size = 2000u,
        .employer = building,
        .need_profile = need_prof
    });

    GameClock clock;
    ScriptRegistry registry = ScriptRegistry::make_builtin();
    ScriptedGameplayRuntime gameplay(registry);
    UtilityAiEngine ai(registry);
    NotificationRuntime notifications(registry);
    OnActionRuntime on_actions(registry);

    const auto orig_checksum = world.checksum();
    const auto save_path = std::filesystem::current_path() / "test_async_save.thundersav";

    {
        AsyncSavePipeline pipeline;
        const bool triggered = pipeline.trigger_save(
            world, clock, gameplay, ai, notifications, on_actions,
            save_path, 0x1234ull, 0x5678ull);

        assert(triggered);
        pipeline.wait_completion();

        const auto status = pipeline.last_status();
        assert(status.state == AsyncSaveState::Completed);
        assert(status.bytes_written > 0);
        assert(status.world_checksum == orig_checksum);
        assert(std::filesystem::exists(save_path));
    }

    // Verify loading the saved file
    std::ifstream file(save_path, std::ios::binary | std::ios::ate);
    assert(file.is_open());
    const auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> save_bytes(static_cast<std::size_t>(file_size));
    file.read(reinterpret_cast<char*>(save_bytes.data()), file_size);
    file.close();

    World restored_world;
    GameClock restored_clock;
    ScriptedGameplayRuntime restored_gameplay(registry);
    UtilityAiEngine restored_ai(registry);
    NotificationRuntime restored_notifications(registry);
    OnActionRuntime restored_on_actions(registry);

    const auto meta = SaveGameCodec::decode(
        save_bytes, restored_world, restored_clock, restored_gameplay,
        restored_ai, restored_notifications, restored_on_actions,
        definitions, 0x1234ull, 0x5678ull);

    assert(meta.world_checksum == orig_checksum);
    assert(restored_world.checksum() == orig_checksum);
    assert(restored_world.countries.size() == 1);
    assert(restored_world.pops.size() == 1);
    assert(restored_world.buildings.size() == 1);

    // Clean up temporary save file
    std::error_code ec;
    std::filesystem::remove(save_path, ec);

    std::cout << "[PASS] AsyncSavePipeline background save and recovery\n";
}

int main() {
    std::cout << "Running Memory & Save Optimization tests...\n";
    test_pop_building_cold_hot_separation();
    test_async_save_pipeline();
    std::cout << "All Memory & Save Optimization tests passed successfully!\n";
    return 0;
}
