#pragma once
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace thunder {

class LocalizationDatabase {
public:
    explicit LocalizationDatabase(SymbolTable& symbols);

    void ingest(const ScriptParseResult& parsed);
    [[nodiscard]] std::string_view lookup(SymbolId language, SymbolId key, SymbolId fallback_language = {}) const noexcept;
    [[nodiscard]] std::size_t language_count() const noexcept { return languages_.size(); }
    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    using Table = std::unordered_map<std::uint32_t, SymbolId>;
    SymbolTable& symbols_;
    SymbolId sym_localization_{};
    std::unordered_map<std::uint32_t, Table> languages_;
};

} // namespace thunder
