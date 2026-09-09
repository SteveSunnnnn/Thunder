#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace thunder {

struct SymbolTag {};
using SymbolId = StrongId<SymbolTag>;

struct TransparentStringHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return (*this)(std::string_view{value});
    }
};

struct TransparentStringEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

// Startup/compiler-side interner. Strings live in a deque so string_view keys remain
// stable even as new symbols are inserted. Runtime systems traffic only in SymbolId.
class SymbolTable {
public:
    SymbolTable();

    [[nodiscard]] SymbolId intern(std::string_view text);
    [[nodiscard]] SymbolId find(std::string_view text) const noexcept;
    [[nodiscard]] std::string_view text(SymbolId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return strings_.size(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, SymbolId, TransparentStringHash, TransparentStringEqual> lookup_;
};

} // namespace thunder
