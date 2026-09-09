#pragma once

#include "thunder/content/definition/ContentLoader.hpp"
#include "thunder/content/definition/DefinitionDatabase.hpp"
#include "thunder/content/definition/VirtualFileSystem.hpp"

#include <cstdint>
#include <vector>

namespace thunder {

class ThunderEngine;
class WorldPackReader;

// Owns symbols and compiled programs for the full lifetime of a running game.
// This prevents the desktop/client layer from keeping temporary parser objects
// alive merely because gameplay VMs reference the compiled program database.
class GameContentRuntime {
public:
    explicit GameContentRuntime(const ScriptRegistry& registry);

    [[nodiscard]] const ContentLoadResult& load(const VirtualFileSystem& vfs);
    bool install_new_game(ThunderEngine& engine,
                          std::int32_t history_date,
                          std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool install_new_game(ThunderEngine& engine,
                          std::int32_t history_date,
                          std::vector<ScriptCompileDiagnostic>& diagnostics,
                          const WorldPackReader* world_pack);

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] bool installed() const noexcept { return installed_; }
    [[nodiscard]] const ContentLoadResult& result() const noexcept { return result_; }
    [[nodiscard]] const DefinitionDatabase& definitions() const noexcept { return definitions_; }

private:
    const ScriptRegistry& registry_;
    SymbolTable symbols_;
    DefinitionDatabase definitions_;
    ContentLoadResult result_;
    bool loaded_ = false;
    bool installed_ = false;
};

} // namespace thunder
