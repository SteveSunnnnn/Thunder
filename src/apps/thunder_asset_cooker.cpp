#include "thunder/content/assets/AssetPack.hpp"
#include "thunder/content/assets/GlTFImporter.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace {std::vector<std::byte> read_file(const std::filesystem::path&p){std::ifstream i(p,std::ios::binary|std::ios::ate);if(!i)throw std::runtime_error("cannot open asset: "+p.string());const auto n=static_cast<std::size_t>(i.tellg());std::vector<std::byte>b(n);i.seekg(0);if(n)i.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));return b;}thunder::AssetKind kind(std::string_view s){if(s=="mesh")return thunder::AssetKind::Mesh;if(s=="texture")return thunder::AssetKind::Texture;if(s=="shader")return thunder::AssetKind::Shader;if(s=="material")return thunder::AssetKind::Material;if(s=="audio")return thunder::AssetKind::Audio;if(s=="font")return thunder::AssetKind::Font;if(s=="world")return thunder::AssetKind::World;if(s=="script")return thunder::AssetKind::Script;throw std::runtime_error("unknown asset kind");}}
// Mesh sources delivered as .glb are imported and baked into the engine's
// quantized layout (THM1) instead of stored as opaque bytes.
std::vector<std::byte> cook_payload(thunder::AssetKind asset_kind,std::string_view filename,std::vector<std::byte>bytes){
    if(asset_kind==thunder::AssetKind::Mesh&&filename.size()>=4&&filename.substr(filename.size()-4)==".glb"){
        return thunder::assets::cook_glb_mesh_payload(bytes);
    }
    return bytes;
}
int main(int argc,char**argv){try{if(argc!=3){std::cerr<<"Usage: thunder_asset_cooker <manifest.txt> <out.thunderasset>\nrows: <kind> <lod> <key> <file> (mesh kind accepts .glb sources)\n";return 2;}std::filesystem::path manifest=argv[1];std::ifstream in(manifest);if(!in)throw std::runtime_error("cannot open manifest");thunder::AssetPackWriter writer;std::string line;std::uint32_t ln=0;while(std::getline(in,line)){++ln;if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::string k,key,file;unsigned lod=0;if(!(row>>k>>lod>>key>>file)||lod>255)throw std::runtime_error("bad asset manifest row "+std::to_string(ln));std::filesystem::path fp=file;if(fp.is_relative())fp=manifest.parent_path()/fp;auto bytes=read_file(fp);const auto asset_kind=kind(k);writer.add(key,asset_kind,static_cast<std::uint8_t>(lod),cook_payload(asset_kind,fp.string(),std::move(bytes)));}writer.write(argv[2]);std::cout<<"Thunder Asset Cooker PASS\n";return 0;}catch(const std::exception&e){std::cerr<<"thunder_asset_cooker: "<<e.what()<<'\n';return 1;}}
