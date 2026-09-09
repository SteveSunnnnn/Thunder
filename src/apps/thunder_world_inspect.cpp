#include "thunder/content/worldpack/WorldPack.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace thunder;

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: thunder_world_inspect <world.thunderworld>\n";
            return 2;
        }
        WorldPackReader pack;
        pack.open(argv[1]);
        const auto stats = pack.stats();
        std::map<WorldChunkType, std::uint32_t> counts;
        std::uint64_t compressed_chunks = 0;
        std::uint64_t xxh3_chunks = 0;
        for (const auto& e : pack.index()) {
            ++counts[e.key.type];
            if (e.codec == WorldChunkCodec::Zstd) ++compressed_chunks;
            if (e.checksum_codec == WorldChecksumCodec::Xxh3_64) ++xxh3_chunks;
        }

        std::cout << "Thunder World Pack\n"
                  << "chunks: " << stats.chunk_count << "\n"
                  << "raw bytes: " << stats.raw_bytes << "\n"
                  << "stored payload bytes: " << stats.stored_bytes << "\n"
                  << "index bytes: " << stats.index_bytes << "\n"
                  << "build hash: 0x" << std::hex << stats.build_hash << std::dec << "\n"
                  << "payload compression ratio: " << std::fixed << std::setprecision(4)
                  << stats.compression_ratio() << "\n"
                  << "compressed chunks: " << compressed_chunks << "\n"
                  << "XXH3 checksum chunks: " << xxh3_chunks << "\n";
        for (const auto& [type, count] : counts) {
            std::cout << "  " << world_chunk_type_name(type) << ": " << count << "\n";
        }

        const WorldChunkKey metadata{WorldChunkType::Metadata, 0u, 0, 0, 0u};
        if (pack.contains(metadata)) {
            const auto bytes = pack.read(metadata);
            std::cout << "metadata:\n";
            std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "thunder_world_inspect: " << e.what() << '\n';
        return 1;
    }
}
