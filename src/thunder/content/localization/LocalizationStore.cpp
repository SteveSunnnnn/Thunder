#include "thunder/content/localization/LocalizationStore.hpp"
#include <algorithm>
#include <sstream>

namespace thunder {

LocalizationStore::LocalizationStore() {
    register_game_concept("concept_seasonal_weather", "Seasonal Climate & Temperature",
        "Provinces experience seasonal thermal cycles across the year. Sub-zero winter temperatures cause ground freezing and snow cover, reducing agricultural vegetative throughput and increasing army movement attrition.",
        "geography");
    register_game_concept("concept_compound_terrain", "3-Layer Orthogonal Terrain",
        "Decoupled regional geography: Climate (governing weather/temperature cycles), Topography (governing combat width and movement cost), and Vegetation (governing natural soil fertility and building slots).",
        "geography");
    register_game_concept("concept_snow_cover", "Snow & Frost Cover",
        "Dynamic surface frost and snow accumulation resulting from sub-zero seasonal temperatures, visible on the 3D realistic terrain.",
        "geography");
    register_game_concept("concept_climate_temperature", "Province Temperature",
        "Thermal rating in Celsius derived from base climate, seasonal solar insolation, and altitude lapse rate.",
        "geography");
}

void LocalizationStore::add_entry(const std::string& lang, std::string key, std::string template_str) {
    dictionaries_[lang][std::move(key)] = std::move(template_str);
}

std::optional<std::string_view> LocalizationStore::get_raw(const std::string& key) const noexcept {
    auto lang_it = dictionaries_.find(current_language_);
    if (lang_it != dictionaries_.end()) {
        auto key_it = lang_it->second.find(key);
        if (key_it != lang_it->second.end()) return key_it->second;
    }
    // Fallback to "en"
    if (current_language_ != "en") {
        auto fb_it = dictionaries_.find("en");
        if (fb_it != dictionaries_.end()) {
            auto key_it = fb_it->second.find(key);
            if (key_it != fb_it->second.end()) return key_it->second;
        }
    }
    return std::nullopt;
}

void LocalizationStore::register_game_concept(std::string concept_id, std::string title, std::string description, std::string category) {
    GameConcept c;
    c.id = concept_id;
    c.title = std::move(title);
    c.description = std::move(description);
    c.category = std::move(category);
    concepts_[std::move(concept_id)] = std::move(c);
}

const GameConcept* LocalizationStore::get_game_concept(const std::string& concept_id) const noexcept {
    auto it = concepts_.find(concept_id);
    if (it != concepts_.end()) return &it->second;
    return nullptr;
}

std::string LocalizationStore::interpolate(std::string_view text,
                                          const std::map<std::string, std::string>& scopes) {
    std::string result;
    result.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[' && i + 1 < text.size()) {
            auto end_bracket = text.find(']', i + 1);
            if (end_bracket != std::string_view::npos) {
                std::string_view token = text.substr(i + 1, end_bracket - (i + 1));
                // Ignore rich text formatting tags like color/icon here
                if (token.starts_with("color:") || token.starts_with("icon:") ||
                    token == "/color" || token == "b" || token == "/b") {
                    result.append(text.substr(i, end_bracket - i + 1));
                } else {
                    auto it = scopes.find(std::string(token));
                    if (it != scopes.end()) {
                        result.append(it->second);
                    } else {
                        result.append(text.substr(i, end_bracket - i + 1));
                    }
                }
                i = end_bracket + 1;
                continue;
            }
        }
        result.push_back(text[i]);
        ++i;
    }
    return result;
}

std::string LocalizationStore::format(const std::string& key,
                                     const std::map<std::string, std::string>& scopes) const {
    auto raw_opt = get_raw(key);
    if (!raw_opt) return key; // Return raw key as fallback
    return interpolate(*raw_opt, scopes);
}

std::vector<RichTextToken> LocalizationStore::parse_rich_text(std::string_view text) {
    std::vector<RichTextToken> tokens;
    std::uint32_t current_color = 0xffffffffu;
    bool current_bold = false;

    std::string buffer;
    auto flush_buffer = [&]() {
        if (!buffer.empty()) {
            RichTextToken t;
            t.text = std::move(buffer);
            t.rgba = current_color;
            t.is_bold = current_bold;
            tokens.push_back(std::move(t));
            buffer.clear();
        }
    };

    std::size_t i = 0;
    while (i < text.size()) {
        // Check for Game Concept tag: @concept_key! or @concept_key|Display Text!
        if (text[i] == '@' && i + 1 < text.size()) {
            auto end_excl = text.find('!', i + 1);
            if (end_excl != std::string_view::npos) {
                std::string_view concept_str = text.substr(i + 1, end_excl - (i + 1));
                if (!concept_str.empty() && (concept_str.starts_with("concept_") || concept_str.find('.') != std::string_view::npos || concept_str.find('_') != std::string_view::npos)) {
                    flush_buffer();
                    std::string concept_id;
                    std::string display_name;
                    auto bar_pos = concept_str.find('|');
                    if (bar_pos != std::string_view::npos) {
                        concept_id = std::string(concept_str.substr(0, bar_pos));
                        display_name = std::string(concept_str.substr(bar_pos + 1));
                    } else {
                        concept_id = std::string(concept_str);
                        display_name = concept_id;
                        if (display_name.starts_with("concept_"))
                            display_name = display_name.substr(8);
                        // Clean underscores to spaces
                        std::replace(display_name.begin(), display_name.end(), '_', ' ');
                        if (!display_name.empty() && display_name[0] >= 'a' && display_name[0] <= 'z')
                            display_name[0] = static_cast<char>(display_name[0] - 'a' + 'A');
                    }

                    RichTextToken concept_token;
                    concept_token.text = std::move(display_name);
                    concept_token.concept_id = std::move(concept_id);
                    concept_token.is_concept_link = true;
                    concept_token.rgba = 0xff60c5bau; // Luminous cyan/gold link highlight
                    concept_token.is_bold = true;
                    tokens.push_back(std::move(concept_token));
                    i = end_excl + 1;
                    continue;
                }
            }
        }

        // Bracket tags like [icon:xyz] or [color:#rrggbb]
        if (text[i] == '[') {
            auto end = text.find(']', i + 1);
            if (end != std::string_view::npos) {
                std::string_view tag = text.substr(i + 1, end - (i + 1));
                if (tag.starts_with("icon:")) {
                    flush_buffer();
                    RichTextToken icon_tok;
                    icon_tok.icon_id = std::string(tag.substr(5));
                    icon_tok.is_icon = true;
                    tokens.push_back(std::move(icon_tok));
                    i = end + 1;
                    continue;
                } else if (tag.starts_with("color:")) {
                    flush_buffer();
                    std::string hex_str = std::string(tag.substr(6));
                    if (hex_str == "gold") {
                        current_color = 0xffd4af37u;
                    } else if (hex_str == "red") {
                        current_color = 0xffdc3545u;
                    } else if (hex_str == "green") {
                        current_color = 0xff28a745u;
                    } else if (!hex_str.empty() && hex_str[0] == '#') {
                        try {
                            std::uint32_t val = static_cast<std::uint32_t>(std::stoul(hex_str.substr(1), nullptr, 16));
                            current_color = 0xff000000u | val;
                        } catch (...) {}
                    }
                    i = end + 1;
                    continue;
                } else if (tag == "/color") {
                    flush_buffer();
                    current_color = 0xffffffffu;
                    i = end + 1;
                    continue;
                } else if (tag == "b") {
                    flush_buffer();
                    current_bold = true;
                    i = end + 1;
                    continue;
                } else if (tag == "/b") {
                    flush_buffer();
                    current_bold = false;
                    i = end + 1;
                    continue;
                }
            }
        }
        buffer.push_back(text[i]);
        ++i;
    }
    flush_buffer();
    return tokens;
}

} // namespace thunder
