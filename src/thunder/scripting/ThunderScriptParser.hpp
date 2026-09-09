#pragma once
#include "thunder/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

enum class ScriptValueKind : std::uint8_t { None, Number, Symbol, Block, Expression };

struct ScriptNode {
    SymbolId key{};
    ScriptValueKind kind = ScriptValueKind::None;
    double number = 0.0;
    SymbolId symbol{};
    std::vector<ScriptNode> children;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    [[nodiscard]] const ScriptNode* find(SymbolId wanted) const noexcept;
};

struct ScriptObject {
    SymbolId type{};
    SymbolId name{};
    std::vector<ScriptNode> fields;
    std::uint32_t line = 0;
};

struct ScriptDiagnostic {
    std::string message;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct ScriptParseResult {
    std::vector<ScriptObject> objects;
    std::vector<ScriptDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class ThunderScriptParser {
public:
    explicit ThunderScriptParser(SymbolTable& symbols) : symbols_(symbols) {}
    [[nodiscard]] ScriptParseResult parse(std::string_view source, std::string_view source_name = {});

private:
    SymbolTable& symbols_;
};

} // namespace thunder
