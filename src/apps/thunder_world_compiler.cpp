#include "thunder/content/worldpack/WorldPack.hpp"
#include "thunder/content/worldpack/WorldPackMetadata.hpp"
#include "thunder/simulation/world/WorldStaticLayers.hpp"
#include "thunder/presentation/render/map/WorldMapPageSource.hpp"
#include "thunder/presentation/render/map/WorldStaticLayerSource.hpp"

#include <cstddef>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

using namespace thunder;

namespace {
std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open input chunk: " + path.string());
    const auto size_pos = in.tellg();
    if (size_pos < 0) throw std::runtime_error("cannot size input chunk: " + path.string());
    const auto size = static_cast<std::size_t>(size_pos);
    std::vector<std::byte> bytes(size);
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in && !bytes.empty()) throw std::runtime_error("cannot read input chunk: " + path.string());
    return bytes;
}

void usage() {
    std::cerr << "Usage: thunder_world_compiler <manifest.txt> <output.thunderworld>\n"
              << "Manifest rows: <type> <level> <x> <y> <variant> <relative-or-absolute-file>\n"
              << "Manifest directives: @world horizontal_wrap=true\n";
}

bool parse_bool(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "1" || value == "true" || value == "yes") return true;
    if (value == "0" || value == "false" || value == "no") return false;
    throw std::runtime_error("invalid @world boolean: " + value);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) { usage(); return 2; }
        const std::filesystem::path manifest_path = argv[1];
        const std::filesystem::path output_path = argv[2];
        std::ifstream manifest(manifest_path);
        if (!manifest) throw std::runtime_error("cannot open manifest: " + manifest_path.string());

        const auto base = manifest_path.parent_path();

        std::vector<std::pair<std::uint32_t, std::string>> rows;
        bool horizontal_wrap = false;
        std::string line;
        std::uint32_t line_no = 0;
        while (std::getline(manifest, line)) {
            ++line_no;
            const auto first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || line[first] == '#') continue;
            if (line.compare(first, 6u, "@world") == 0 &&
                (first + 6u == line.size() || std::isspace(static_cast<unsigned char>(line[first + 6u])))) {
                std::istringstream directive(line.substr(first + 6u));
                std::string assignment;
                while (directive >> assignment) {
                    const auto equals = assignment.find('=');
                    if (equals == std::string::npos) throw std::runtime_error("invalid @world directive " + std::to_string(line_no));
                    const auto key = assignment.substr(0, equals);
                    const auto value = assignment.substr(equals + 1u);
                    if (key == "horizontal_wrap") horizontal_wrap = parse_bool(value);
                    else throw std::runtime_error("unknown @world directive key: " + key);
                }
                continue;
            }
            std::istringstream row(line);
            std::string type_text;
            std::uint32_t level = 0;
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::uint32_t variant = 0;
            std::string file_text;
            if (!(row >> type_text >> level >> x >> y >> variant >> file_text))
                throw std::runtime_error("invalid manifest row " + std::to_string(line_no));
            rows.emplace_back(line_no, line);
        }

        WorldPackWriteOptions options;
        options.horizontal_wrap = horizontal_wrap;
        WorldPackWriter writer;
        writer.open(output_path, options);
        for (const auto& [row_line_no, row_text] : rows) {
            std::istringstream row(row_text);
            std::string type_text;
            std::uint32_t level = 0;
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::uint32_t variant = 0;
            std::string file_text;
            row >> type_text >> level >> x >> y >> variant >> file_text;
            const auto type = parse_world_chunk_type(type_text);
            if (!type) throw std::runtime_error("unknown chunk type on row " + std::to_string(row_line_no));
            if (level > 65535u) throw std::runtime_error("level out of range on row " + std::to_string(row_line_no));
            std::filesystem::path file = file_text;
            if (file.is_relative()) file = base / file;
            const auto bytes = read_file(file);
            writer.append({*type, static_cast<std::uint16_t>(level), x, y, variant}, bytes);
        }

        const auto stats = writer.finalize();
        WorldPackReader verify;
        verify.open(output_path);
        const auto metadata_key = WorldChunkKey{WorldChunkType::Metadata, 0u, 0, 0, 0u};
        if (!verify.contains(metadata_key)) throw std::runtime_error("manifest must contain a metadata chunk");
        const auto metadata = parse_world_pack_metadata(verify.read(metadata_key));
        if (metadata.horizontal_wrap != stats.horizontal_wrap)
            throw std::runtime_error("metadata/header horizontal-wrap mismatch after compilation");
        const std::set<WorldChunkType> chunk_types = [&] {
            std::set<WorldChunkType> result;
            for (const auto& entry : verify.index()) result.insert(entry.key.type);
            return result;
        }();
        const std::array required_types{
            WorldChunkType::CountryDefinitions, WorldChunkType::MarketDefinitions,
            WorldChunkType::StateDefinitions, WorldChunkType::ProvinceDefinitions,
            WorldChunkType::HistoricalSetup, WorldChunkType::AdjacencyOffsets,
            WorldChunkType::AdjacencyNeighbors, WorldChunkType::ProvinceCoastBundle,
            WorldChunkType::TerrainHeightPage, WorldChunkType::LakeMask,
            WorldChunkType::SpatialMask, WorldChunkType::PlacementCandidates,
            WorldChunkType::SettlementAnchors,
            WorldChunkType::ResourceDistribution, WorldChunkType::RiverPolyline,
            WorldChunkType::TransportPolyline, WorldChunkType::ArchitectureRegion,
            WorldChunkType::AreaDefinitions, WorldChunkType::TradeProvinceDefinitions,
            WorldChunkType::LocationDefinitions, WorldChunkType::MapLabelDefinitions};
        for (const auto type : required_types) {
            if (!chunk_types.contains(type))
                throw std::runtime_error("manifest is missing required world chunk type: " +
                                         std::string{world_chunk_type_name(type)});
        }
        for (std::uint32_t level = 0u; level < metadata.clip_levels; ++level) {
            const auto count_x = metadata.page_count_x(level);
            const auto count_y = metadata.page_count_y(level);
            for (std::uint32_t y = 0u; y < count_y; ++y) {
                for (std::uint32_t x = 0u; x < count_x; ++x) {
                    const auto page = WorldChunkKey{WorldChunkType::ProvinceCoastBundle,
                                                    static_cast<std::uint16_t>(level),
                                                    static_cast<std::int32_t>(x),
                                                    static_cast<std::int32_t>(y), 0u};
                    const auto height = WorldChunkKey{WorldChunkType::TerrainHeightPage,
                                                      static_cast<std::uint16_t>(level),
                                                      static_cast<std::int32_t>(x),
                                                      static_cast<std::int32_t>(y), 0u};
                    const auto lake = WorldChunkKey{WorldChunkType::LakeMask,
                                                    static_cast<std::uint16_t>(level),
                                                    static_cast<std::int32_t>(x),
                                                    static_cast<std::int32_t>(y), 0u};
                    const auto spatial = WorldChunkKey{WorldChunkType::SpatialMask,
                                                       static_cast<std::uint16_t>(level),
                                                       static_cast<std::int32_t>(x),
                                                       static_cast<std::int32_t>(y), 0u};
                    if (!verify.contains(page) || !verify.contains(height) ||
                        !verify.contains(lake) || !verify.contains(spatial))
                        throw std::runtime_error("clipmap page family is incomplete at level " +
                                                 std::to_string(level) + " page " +
                                                 std::to_string(x) + "," + std::to_string(y));
                }
            }
        }
        WorldMapPageSource page_source;
        std::string page_diagnostic;
        if (!page_source.open(output_path, page_diagnostic))
            throw std::runtime_error(page_diagnostic.empty()
                                         ? "compiled world page family failed to open"
                                         : page_diagnostic);
        WorldMapPage decoded_page;
        for (std::uint32_t level = 0u; level < metadata.clip_levels; ++level) {
            const auto count_x = metadata.page_count_x(level);
            const auto count_y = metadata.page_count_y(level);
            for (std::uint32_t y = 0u; y < count_y; ++y) {
                for (std::uint32_t x = 0u; x < count_x; ++x) {
                    if (!page_source.decode({static_cast<std::int32_t>(x),
                                             static_cast<std::int32_t>(y),
                                             static_cast<std::uint16_t>(level)}, decoded_page))
                        throw std::runtime_error("compiled world page payload failed decode at level " +
                                                 std::to_string(level) + " page " +
                                                 std::to_string(x) + "," + std::to_string(y));
                }
            }
        }
        const auto static_layers = load_world_static_layers(
            verify, metadata.province_count, metadata.state_count);
        WorldStaticLayerSource static_source{verify};
        WorldPolylineChunk decoded_polyline;
        for (const auto& key : static_layers.rivers) {
            if (!static_source.decode(key, decoded_polyline))
                throw std::runtime_error("invalid river polyline payload in compiled pack");
        }
        for (const auto& key : static_layers.transports) {
            if (!static_source.decode(key, decoded_polyline))
                throw std::runtime_error("invalid transport polyline payload in compiled pack");
        }
        std::cout << "Thunder World Compiler\n"
                  << "chunks: " << stats.chunk_count << "\n"
                  << "raw bytes: " << stats.raw_bytes << "\n"
                  << "stored bytes: " << stats.stored_bytes << "\n"
                  << "compression ratio: " << stats.compression_ratio() << "\n"
                  << "index bytes: " << stats.index_bytes << "\n"
                  << "horizontal wrap: " << (stats.horizontal_wrap ? "true" : "false") << "\n"
                  << "clip levels: " << metadata.clip_levels << "\n"
                  << "provinces: " << metadata.province_count << "\n"
                  << "sea provinces: " << metadata.sea_count << "\n"
                  << "lake provinces: " << metadata.lake_count << "\n"
                  << "river chunks: " << static_layers.rivers.size() << "\n"
                  << "transport chunks: " << static_layers.transports.size() << "\n"
                  << "architecture assignments: " << static_layers.architecture_regions.size() << "\n"
                  << "resource records: " << static_layers.resource_distribution.size() << "\n"
                  << "build hash: 0x" << std::hex << stats.build_hash << std::dec << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "thunder_world_compiler: " << e.what() << '\n';
        return 1;
    }
}
