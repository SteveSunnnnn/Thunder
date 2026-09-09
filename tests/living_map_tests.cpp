#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include "thunder/simulation/living/LivingMapStreamingPlanner.hpp"
#include "thunder/simulation/living/LivingMapSystem.hpp"
#include "thunder/simulation/living/TransportNetwork.hpp"
#include "thunder/presentation/render/LivingMapRenderPlan.hpp"
#include "thunder/presentation/render/RenderGraph.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace thunder;

struct LivingFixture {
    EconomyDefinitions defs;
    World world;
    std::vector<ProvinceVisualDefinition> provinces;
    BuildingTypeId urban;
    BuildingTypeId factory;
    BuildingTypeId farm;
    BuildingTypeId mine;
    BuildingTypeId port;
    NeedProfileId needs;
};

static LivingFixture make_fixture(std::size_t province_count) {
    LivingFixture f;
    const auto grain = f.defs.add_good({"grain", 1000});
    const std::array<NeedFlow,1> needs{{{grain,1000}}};
    f.needs = f.defs.add_need_profile("workers", needs);
    const std::array<RecipeFlow,0> none{};
    const std::array<RecipeFlow,1> output{{{grain,1000}}};
    f.urban = f.defs.add_building_type("urban",1000,none,output);
    f.factory = f.defs.add_building_type("factory",1000,none,output);
    f.farm = f.defs.add_building_type("farm",1000,none,output);
    f.mine = f.defs.add_building_type("mine",1000,none,output);
    f.port = f.defs.add_building_type("port",1000,none,output);
    f.world.countries.create({"TST",0,0,0,0.2});
    f.world.markets.resize(1u,f.defs);
    f.world.markets.set_owner(MarketId{0u},CountryId{0u});
    f.provinces.resize(province_count);
    for(std::size_t p=0;p<province_count;++p){
        const auto x=static_cast<double>(p%16u)*30'000.0;
        const auto y=static_cast<double>(p/16u)*30'000.0;
        f.provinces[p]={x,y,12'000u,BiomeClass::Temperate,(p%7u)==0u};
        const ProvinceId province{static_cast<ProvinceId::rep_type>(p)};
        const std::array<BuildingTypeId,5> types{f.urban,f.factory,f.farm,f.mine,f.port};
        std::vector<BuildingId> employers;
        for(std::size_t b=0;b<5u;++b){
            employers.push_back(f.world.buildings.create({MarketId{0u},types[b],static_cast<std::uint16_t>(1u+(p+b)%3u),1000,0,province}));
        }
        for(std::size_t i=0;i<16u;++i){
            const auto pop=f.world.pops.create({MarketId{0u},1000u,employers[i%employers.size()],f.needs,province});
            f.world.pops.set_employed(pop,800u);
            f.world.pops.set_income(pop,1000);
            f.world.pops.set_standard_of_living_milli(pop,15'000+static_cast<std::int32_t>(p%5u)*1000);
        }
    }
    return f;
}

static LivingMapSystem make_map(const LivingFixture& f){
    LivingMapSystem map;
    map.set_provinces(f.provinces);
    map.set_building_visual(f.urban,BuildingVisualKind::Urban);
    map.set_building_visual(f.factory,BuildingVisualKind::Factory);
    map.set_building_visual(f.farm,BuildingVisualKind::Farm);
    map.set_building_visual(f.mine,BuildingVisualKind::Mine);
    map.set_building_visual(f.port,BuildingVisualKind::Port);
    return map;
}

static void test_incremental_and_compact(){
    auto f=make_fixture(64u);
    auto map=make_map(f);
    JobSystem jobs{4u};
    const auto first=map.update(f.world,jobs);
    assert(first.dirty_provinces==64u);
    assert(first.dirty_chunks>0u);
    assert(first.total_instances>0u);
    assert(first.upload_bytes==first.total_instances*sizeof(LivingInstanceGpu)+64u*sizeof(LivingClusterGpu));
    const auto checksum=map.checksum();
    const auto second=map.update(f.world,jobs);
    assert(second.dirty_provinces==0u);
    assert(second.dirty_chunks==0u);
    assert(second.upload_bytes==0u);
    assert(map.checksum()==checksum);
    assert(map.memory_bytes()<2u*1024u*1024u);

    f.world.buildings.set_level(BuildingId{1u},12u);
    const auto changed=map.update(f.world,jobs);
    assert(changed.dirty_provinces==1u);
    assert(changed.dirty_chunks==1u);
    assert(changed.upload_bytes>0u);
}

static void test_deterministic_across_workers(){
    auto f=make_fixture(96u);
    auto a=make_map(f);
    auto b=make_map(f);
    JobSystem serial{0u};
    JobSystem parallel{4u};
    const auto sa=a.update(f.world,serial);
    const auto sb=b.update(f.world,parallel);
    assert(sa.total_instances==sb.total_instances);
    assert(a.checksum()==b.checksum());
}

static void test_streaming_plan_uses_clusters_far_away(){
    auto f=make_fixture(128u);
    auto map=make_map(f);
    JobSystem jobs{2u};
    map.update(f.world,jobs);
    std::vector<std::uint32_t> resident(map.chunk_count(),0u);
    LivingMapStreamingPlanner planner;
    LivingStreamingSettings settings;
    settings.near_radius_m=90'000.0;
    settings.medium_radius_m=180'000.0;
    settings.far_radius_m=500'000.0;
    settings.max_near_instances=500u;
    const auto plan=planner.build(map,0.0,0.0,resident,settings);
    assert(!plan.visible.empty());
    assert(plan.near_instance_count<=settings.max_near_instances);
    assert(plan.stale_upload_bytes>0u);
    bool saw_cluster=false;
    for(const auto& request:plan.visible){ if(request.lod!=LivingLod::NearInstances) saw_cluster=true; }
    assert(saw_cluster);
}



static void test_spatial_index_rebuilds_on_province_membership_changes(){
    auto f=make_fixture(2u);
    auto map=make_map(f);
    JobSystem jobs{0u};
    map.update(f.world,jobs);
    assert(map.province_aggregate(ProvinceId{0u}).population==16'000u);
    assert(map.province_aggregate(ProvinceId{1u}).population==16'000u);

    f.world.pops.set_province(PopId{0u},ProvinceId{1u});
    f.world.buildings.set_province(BuildingId{0u},ProvinceId{1u});
    map.update(f.world,jobs);
    assert(map.province_aggregate(ProvinceId{0u}).population==15'000u);
    assert(map.province_aggregate(ProvinceId{1u}).population==17'000u);
    assert(map.spatial_index().pops(ProvinceId{0u}).size()==15u);
    assert(map.spatial_index().pops(ProvinceId{1u}).size()==17u);
    assert(map.spatial_index().buildings(ProvinceId{0u}).size()==4u);
    assert(map.spatial_index().buildings(ProvinceId{1u}).size()==6u);
}


static void test_authored_placement_spans_multiple_chunks(){
    auto f=make_fixture(2u);
    auto map=make_map(f);
    const thunder::PlacementCandidate candidates[]{
        {ProvinceId{0u},{0,0},1000u,2000u,4,thunder::PlacementClass::Urban,thunder::PlacementBuildable,100u},
        {ProvinceId{0u},{2,0},3000u,4000u,6,thunder::PlacementClass::Industrial,thunder::PlacementBuildable,100u},
        {ProvinceId{0u},{0,0},5000u,6000u,2,thunder::PlacementClass::Buildable,thunder::PlacementBuildable,50u}
    };
    const thunder::SettlementAnchor anchors[]{
        {ProvinceId{0u},{1,0},7000u,8000u,8,1000u,0xABCDEFu}
    };
    thunder::SpatialPlacementDatabase placement;
    placement.build(2u,candidates,anchors);
    map.set_spatial_placement(std::move(placement));
    JobSystem jobs{0u};
    const auto stats=map.update(f.world,jobs);
    assert(stats.dirty_provinces==2u);
    assert(map.chunk_count()>=3u);
    const auto urban_chunk=map.find_chunk_index({0,0});
    const auto anchor_chunk=map.find_chunk_index({1,0});
    const auto industry_chunk=map.find_chunk_index({2,0});
    assert(urban_chunk<map.chunk_count()&&anchor_chunk<map.chunk_count()&&industry_chunk<map.chunk_count());
    bool saw_residential=false;
    for(const auto& i:map.chunk_instances(urban_chunk)) if(i.province_r16==1u&&i.kind==static_cast<std::uint8_t>(LivingInstanceKind::Residential)) saw_residential=true;
    bool saw_factory=false;
    for(const auto& i:map.chunk_instances(industry_chunk)) if(i.province_r16==1u&&i.kind==static_cast<std::uint8_t>(LivingInstanceKind::Factory)) saw_factory=true;
    bool saw_anchor_cluster=false;
    for(const auto& c:map.chunk_clusters(anchor_chunk)) if(c.province_r16==1u&&c.local_x_m==7000u&&c.local_y_m==8000u) saw_anchor_cluster=true;
    assert(saw_residential&&saw_factory&&saw_anchor_cluster);
    const auto steady=map.update(f.world,jobs);
    assert(steady.dirty_chunks==0u&&steady.upload_bytes==0u);
}

static void test_transport_long_diagonal_is_linear_in_crossed_chunks(){
    constexpr double cs=static_cast<double>(LivingMapSystem::chunk_size_m);
    std::array<ProvinceVisualDefinition,2> provinces{{
        {0.5*cs,0.5*cs,8'000u,BiomeClass::Temperate,false},
        {1023.5*cs,1023.5*cs,8'000u,BiomeClass::Temperate,false}}};
    std::array<TransportLinkDefinition,1> links{{
        {ProvinceId{0u},ProvinceId{1u},TransportKind::Rail,1u,0u}}};
    TransportNetwork network;
    network.build(provinces,links);
    assert(network.last_build_candidate_chunks()<=1026u);
    assert(network.chunk_count()>=1023u);
}

static void test_transport_segments_and_render_plan(){
    std::array<ProvinceVisualDefinition,3> provinces{{
        {10'000.0,10'000.0,8'000u,BiomeClass::Temperate,false},
        {150'000.0,10'000.0,8'000u,BiomeClass::Temperate,false},
        {150'000.0,90'000.0,8'000u,BiomeClass::Temperate,false}}};
    std::array<TransportLinkDefinition,2> links{{
        {ProvinceId{0u},ProvinceId{1u},TransportKind::Rail,2u,0u},
        {ProvinceId{1u},ProvinceId{2u},TransportKind::Road,1u,0u}}};
    TransportNetwork network; network.build(provinces,links);
    assert(network.chunk_count()>=3u);
    const auto before=network.checksum();
    std::vector<std::uint32_t> versions; versions.reserve(network.chunk_count());
    for(std::size_t i=0;i<network.chunk_count();++i) versions.push_back(network.chunk_version(i));
    network.set_link_level(0u,5u);
    assert(network.checksum()!=before);
    bool changed=false; bool saw_level=false;
    for(std::size_t i=0;i<network.chunk_count();++i){
        if(network.chunk_version(i)!=versions[i]) changed=true;
        for(const auto& segment:network.chunk_segments(i)) if(segment.kind==static_cast<std::uint8_t>(TransportKind::Rail) && segment.level==5u) saw_level=true;
    }
    assert(changed && saw_level);
    RenderGraph graph; add_living_map_passes(graph); graph.compile();
    assert(graph.order().size()==5u);
    assert(!graph.barriers().empty());
}

int main(){
    test_incremental_and_compact();
    test_deterministic_across_workers();
    test_streaming_plan_uses_clusters_far_away();
    test_spatial_index_rebuilds_on_province_membership_changes();
    test_authored_placement_spans_multiple_chunks();
    test_transport_long_diagonal_is_linear_in_crossed_chunks();
    test_transport_segments_and_render_plan();
    std::cout<<"All Thunder 1.0 Living Map tests passed.\n";
}
