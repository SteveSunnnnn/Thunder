#include "thunder/content/assets/AssetPack.hpp"
#include <iostream>
int main(int argc,char**argv){try{if(argc!=2)return 2;thunder::AssetPackReader r;r.open(argv[1]);std::cout<<"Thunder Asset Pack\nentries="<<r.entries().size()<<"\nbuild_hash=0x"<<std::hex<<r.build_hash()<<std::dec<<"\n";for(const auto&e:r.entries())std::cout<<"hash=0x"<<std::hex<<e.key_hash<<std::dec<<" kind="<<static_cast<unsigned>(e.kind)<<" lod="<<static_cast<unsigned>(e.lod)<<" bytes="<<e.size<<"\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
