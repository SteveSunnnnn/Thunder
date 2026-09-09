#include "thunder/content/definition/LocalizationDatabase.hpp"

namespace thunder {

LocalizationDatabase::LocalizationDatabase(SymbolTable& symbols) : symbols_(symbols) {
    sym_localization_ = symbols_.intern("localization");
    languages_.reserve(16u);
}

void LocalizationDatabase::ingest(const ScriptParseResult& parsed) {
    for (const auto& object : parsed.objects) {
        if (object.type != sym_localization_) continue;
        auto& table = languages_[object.name.value()];
        if (table.empty()) table.reserve(object.fields.size() * 2u + 1u);
        for (const auto& field : object.fields) {
            if (field.kind != ScriptValueKind::Symbol) continue;
            table.insert_or_assign(field.key.value(), field.symbol);
        }
    }
}

std::string_view LocalizationDatabase::lookup(SymbolId language, SymbolId key, SymbolId fallback_language) const noexcept {
    auto find_in = [&](SymbolId lang) -> SymbolId {
        if (!lang.valid()) return {};
        const auto lit = languages_.find(lang.value());
        if (lit == languages_.end()) return {};
        const auto eit = lit->second.find(key.value());
        return eit == lit->second.end() ? SymbolId{} : eit->second;
    };
    auto value = find_in(language);
    if (!value.valid()) value = find_in(fallback_language);
    if (!value.valid()) return {};
    try { return symbols_.text(value); } catch (...) { return {}; }
}

std::size_t LocalizationDatabase::entry_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [language, table] : languages_) { (void)language; count += table.size(); }
    return count;
}

std::size_t LocalizationDatabase::memory_bytes() const noexcept {
    std::size_t bytes = languages_.size() * sizeof(std::pair<const std::uint32_t, Table>);
    for (const auto& [language, table] : languages_) {
        (void)language;
        bytes += table.size() * sizeof(std::pair<const std::uint32_t, SymbolId>);
    }
    return bytes;
}

} // namespace thunder
