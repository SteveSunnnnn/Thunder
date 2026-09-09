#include "thunder/scripting/SymbolTable.hpp"
#include <stdexcept>

namespace thunder {

SymbolTable::SymbolTable() {
    lookup_.reserve(4096u);
}

SymbolId SymbolTable::intern(std::string_view text_value) {
    if (const auto it = lookup_.find(text_value); it != lookup_.end()) return it->second;
    const auto raw = static_cast<SymbolId::rep_type>(strings_.size());
    strings_.emplace_back(text_value);
    const std::string_view stable{strings_.back()};
    const SymbolId id{raw};
    lookup_.emplace(stable, id);
    return id;
}

SymbolId SymbolTable::find(std::string_view text_value) const noexcept {
    const auto it = lookup_.find(text_value);
    return it == lookup_.end() ? SymbolId{} : it->second;
}

std::string_view SymbolTable::text(SymbolId id) const {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= strings_.size()) throw std::out_of_range("invalid SymbolId");
    return strings_[index];
}

std::size_t SymbolTable::memory_bytes() const noexcept {
    std::size_t bytes = 0;
    for (const auto& value : strings_) bytes += value.capacity() + 1u;
    bytes += lookup_.size() * (sizeof(std::string_view) + sizeof(SymbolId));
    return bytes;
}

} // namespace thunder
