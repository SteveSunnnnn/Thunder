#include "thunder/simulation/world/WorldBootstrap.hpp"
#include "thunder/simulation/world/WorldTopology.hpp"
#include "TestTempPath.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
int main(){
    thunder::EconomyDefinitions defs; defs.add_good({"grain",1000});
    const thunder::CountryInit countries[]{{"GBR",1,2,3,0.2}};
    const thunder::MarketBootstrapRecord markets[]{{thunder::CountryId{0}}};
    const thunder::StateInit states[]{{"england",thunder::CountryId{0},thunder::MarketId{0},thunder::ProvinceId{0}}};
    const thunder::ProvinceInit provinces[]{{"london",thunder::StateId{0},thunder::CountryId{0},thunder::MarketId{0},1000.0,2000.0,500}};
    const std::uint32_t offsets[]{0,0};
    const thunder::PlacementCandidateLocal placement[]{{thunder::ProvinceId{0},1234u,2345u,12,thunder::PlacementClass::Urban,thunder::PlacementBuildable,100u}};
    const thunder::SettlementAnchorLocal anchors[]{{thunder::ProvinceId{0},3456u,4567u,14,500u,0x1234u}};
    const auto path=thunder_test::unique_temp_path("thunder_bootstrap_test.thunderworld");
    thunder::WorldPackWriter w;w.open(path);w.append({thunder::WorldChunkType::CountryDefinitions,0,0,0,0},thunder::WorldBootstrapWire::countries(countries));w.append({thunder::WorldChunkType::MarketDefinitions,0,0,0,0},thunder::WorldBootstrapWire::markets(markets));w.append({thunder::WorldChunkType::StateDefinitions,0,0,0,0},thunder::WorldBootstrapWire::states(states));w.append({thunder::WorldChunkType::ProvinceDefinitions,0,0,0,0},thunder::WorldBootstrapWire::provinces(provinces));w.append({thunder::WorldChunkType::AdjacencyOffsets,0,0,0,0},thunder::WorldBootstrapWire::adjacency_offsets(offsets));w.append({thunder::WorldChunkType::AdjacencyNeighbors,0,0,0,0},thunder::WorldBootstrapWire::adjacency_neighbors({}));
    w.append({thunder::WorldChunkType::PlacementCandidates,0,2,3,0},thunder::SpatialPlacementWire::placement_candidates(placement));
    w.append({thunder::WorldChunkType::SettlementAnchors,0,4,5,0},thunder::SpatialPlacementWire::settlement_anchors(anchors));
    const std::uint8_t palette[]{0x43,0x50,0x4c,0x31,1,0,0,0,12,34,56,255};
    w.append({thunder::WorldChunkType::CountryPresentation,0,0,0,0}, std::as_bytes(std::span{palette}));
    w.finalize();
    thunder::WorldPackReader r;r.open(path);auto result=thunder::WorldBootstrap::load(r,defs);assert(result.world.countries.size()==1);assert(result.world.geography.state_count()==1);assert(result.world.geography.province_count()==1);assert(result.scope_index.states(thunder::CountryId{0}).size()==1);assert(result.scope_index.provinces(thunder::StateId{0}).size()==1);assert(result.adjacency.province_count()==1);assert(result.spatial_placement.candidate_count()==1);assert(result.spatial_placement.anchor_count()==1);
    const auto pc=result.spatial_placement.candidates(thunder::ProvinceId{0},thunder::PlacementClass::Urban);assert(pc.size()==1);assert(pc[0].chunk.x==2&&pc[0].chunk.y==3);
    const auto sa=result.spatial_placement.anchors(thunder::ProvinceId{0});assert(sa.size()==1);assert(sa[0].chunk.x==4&&sa[0].chunk.y==5);
    assert(result.state_regions.region_count() == 1);
    const auto reg = result.state_regions.region_for_state(thunder::StateId{0});
    assert(result.state_regions.states(reg).size() == 1);
    assert(result.state_regions.provinces(reg).size() == 1);
    assert(result.state_regions.checksum() != 0);
    const auto topology = thunder::WorldTopology::load(r);
    assert(topology.countries[0].authored_map_color);
    assert((topology.countries[0].map_color == std::array<std::uint8_t,4>{12,34,56,255}));
    r.close();std::filesystem::remove(path);std::cout<<"Thunder world bootstrap tests passed\n";
}
