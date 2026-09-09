#pragma once

#include "thunder/scripting/DynamicVariables.hpp"
#include "thunder/scripting/Scope.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace thunder {


struct DebugCommandResult {
    bool ok = true;
    std::string output;
};

class DebugConsole {
public:
    DebugConsole() = default;

    // Executes a text command line and returns the resulting status and text output
    [[nodiscard]] DebugCommandResult execute(World& world, std::string_view command_line);

    // Dynamic variable map registry for entities
    [[nodiscard]] DynamicVariableMap& entity_vars(ScopeRef scope) {
        return entity_vars_[scope];
    }
    [[nodiscard]] const DynamicVariableMap* find_entity_vars(ScopeRef scope) const {
        const auto it = entity_vars_.find(scope);
        return it != entity_vars_.end() ? &it->second : nullptr;
    }

private:
    [[nodiscard]] static std::vector<std::string_view> tokenize(std::string_view line);
    std::unordered_map<ScopeRef, DynamicVariableMap, ScopeRefHash> entity_vars_;
};

} // namespace thunder
