#include "thunder/presentation/render/map/WorldMapPageSource.hpp"
#include "thunder/presentation/render/map/WorldMapPicker.hpp"
#include "thunder/simulation/world/WorldTopology.hpp"
#include "thunder/content/worldpack/WorldPack.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: thunder_world_validate <world.thunderworld> [--location-prefix prefix] [--require-owned-prefix prefix]\n";
        return 2;
    }
    try {
        const std::filesystem::path path{argv[1]};
        std::string prefix;
        std::vector<std::string> owned_prefixes;
        for (int i = 2; i < argc; ++i) {
            const std::string option{argv[i]};
            if (i + 1 >= argc) throw std::runtime_error("missing validation option value");
            if (option == "--location-prefix") prefix = argv[++i];
            else if (option == "--require-owned-prefix") owned_prefixes.emplace_back(argv[++i]);
            else throw std::runtime_error("unknown validation option: " + option);
        }
        thunder::WorldPackReader pack;
        pack.open(path);
        const auto topology = thunder::WorldTopology::load(pack);
        if (!topology.map_hierarchy.validate(topology.geography.province_count()))
            throw std::runtime_error("Area/Province/Location hierarchy failed validation");
        if (topology.adjacency.province_count() != topology.geography.province_count() ||
            !topology.adjacency.is_symmetric())
            throw std::runtime_error("Location adjacency graph failed validation");
        std::uint64_t ownership_checks = 0u;
        for (std::size_t index = 0; index < topology.map_hierarchy.location_count(); ++index) {
            const auto& location = topology.map_hierarchy.location(
                thunder::LocationId{static_cast<thunder::LocationId::rep_type>(index)});
            if (!std::any_of(owned_prefixes.begin(), owned_prefixes.end(),
                             [&](const auto& value) { return location.key.starts_with(value); })) continue;
            ++ownership_checks;
            const auto raster = location.raster_province;
            if (!topology.geography.province_owner(raster).valid())
                throw std::runtime_error("required-owned Location has no owner: " + location.key);
        }
        if (!owned_prefixes.empty() && ownership_checks == 0u)
            throw std::runtime_error("ownership prefixes matched no Locations");

        thunder::WorldMapPageSource pages;
        std::string diagnostic;
        if (!pages.open(path, diagnostic))
            throw std::runtime_error("page source failed: " + diagnostic);
        std::uint64_t decoded_pages = 0u;
        thunder::WorldMapPage page;
        const auto& metadata = pages.metadata();
        for (std::uint32_t level = 0; level < metadata.clip_levels; ++level) {
            for (std::uint32_t y = 0; y < metadata.page_count_y(level); ++y) {
                for (std::uint32_t x = 0; x < metadata.page_count_x(level); ++x) {
                    if (!pages.decode({static_cast<std::int32_t>(x),
                                       static_cast<std::int32_t>(y),
                                       static_cast<std::uint16_t>(level)}, page))
                        throw std::runtime_error("page decode failed at " +
                            std::to_string(level) + "/" + std::to_string(x) + "/" +
                            std::to_string(y));
                    ++decoded_pages;
                }
            }
        }

        thunder::WorldMapPicker picker;
        if (!picker.open(path, topology.map_hierarchy, diagnostic))
            throw std::runtime_error("picker failed: " + diagnostic);
        std::uint64_t pick_failures = 0u;
        std::uint64_t checked_locations = 0u;
        std::uint64_t frame = 1u;
        for (std::size_t index = 0; index < topology.map_hierarchy.location_count(); ++index) {
            const auto expected = thunder::LocationId{static_cast<thunder::LocationId::rep_type>(index)};
            const auto& location = topology.map_hierarchy.location(expected);
            if (!location.key.starts_with(prefix)) continue;
            ++checked_locations;
            const auto picked = picker.pick_world(
                {location.center_x_m, location.center_y_m}, 0u, frame++);
            if (picked.location != expected) {
                if (pick_failures < 20u)
                    std::cerr << "pick mismatch: " << location.key << '\n';
                ++pick_failures;
            }
        }
        if (checked_locations == 0u)
            throw std::runtime_error("no Locations match the requested prefix");
        if (pick_failures != 0u)
            throw std::runtime_error("representative-point picking failures=" +
                                     std::to_string(pick_failures));

        std::cout << "Thunder World Validation PASS\n"
                  << "areas=" << topology.map_hierarchy.area_count() << '\n'
                  << "provinces=" << topology.map_hierarchy.province_count() << '\n'
                  << "locations=" << topology.map_hierarchy.location_count() << '\n'
                  << "adjacency_directed=" << topology.adjacency.directed_edge_count() << '\n'
                  << "decoded_pages=" << decoded_pages << '\n'
                  << "representative_pick_scope=" << (prefix.empty() ? "all" : prefix) << '\n'
                  << "representative_picks=" << checked_locations << '\n'
                  << "required_ownership_checks=" << ownership_checks << '\n'
                  << "world_pack_hash=0x" << std::hex << topology.world_pack_hash << std::dec << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Thunder World Validation FAIL: " << error.what() << '\n';
        return 1;
    }
}
