#include "thunder/content/assets/AssetPack.hpp"
#include "thunder/content/assets/Material.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::array<float, 4> vec4(std::string_view text) {
    std::array<float, 4> out{};
    std::stringstream stream{std::string{text}};
    char comma = 0;
    if (!(stream >> out[0] >> comma) || comma != ',' ||
        !(stream >> out[1] >> comma) || comma != ',' ||
        !(stream >> out[2] >> comma) || comma != ',' || !(stream >> out[3])) {
        throw std::runtime_error("expected four comma-separated floats");
    }
    return out;
}

std::array<float, 3> vec3(std::string_view text) {
    std::array<float, 3> out{};
    std::stringstream stream{std::string{text}};
    char comma = 0;
    if (!(stream >> out[0] >> comma) || comma != ',' ||
        !(stream >> out[1] >> comma) || comma != ',' || !(stream >> out[2])) {
        throw std::runtime_error("expected three comma-separated floats");
    }
    return out;
}

std::uint64_t texture_hash(const std::string& value) {
    return value.empty() || value == "none" ? 0 : thunder::asset_key_hash(value);
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: thunder_material_cooker <material.txt> <output.thundermat>\n";
        return 2;
    }
    try {
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("cannot open material manifest");
        thunder::PbrMaterial material;
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            line = trim(std::move(line));
            if (line.empty() || line[0] == '#') continue;
            const auto equals = line.find('=');
            if (equals == std::string::npos) throw std::runtime_error("missing '=' on line " + std::to_string(line_number));
            const auto key = trim(line.substr(0, equals));
            const auto value = trim(line.substr(equals + 1));
            if (key == "base_color") material.base_color = vec4(value);
            else if (key == "emissive") material.emissive = vec3(value);
            else if (key == "metallic") material.metallic = std::stof(value);
            else if (key == "roughness") material.roughness = std::stof(value);
            else if (key == "normal_scale") material.normal_scale = std::stof(value);
            else if (key == "alpha_cutoff") material.alpha_cutoff = std::stof(value);
            else if (key == "flags") material.flags = static_cast<thunder::MaterialFlags>(std::stoul(value));
            else if (key == "base_color_texture") material.base_color_texture = texture_hash(value);
            else if (key == "normal_texture") material.normal_texture = texture_hash(value);
            else if (key == "orm_texture") material.orm_texture = texture_hash(value);
            else if (key == "emissive_texture") material.emissive_texture = texture_hash(value);
            else throw std::runtime_error("unknown material key on line " + std::to_string(line_number) + ": " + key);
        }
        material.write(argv[2]);
        std::cout << "Thunder material cooked: " << std::filesystem::path(argv[2]).string()
                  << " checksum=0x" << std::hex << material.checksum() << std::dec << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thunder_material_cooker: " << error.what() << '\n';
        return 1;
    }
}
