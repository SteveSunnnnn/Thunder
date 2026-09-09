#include "thunder/runtime/engine/DebugConsole.hpp"
#include <charconv>
#include <sstream>

namespace thunder {

std::vector<std::string_view> DebugConsole::tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    while (start < line.size()) {
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t' || line[start] == '\r' || line[start] == '\n')) {
            ++start;
        }
        if (start >= line.size()) break;
        std::size_t end = start;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r' && line[end] != '\n') {
            ++end;
        }
        tokens.push_back(line.substr(start, end - start));
        start = end;
    }
    return tokens;
}

DebugCommandResult DebugConsole::execute(World& world, std::string_view command_line) {
    const auto tokens = tokenize(command_line);
    if (tokens.empty()) {
        return {true, ""};
    }

    const auto cmd = tokens[0];

    if (cmd == "help") {
        return {true,
            "Available Commands:\n"
            "  help                               - Show this command list\n"
            "  inspect <type> <id>                - Inspect country, state, prov, pop, market, army, navy, play\n"
            "  set_var <type> <id> <key> <val>    - Set dynamic integer variable on entity\n"
            "  get_var <type> <id> <key>          - Get dynamic variable from entity\n"
            "  tick [n]                           - Step simulation n weeks (default: 1)\n"
            "  resync                             - Compute authoritative world checksum\n"
            "  blockade <zone_id>                 - Check sea zone blockade efficiency\n"
        };
    }

    if (cmd == "resync") {
        std::ostringstream ss;
        ss << "World checksum: 0x" << std::hex << world.checksum();
        return {true, ss.str()};
    }

    if (cmd == "tick") {
        std::uint32_t count = 1;
        if (tokens.size() > 1) {
            std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), count);
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            world.grand_strategy.run_weekly_reference_tick();
        }
        std::ostringstream ss;
        ss << "Advanced simulation by " << count << " week(s).";
        return {true, ss.str()};
    }

    if (cmd == "inspect") {
        if (tokens.size() < 3) return {false, "Usage: inspect <type> <id>"};
        const auto type = tokens[1];
        std::uint32_t id = 0;
        std::from_chars(tokens[2].data(), tokens[2].data() + tokens[2].size(), id);

        std::ostringstream ss;
        if (type == "country") {
            if (id >= world.countries.size()) return {false, "Country ID out of range"};
            ss << "Country ID: " << id << ", Treasury: " << world.countries.treasuries()[id];
            return {true, ss.str()};
        }
        if (type == "pop") {
            if (id >= world.pops.size()) return {false, "Pop ID out of range"};
            if (!world.pops.slot_pool().is_index_alive(id)) return {false, "Pop ID refers to a dead slot"};
            ss << "Pop ID: " << id << ", Count: " << world.pops.populations()[id]
               << ", Employed: " << (world.pops.employed(PopId{id}) ? "yes" : "no");
            return {true, ss.str()};
        }

        if (type == "play") {
            if (id >= world.grand_strategy.diplomatic_plays().size()) return {false, "Play ID out of range"};
            const auto& p = world.grand_strategy.diplomatic_plays()[id];
            ss << "Play ID: " << id << ", Initiator: " << p.initiator.value() << ", Target: " << p.target.value();
            return {true, ss.str()};
        }
        if (type == "army") {
            if (id >= world.grand_strategy.armys().size()) return {false, "Army ID out of range"};
            const auto& a = world.grand_strategy.armys()[id];
            ss << "Army ID: " << id << ", Country: " << a.country.value() << ", Manpower: " << a.manpower;
            return {true, ss.str()};
        }
        return {false, "Unknown inspect type"};
    }

    if (cmd == "set_var") {
        if (tokens.size() < 5) return {false, "Usage: set_var <type> <id> <key> <int_val>"};
        const auto type = tokens[1];
        std::uint32_t id = 0;
        std::from_chars(tokens[2].data(), tokens[2].data() + tokens[2].size(), id);
        const auto key = tokens[3];
        std::int64_t val = 0;
        std::from_chars(tokens[4].data(), tokens[4].data() + tokens[4].size(), val);

        ScopeRef scope{};
        if (type == "country") scope = ScopeRef::country(CountryId{id});
        else if (type == "state") scope = ScopeRef::state(StateId{id});
        else if (type == "province") scope = ScopeRef::province(ProvinceId{id});
        else return {false, "Unsupported entity type for set_var"};

        entity_vars(scope).set_int(key, val);
        return {true, "Variable set successfully."};
    }

    if (cmd == "get_var") {
        if (tokens.size() < 4) return {false, "Usage: get_var <type> <id> <key>"};
        const auto type = tokens[1];
        std::uint32_t id = 0;
        std::from_chars(tokens[2].data(), tokens[2].data() + tokens[2].size(), id);
        const auto key = tokens[3];

        ScopeRef scope{};
        if (type == "country") scope = ScopeRef::country(CountryId{id});
        else if (type == "state") scope = ScopeRef::state(StateId{id});
        else if (type == "province") scope = ScopeRef::province(ProvinceId{id});
        else return {false, "Unsupported entity type for get_var"};

        const auto* vars = find_entity_vars(scope);
        if (!vars || !vars->has(key)) return {false, "Variable not found"};
        std::ostringstream ss;
        ss << key << " = " << vars->get_int(key).value_or(0);
        return {true, ss.str()};

    }

    if (cmd == "blockade") {
        if (tokens.size() < 2) return {false, "Usage: blockade <zone_id>"};
        std::uint32_t zone_id = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), zone_id);
        const auto eff = world.grand_strategy.sea_zone_blockade_level(SeaZoneId{zone_id});
        std::ostringstream ss;
        ss << "Sea Zone " << zone_id << " blockade efficiency: " << (eff / 10'000) << "%";
        return {true, ss.str()};
    }

    return {false, "Unknown command. Type 'help' for available commands."};
}

} // namespace thunder
