#include "thunder/content/assets/AssetPack.hpp"
#include "thunder/content/assets/ArchitectureKit.hpp"
#include "thunder/content/assets/Material.hpp"
#include "thunder/presentation/ui/FontAtlas.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include "TestTempPath.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
int main(){
    const auto path=thunder_test::unique_temp_path("thunder_asset_test.thunderasset");
    std::vector<std::byte> mesh(1024,std::byte{0x2a});std::vector<std::byte> tex(256,std::byte{0x07});
    thunder::AssetPackWriter w;w.add("architecture/britain/house",thunder::AssetKind::Mesh,0,mesh);w.add("architecture/britain/house",thunder::AssetKind::Mesh,1,std::span<const std::byte>{mesh}.first(256));w.add("textures/brick",thunder::AssetKind::Texture,0,tex);w.write(path);
    thunder::AssetPackReader r;r.open(path);auto e=r.find("architecture/britain/house",0);assert(e);assert(r.read(*e)==mesh);assert(!r.find("missing",0));
    thunder::AssetResidencyManager residency(1000);residency.touch(1,0,700,1);residency.touch(2,0,500,2);const auto evicted=residency.enforce_budget();assert(evicted.size()==1&&evicted[0].key_hash==1&&residency.resident_bytes()==500);
    thunder::ArchitectureKit kit;kit.add({thunder::ArchitectureKind::Residential,1800,1900,0,100000,2,11,22,3});kit.add({thunder::ArchitectureKind::Residential,1800,1900,0,100000,1,33,44,2});assert(kit.select(thunder::ArchitectureKind::Residential,1836,15000,123)!=nullptr);
    const auto arch_path=thunder_test::unique_temp_path("thunder_arch_test.thunderarch");kit.write(arch_path);const auto loaded_kit=thunder::ArchitectureKit::read(arch_path);assert(loaded_kit.checksum()==kit.checksum()&&std::equal(loaded_kit.variants().begin(),loaded_kit.variants().end(),kit.variants().begin(),kit.variants().end()));

    thunder::PbrMaterial material;material.base_color={0.72f,0.41f,0.22f,1.0f};material.roughness=0.72f;material.base_color_texture=thunder::asset_key_hash("textures/brick/albedo");const auto material_path=thunder_test::unique_temp_path("thunder_material_test.thundermat");material.write(material_path);const auto loaded_material=thunder::PbrMaterial::read(material_path);assert(loaded_material==material);
    thunder::FontAtlas font;font.set_metrics(64,64,4.0f,thunder::asset_key_hash("fonts/test_atlas"));font.set_glyphs({{63,0.55f,0.0f,-0.2f,0.5f,0.8f,0.0f,0.0f,16.0f,32.0f},{65,0.62f,0.0f,-0.1f,0.58f,0.8f,16.0f,0.0f,32.0f,32.0f},{0x4e2du,1.0f,0.0f,-0.1f,1.0f,0.9f,32.0f,0.0f,48.0f,32.0f},{0xfffdu,0.6f,0.0f,-0.2f,0.55f,0.8f,48.0f,0.0f,64.0f,32.0f}});const auto font_path=thunder_test::unique_temp_path("thunder_font_test.thunderfont");font.write(font_path);const auto loaded_font=thunder::FontAtlas::read(font_path);assert(loaded_font.checksum()==font.checksum());thunder::UiDrawList text_ui;loaded_font.append_text(text_ui,"AAA",10.0f,30.0f,18.0f,0xffffffffu,{0,0,200,60});assert(text_ui.vertices().size()==12&&text_ui.indices().size()==18&&text_ui.batches().size()==1&&text_ui.batches()[0].kind==thunder::UiBatchKind::MsdfText);thunder::UiDrawList utf8_ui;loaded_font.append_text(utf8_ui,"A\xE4\xB8\xAD" "A",10.0f,30.0f,18.0f,0xffffffffu,{0,0,200,60});assert(utf8_ui.vertices().size()==12&&utf8_ui.indices().size()==18&&utf8_ui.batches().size()==1);
    thunder::FontAtlas fallback_font;fallback_font.set_metrics(64,64,4.0f,thunder::asset_key_hash("fonts/fallback_atlas"));fallback_font.set_glyphs({{0x6587u,1.0f,0.0f,-0.1f,1.0f,0.9f,0.0f,0.0f,16.0f,32.0f}});thunder::FontAtlas primary_font;primary_font.set_metrics(64,64,4.0f,thunder::asset_key_hash("fonts/primary_atlas"));primary_font.set_glyphs({{65,0.62f,0.0f,-0.1f,0.58f,0.8f,16.0f,0.0f,32.0f,32.0f}});primary_font.set_fallback(&fallback_font);assert(primary_font.find(65)!=nullptr&&primary_font.find(0x6587u)!=nullptr&&primary_font.find(0x9999u)==nullptr);thunder::UiDrawList fb_ui;primary_font.append_text(fb_ui,"A\xE6\x96\x87",10.0f,30.0f,18.0f,0xffffffffu,{0,0,200,60});assert(fb_ui.vertices().size()==8&&fb_ui.indices().size()==12);
    thunder::UiDrawList ui;ui.quad({0,0,100,20},0xffffffffu);ui.text("Population",4,4,14,0xffffffffu,{0,0,100,20});ui.hit(42,{0,0,100,20});assert(ui.vertices().size()==4&&ui.indices().size()==6&&ui.text_runs().size()==1&&ui.hits().size()==1);
    const auto v=thunder::virtualize_rows(100000,20.0f,40000.0f,600.0f,2);assert(v.count<40&&v.first>0);
    const float row_offsets[]{0.0f,18.0f,42.0f,78.0f,96.0f,140.0f};const auto variable=thunder::virtualize_variable_rows(row_offsets,40.0f,45.0f,1);assert(variable.first==0u&&variable.count>=4u&&variable.bottom_padding>=0.0f);
    thunder::UiDrawList rich_ui;rich_ui.panel({10,10,240,120},0xf020252cu,0xff6f7782u);rich_ui.nine_slice({20,20,180,70},{{0,0,1,1},{0.25f,0.25f,0.5f,0.5f},{12,12,12,12}},0xffffffffu,99u,{0,0,300,200});rich_ui.hit(1,{20,20,100,50});rich_ui.hit(2,{30,30,20,20});assert(rich_ui.vertices().size()>=44u);assert(rich_ui.hit_test(35,35)==2u);assert(!rich_ui.hit_test(290,190).has_value());
    std::vector<float> series(1000u);for(std::size_t i=0;i<series.size();++i)series[i]=static_cast<float>(i%37u);series[501]=1000.0f;std::vector<float> chart;const auto range=thunder::chart_range(series,true);thunder::build_chart_polyline(series,{0,0,300,100},64u,chart,range);assert(chart.size()<=128u&&chart.size()>=4u);float minimum_y=1000.0f;for(std::size_t i=1;i<chart.size();i+=2u)minimum_y=std::min(minimum_y,chart[i]);assert(minimum_y<10.0f);
    const auto tooltip=thunder::place_tooltip({285,185,10,10},120,80,{0,0,300,200},5);assert(tooltip.x>=5.0f&&tooltip.y>=5.0f&&tooltip.x+tooltip.w<=295.0f&&tooltip.y+tooltip.h<=195.0f);
    std::filesystem::remove(path);std::filesystem::remove(arch_path);std::filesystem::remove(material_path);std::filesystem::remove(font_path);std::cout<<"Thunder asset/UI tests passed\n";
}
