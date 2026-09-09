#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/simulation/living/LivingMapStreamingPlanner.hpp"
#include "thunder/simulation/living/LivingMapSystem.hpp"
#include "thunder/simulation/living/TransportNetwork.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace thunder;
using Clock=std::chrono::steady_clock;

template<class F> static double ms(F&& f){const auto a=Clock::now();f();return std::chrono::duration<double,std::milli>(Clock::now()-a).count();}

int main(int argc,char** argv){
    constexpr std::size_t provinces=8'000u,markets=128u;
    const std::size_t pops=argc>1?static_cast<std::size_t>(std::strtoull(argv[1],nullptr,10)):300'000u;
    const std::size_t buildings=argc>2?static_cast<std::size_t>(std::strtoull(argv[2],nullptr,10)):30'000u;
    EconomyDefinitions defs;
    const auto grain=defs.add_good({"grain",1000});
    const std::array<NeedFlow,1> needs_flow{{{grain,1000}}};
    const auto needs=defs.add_need_profile("workers",needs_flow);
    const std::array<RecipeFlow,0> none{}; const std::array<RecipeFlow,1> out{{{grain,1000}}};
    std::array<BuildingTypeId,5> types{
        defs.add_building_type("urban",1000,none,out),defs.add_building_type("factory",1000,none,out),
        defs.add_building_type("farm",1000,none,out),defs.add_building_type("mine",1000,none,out),
        defs.add_building_type("port",1000,none,out)};
    World world;
    for(std::size_t m=0;m<markets;++m) world.countries.create({"T",0,0,0,0.2});
    world.markets.resize(markets,defs);
    for(std::size_t m=0;m<markets;++m) world.markets.set_owner(MarketId{static_cast<MarketId::rep_type>(m)},CountryId{static_cast<CountryId::rep_type>(m)});
    std::vector<ProvinceVisualDefinition> province_defs(provinces);
    for(std::size_t p=0;p<provinces;++p){
        province_defs[p]={static_cast<double>(p%160u)*28'000.0,static_cast<double>(p/160u)*28'000.0,10'000u,BiomeClass::Temperate,(p%11u)==0u};
    }
    world.buildings.reserve(buildings); world.pops.reserve(pops);
    std::vector<BuildingId> employer_by_province(provinces);
    for(std::size_t b=0;b<buildings;++b){
        const auto p=b%provinces; const auto m=p%markets;
        const auto id=world.buildings.create({MarketId{static_cast<MarketId::rep_type>(m)},types[b%types.size()],static_cast<std::uint16_t>(1u+(b%4u)),1000,0,ProvinceId{static_cast<ProvinceId::rep_type>(p)}});
        if(b<provinces) employer_by_province[p]=id;
    }
    for(std::size_t p=0;p<provinces;++p){ if(!employer_by_province[p].valid()) employer_by_province[p]=BuildingId{static_cast<BuildingId::rep_type>(p%buildings)}; }
    for(std::size_t i=0;i<pops;++i){
        const auto p=i%provinces; const auto m=p%markets;
        const auto id=world.pops.create({MarketId{static_cast<MarketId::rep_type>(m)},100u,employer_by_province[p],needs,ProvinceId{static_cast<ProvinceId::rep_type>(p)}});
        world.pops.set_employed(id,80u); world.pops.set_standard_of_living_milli(id,18'000);
    }
    LivingMapSystem map; map.set_provinces(province_defs);
    map.set_building_visual(types[0],BuildingVisualKind::Urban); map.set_building_visual(types[1],BuildingVisualKind::Factory);
    map.set_building_visual(types[2],BuildingVisualKind::Farm); map.set_building_visual(types[3],BuildingVisualKind::Mine); map.set_building_visual(types[4],BuildingVisualKind::Port);
    JobSystem jobs{4u};
    LivingMapUpdateStats first{}; const auto first_ms=ms([&]{first=map.update(world,jobs);});
    LivingMapUpdateStats steady{}; double steady_total=0; for(int i=0;i<100;++i) steady_total+=ms([&]{steady=map.update(world,jobs);});
    for(std::size_t i=0;i<100u;++i) world.buildings.set_level(BuildingId{static_cast<BuildingId::rep_type>(i)},20u);
    LivingMapUpdateStats changed{}; const auto changed_ms=ms([&]{changed=map.update(world,jobs);});
    std::vector<TransportLinkDefinition> transport_links; transport_links.reserve(provinces*2u);
    for(std::size_t p=0;p<provinces;++p){ if((p%160u)+1u<160u) transport_links.push_back({ProvinceId{static_cast<ProvinceId::rep_type>(p)},ProvinceId{static_cast<ProvinceId::rep_type>(p+1u)},(p%3u==0u)?TransportKind::Rail:TransportKind::Road,static_cast<std::uint8_t>(1u+(p%4u)),0u}); if(p+160u<provinces) transport_links.push_back({ProvinceId{static_cast<ProvinceId::rep_type>(p)},ProvinceId{static_cast<ProvinceId::rep_type>(p+160u)},TransportKind::Road,1u,0u}); }
    TransportNetwork transport; const auto transport_ms=ms([&]{transport.build(province_defs,transport_links);});
    std::vector<std::uint32_t> resident(map.chunk_count(),0u); LivingMapStreamingPlanner planner;
    double planner_total=0; LivingStreamingPlan plan; for(int i=0;i<1000;++i) planner_total+=ms([&]{plan=planner.build(map,1'000'000.0,500'000.0,resident);});
    std::cout<<std::fixed<<std::setprecision(3)
        <<"Thunder 1.0 RC-GPU Living Map benchmark\n"
        <<"provinces="<<provinces<<" pops="<<pops<<" buildings="<<buildings<<" chunks="<<map.chunk_count()<<"\n"
        <<"first update ms="<<first_ms<<" dirty provinces="<<first.dirty_provinces<<" instances="<<first.total_instances<<" upload MiB="<<(static_cast<double>(first.upload_bytes)/(1024.0*1024.0))<<"\n"
        <<"steady update avg ms="<<(steady_total/100.0)<<" dirty="<<steady.dirty_provinces<<" upload="<<steady.upload_bytes<<"\n"
        <<"100 province-visible changes ms="<<changed_ms<<" dirty provinces="<<changed.dirty_provinces<<" dirty chunks="<<changed.dirty_chunks<<" upload KiB="<<(static_cast<double>(changed.upload_bytes)/1024.0)<<"\n"
        <<"planner 1000 avg ms="<<(planner_total/1000.0)<<" visible chunks="<<plan.visible.size()<<" near instances="<<plan.near_instance_count<<"\n"
        <<"transport links="<<transport_links.size()<<" build ms="<<transport_ms<<" chunks="<<transport.chunk_count()<<" memory MiB="<<(static_cast<double>(transport.memory_bytes())/(1024.0*1024.0))<<"\n"
        <<"living memory MiB="<<(static_cast<double>(map.memory_bytes())/(1024.0*1024.0))<<" instance bytes="<<sizeof(LivingInstanceGpu)<<" cluster bytes="<<sizeof(LivingClusterGpu)<<"\n"
        <<"checksum=0x"<<std::hex<<map.checksum()<<std::dec<<"\n";
}
