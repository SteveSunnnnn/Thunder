#pragma once
#include "thunder/scripting/Scope.hpp"
#include <vector>

namespace thunder {
class World;

class ScopeResolver {
public:
    [[nodiscard]] static bool valid(const World& world, ScopeRef scope) noexcept;
    [[nodiscard]] static ScopeRef owner(const World& world, ScopeRef scope) noexcept;
    [[nodiscard]] static ScopeRef country(const World& world, ScopeRef scope) noexcept { return owner(world, scope); }
    [[nodiscard]] static ScopeRef market(const World& world, ScopeRef scope) noexcept;
    [[nodiscard]] static ScopeRef state(const World& world, ScopeRef scope) noexcept;
    [[nodiscard]] static ScopeRef province(const World& world, ScopeRef scope) noexcept;
    [[nodiscard]] static std::vector<ScopeRef> children(const World& world, ScopeRef scope, ScopeType target);
    [[nodiscard]] static std::vector<ScopeRef> all(const World& world, ScopeType type);
};
}
