#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/simulation/economy/EconomySystem.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <numeric>
#include <vector>

using namespace thunder;
using Clock = std::chrono::steady_clock;

struct BenchScenario {
    EconomyDefinitions definitions;
    World world;
};

static BenchScenario make_scenario(std::size_t market_count, std::size_t building_count, std::size_t pop_count) {
    BenchScenario s;
    const auto grain=s.definitions.add_good({"grain",1000});
    const auto coal=s.definitions.add_good({"coal",1400});
    const auto iron=s.definitions.add_good({"iron",1800});
    const auto tools=s.definitions.add_good({"tools",3200});
    const auto clothes=s.definitions.add_good({"clothes",2200});
    // Additional goods keep market rows representative of a larger strategy economy.
    for(std::size_t i=5;i<32u;++i) s.definitions.add_good({
        "aux_" + std::to_string(i), static_cast<EconomyPrice>(1000+i*75u)});
    const std::array<NeedFlow,2> worker_needs{{{grain,7000},{clothes,1600}}};
    const auto need=s.definitions.add_need_profile("worker",worker_needs);
    const std::array<RecipeFlow,0> none{};
    const std::array<RecipeFlow,1> farm_out{{{grain,12'000}}};
    const std::array<RecipeFlow,1> coal_out{{{coal,8'000}}};
    const std::array<RecipeFlow,1> iron_out{{{iron,7'000}}};
    const std::array<RecipeFlow,2> tool_in{{{iron,4000},{coal,2000}}};
    const std::array<RecipeFlow,1> tool_out{{{tools,5000}}};
    const std::array<RecipeFlow,1> cloth_out{{{clothes,8000}}};
    const std::array<BuildingTypeId,5> types{{
        s.definitions.add_building_type("farm",1000,none,farm_out),
        s.definitions.add_building_type("coal",1000,none,coal_out),
        s.definitions.add_building_type("iron",1000,none,iron_out),
        s.definitions.add_building_type("tools",1000,tool_in,tool_out),
        s.definitions.add_building_type("textile",1000,none,cloth_out)}};

    s.world.countries.reserve(market_count);
    for(std::size_t m=0;m<market_count;++m) s.world.countries.create({"TST",10'000'000.0,1000.0,50.0,0.20});
    s.world.markets.resize(market_count,s.definitions);
    for(std::size_t m=0;m<market_count;++m) s.world.markets.set_owner(MarketId{static_cast<MarketId::rep_type>(m)},CountryId{static_cast<CountryId::rep_type>(m)});

    s.world.buildings.reserve(building_count);
    std::vector<std::vector<BuildingId>> employers(market_count);
    for(std::size_t i=0;i<building_count;++i){
        const std::size_t m=i%market_count;
        employers[m].push_back(s.world.buildings.create({MarketId{static_cast<MarketId::rep_type>(m)},types[i%types.size()],1u,1100,100'000}));
    }
    s.world.pops.reserve(pop_count);
    for(std::size_t i=0;i<pop_count;++i){
        const std::size_t m=i%market_count;
        const auto& local=employers[m];
        s.world.pops.create({MarketId{static_cast<MarketId::rep_type>(m)},100u,local[(i/market_count)%local.size()],need});
    }
    return s;
}

static double ms(std::chrono::nanoseconds ns){ return std::chrono::duration<double,std::milli>(ns).count(); }

int main(int argc, char** argv){
    const bool large = argc > 1 && std::string_view{argv[1]} == "large";
    const std::size_t markets = large ? 256u : 128u;
    const std::size_t buildings = large ? 80'000u : 30'000u;
    const std::size_t pops = large ? 1'000'000u : 300'000u;
    auto scenario=make_scenario(markets,buildings,pops);
    EconomySystem economy{scenario.definitions};
    const auto index_begin=Clock::now();
    economy.rebuild_indices(scenario.world);
    const auto index_end=Clock::now();
    JobSystem jobs;
    const int warmup_weeks = large ? 3 : 4;
    const int measured_weeks = large ? 16 : 52;
    for(int i=0;i<warmup_weeks;++i) economy.run_weekly(scenario.world,jobs);

    std::vector<double> ticks;
    ticks.reserve(static_cast<std::size_t>(measured_weeks));
    EconomyTickProfile last{};
    for(int i=0;i<measured_weeks;++i){
        EconomyTickProfile profile;
        const auto begin=Clock::now();
        economy.run_weekly(scenario.world,jobs,&profile);
        const auto end=Clock::now();
        ticks.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        last=profile;
    }
    std::sort(ticks.begin(),ticks.end());
    const double average=std::accumulate(ticks.begin(),ticks.end(),0.0)/static_cast<double>(ticks.size());
    const double p50=ticks[ticks.size()/2u];
    const double p95=ticks[(ticks.size()*95u)/100u];

    std::cout<<std::fixed<<std::setprecision(3)
        <<"Thunder 1.0 RC-GPU economy benchmark (machine-specific regression baseline)\n"
        <<"markets="<<markets<<" goods="<<scenario.definitions.good_count()<<" buildings="<<buildings<<" pops="<<pops<<"\n"
        <<"job slots="<<jobs.parallelism()<<"\n"
        <<"market index build="<<std::chrono::duration<double,std::milli>(index_end-index_begin).count()<<" ms\n"
        <<"weekly tick avg="<<average<<" ms p50="<<p50<<" ms p95="<<p95<<" ms max="<<ticks.back()<<" ms\n"
        <<"last phase ms employment="<<ms(last.employment)
        <<" production="<<ms(last.production)
        <<" consumption="<<ms(last.consumption)
        <<" prices="<<ms(last.prices)
        <<" settlement="<<ms(last.settlement)<<"\n"
        <<"economy world bytes="<<scenario.world.economy_memory_bytes()
        <<" scratch/index bytes="<<economy.scratch_memory_bytes()
        <<" pop bytes/row="<<static_cast<double>(scenario.world.pops.memory_bytes())/static_cast<double>(scenario.world.pops.size())<<"\n"
        <<"checksum=0x"<<std::hex<<scenario.world.checksum()<<std::dec<<"\n";
}
